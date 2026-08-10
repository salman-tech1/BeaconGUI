/*
 * frame.c 
 *  Author: Muhmmad Salman
 */

#include "frame.h"
#include "com.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define FRAME_PARSER_TIMEOUT_MS   200U   /* mid-packet silence -> assume the
                                            sender reset; re-sync on next 0xAA */
#define FRAME_POLL_CHUNK_SIZE     256U

typedef enum {
    FRAME_STATE_WAIT_SYNC1,
    FRAME_STATE_WAIT_SYNC2,
    FRAME_STATE_READ_HEADER,
    FRAME_STATE_READ_PAYLOAD,
    FRAME_STATE_CHECK_CRC,
} frame_ParserState_t;

/*
everything the parser needs to 
remember between calls: which 
state it's in, 
*/
typedef struct {
    frame_ParserState_t state;
    uint8_t  buffer[FRAME_MAX_PAYLOAD_SIZE + FRAME_CRC_SIZE];
    uint16_t bytes_received;
    uint16_t payload_length;
    uint8_t  cmd;
    uint16_t seq;
    uint8_t  flags;
    uint32_t last_byte_tick;
} frame_Parser_t;

static frame_Parser_t         s_parser;
static frame_PacketCallback_t s_packet_cb = NULL;
static frame_Stats_t          s_stats;// wo counters: rx_packets_ok, rx_crc_errors.


// snaps the parser back to FRAME_STATE_WAIT_SYNC1 
// and clears the counters. Called after every successful
// packet, every CRC failure, and every mid-packet timeout.
static void parser_reset(void)
{
    s_parser.state          = FRAME_STATE_WAIT_SYNC1;
    s_parser.bytes_received = 0U;
    s_parser.payload_length = 0U;
    s_parser.last_byte_tick = xTaskGetTickCount();
}

/*
copies the parser's fields into a fresh frame_Packet_t
bumps rx_packets_ok, and calls s_packet_cb() if one's
 registered.
*/
static void deliver_packet(void)
{
    /*
    frame_Packet_t — one fully-validated 
    packet: cmd, seq, flags, payload_length, 
    and the payload bytes themselves.
    */
    frame_Packet_t pkt;
    pkt.cmd            = s_parser.cmd;
    pkt.seq            = s_parser.seq;
    pkt.flags          = s_parser.flags;
    pkt.payload_length = s_parser.payload_length;
    // 
    memcpy(pkt.payload, s_parser.buffer, s_parser.payload_length);

    s_stats.rx_packets_ok++;
    if (s_packet_cb != NULL) {
        s_packet_cb(&pkt);
    }
}


/*
The actual state machine, one byte at a time:

First checks: if we're mid-packet and it's been >200ms 
since the last byte, something died mid-transmission — reset and treat this byte as fresh.

WAIT_SYNC1 → looking for 0xAA. Everything else is ignored.

WAIT_SYNC2 → next byte must be 0x55 or we bail back to WAIT_SYNC1 
(the 0xAA was noise or mid-payload data, not a real sync).

READ_HEADER → collects exactly 6 bytes (length, cmd, seq, flags), 
then decides: if the claimed length is impossible, reject; if it's 0 
(like a heartbeat), skip straight to CRC; otherwise go collect the payload.

READ_PAYLOAD → collects exactly payload_length bytes, no scanning — 
this is why a 0xAA inside a payload doesn't confuse anything, we're just counting.

CHECK_CRC → collects 2 more bytes, recomputes the CRC over 
cmd+seq+flags+payload, compares it to what was received. 

Match → deliver_packet(). Mismatch → count it and discard. Either way, parser_reset().
*/
static void process_byte(uint8_t byte)
{
    uint32_t now = xTaskGetTickCount();


    if (s_parser.state != FRAME_STATE_WAIT_SYNC1) {
        // 
        if ((now - s_parser.last_byte_tick) > pdMS_TO_TICKS(FRAME_PARSER_TIMEOUT_MS)) {
            parser_reset();
        }
    }
    s_parser.last_byte_tick = now;

    switch (s_parser.state) {

    case FRAME_STATE_WAIT_SYNC1:
        if (byte == FRAME_SYNC_BYTE_1) {
            s_parser.state = FRAME_STATE_WAIT_SYNC2;
        }
        break;

    case FRAME_STATE_WAIT_SYNC2:
        if (byte == FRAME_SYNC_BYTE_2) {
            s_parser.bytes_received = 0U;
            s_parser.state          = FRAME_STATE_READ_HEADER;
        } else {
            parser_reset();
        }
        break;

    case FRAME_STATE_READ_HEADER:
        s_parser.buffer[s_parser.bytes_received++] = byte;
        if (s_parser.bytes_received == 6U) {
            s_parser.payload_length = (uint16_t)(s_parser.buffer[0])
                                    | (uint16_t)(s_parser.buffer[1] << 8);
            s_parser.cmd   = s_parser.buffer[2];
            s_parser.seq   = (uint16_t)(s_parser.buffer[3])
                           | (uint16_t)(s_parser.buffer[4] << 8);
            s_parser.flags = s_parser.buffer[5];

            if (s_parser.payload_length > FRAME_MAX_PAYLOAD_SIZE) {
                parser_reset();   /* reject impossible length */
            } else if (s_parser.payload_length == 0U) {
                s_parser.bytes_received = 0U;
                s_parser.state          = FRAME_STATE_CHECK_CRC;
            } else {
                s_parser.bytes_received = 0U;
                s_parser.state          = FRAME_STATE_READ_PAYLOAD;
            }
        }
        break;

    case FRAME_STATE_READ_PAYLOAD:
        s_parser.buffer[s_parser.bytes_received++] = byte;
        if (s_parser.bytes_received == s_parser.payload_length) {
            s_parser.bytes_received = 0U;
            s_parser.state          = FRAME_STATE_CHECK_CRC;
        }
        break;

    case FRAME_STATE_CHECK_CRC: {
        s_parser.buffer[s_parser.payload_length + s_parser.bytes_received] = byte;
        s_parser.bytes_received++;

        if (s_parser.bytes_received == FRAME_CRC_SIZE) {
            uint16_t received_crc =
                (uint16_t)(s_parser.buffer[s_parser.payload_length])
              | (uint16_t)(s_parser.buffer[s_parser.payload_length + 1U] << 8);

            /* static: keeps a 516-byte buffer out of the task stack */
            static uint8_t crc_input[4U + FRAME_MAX_PAYLOAD_SIZE];
            crc_input[0] = s_parser.cmd;
            crc_input[1] = (uint8_t)(s_parser.seq & 0xFFU);
            crc_input[2] = (uint8_t)(s_parser.seq >> 8U);
            crc_input[3] = s_parser.flags;
            memcpy(&crc_input[4], s_parser.buffer, s_parser.payload_length);

            uint16_t computed_crc = frame_Crc16(crc_input, 4U + s_parser.payload_length);

            if (received_crc == computed_crc) {
                deliver_packet();
            } else {
                s_stats.rx_crc_errors++;
            }
            parser_reset();
        }
        break;
    }

    default:
        parser_reset();
        break;
    }
}


/*
CRC-16/IBM: starts at 0xFFFF, XORs in each byte, 
then shifts 8 times per byte, applying the polynomial 
0xA001 whenever the low bit is set. This exact algorithm
 has to match on both MCUs bit-for-bit, which is what the 
 two-board test's crc_err=0 confirmed.
*/
uint16_t frame_Crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t b = 0U; b < 8U; b++) {
            crc = (crc & 0x0001U) ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                                   : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

frame_Status_t frame_Init(void)
{
    parser_reset();
    memset(&s_stats, 0, sizeof(s_stats));
    return FRAME_OK;
}

void frame_RegisterPacketCallback(frame_PacketCallback_t callback)
{
    s_packet_cb = callback;
}

void frame_GetStats(frame_Stats_t *out_stats)
{
    if (out_stats != NULL) {
        *out_stats = s_stats;
    }
}

/*
builds the wire frame byte by byte
into a static buffer (sync, length, cmd,
seq, flags, payload), computes the CRC
over the same cmd+seq+flags+payload used
on receive, appends it, and hands the 
whole thing to Com_Send().
*/
frame_Status_t frame_Send(uint8_t cmd, uint16_t seq, uint8_t flags,
                         const uint8_t *payload, uint16_t payload_length)
{
    static uint8_t frame[FRAME_MAX_FRAME_SIZE];
    uint16_t idx = 0U;

    if (payload_length > FRAME_MAX_PAYLOAD_SIZE) {
        return FRAME_ERROR;
    }

    /* Wire layout: [0]SYNC1 [1]SYNC2 [2-3]length [4]cmd [5-6]seq [7]flags
       [8..N]payload [N+1..N+2]CRC. CRC covers cmd+seq+flags+payload only
       (not sync bytes or length) — must match the STM32 side exactly. */
    frame[idx++] = FRAME_SYNC_BYTE_1;
    frame[idx++] = FRAME_SYNC_BYTE_2;
    frame[idx++] = (uint8_t)(payload_length & 0xFFU);
    frame[idx++] = (uint8_t)(payload_length >> 8U);
    frame[idx++] = cmd;
    frame[idx++] = (uint8_t)(seq & 0xFFU);
    frame[idx++] = (uint8_t)(seq >> 8U);
    frame[idx++] = flags;

    if ((payload != NULL) && (payload_length > 0U)) {
        memcpy(&frame[idx], payload, payload_length);
        idx = (uint16_t)(idx + payload_length);
    }

    /* static: keeps a 516-byte buffer out of the task stack (this was
       declared WITHOUT `static` in the original uart_send_packet(),
       despite a comment claiming otherwise — fixed here). */
    static uint8_t crc_input[4U + FRAME_MAX_PAYLOAD_SIZE];
    crc_input[0] = cmd;
    crc_input[1] = (uint8_t)(seq & 0xFFU);
    crc_input[2] = (uint8_t)(seq >> 8U);
    crc_input[3] = flags;
    if ((payload != NULL) && (payload_length > 0U)) {
        memcpy(&crc_input[4], payload, payload_length);
    }
    uint16_t crc = frame_Crc16(crc_input, 4U + payload_length);
    frame[idx++] = (uint8_t)(crc & 0xFFU);
    frame[idx++] = (uint8_t)(crc >> 8U);

    return (Com_Send(frame, idx) == COM_OK) ? FRAME_OK : FRAME_ERROR;
}


/*
repeatedly calls Com_ReadAvailable() 
and feeds every byte it gets into process_byte(),
until Com_ReadAvailable() returns 0 (nothing left this cycle).
*/
void frame_Poll(void)
{
    uint8_t  chunk[FRAME_POLL_CHUNK_SIZE];
    uint16_t n;

    while ((n = Com_ReadAvailable(chunk, sizeof(chunk))) > 0U) {
        for (uint16_t i = 0U; i < n; i++) {
            process_byte(chunk[i]);
        }
    }
}

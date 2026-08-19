/*
 * frame.c
 *
 *  Created on: Jul 29, 2026
 *      Author: Muhmmad Salman
 */



#include "frame.h"
#include "com.h"
#include "stm32h7xx_hal.h"   /* HAL_GetTick() only */
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
static  frame_Stats_t          s_stats;

/* ============================================================
   PRIVATE
   ============================================================ */

static void parser_reset(void)
{
    s_parser.state          = FRAME_STATE_WAIT_SYNC1;
    s_parser.bytes_received = 0U;
    s_parser.payload_length = 0U;
    s_parser.last_byte_tick = HAL_GetTick();
}

static void deliver_packet(void)
{
    frame_Packet_t pkt;
    pkt.cmd            = s_parser.cmd;
    pkt.seq            = s_parser.seq;
    pkt.flags          = s_parser.flags;
    pkt.payload_length = s_parser.payload_length;
    memcpy(pkt.payload, s_parser.buffer, s_parser.payload_length);

    s_stats.rx_packets_ok++;
    if (s_packet_cb != NULL) {
        s_packet_cb(&pkt);
    }
}

static void process_byte(uint8_t byte)
{
    uint32_t now = HAL_GetTick();

    if (s_parser.state != FRAME_STATE_WAIT_SYNC1) {
        if ((now - s_parser.last_byte_tick) > FRAME_PARSER_TIMEOUT_MS) {
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

frame_Status_t frame_Send(uint8_t cmd, uint16_t seq, uint8_t flags,
                         const uint8_t *payload, uint16_t payload_length)
{
    static uint8_t frame[FRAME_MAX_FRAME_SIZE];
    uint16_t idx = 0U;

    if (payload_length > FRAME_MAX_PAYLOAD_SIZE) {
        return FRAME_ERROR;
    }

    /* Wire layout: [0]SYNC1 [1]SYNC2 [2-3]length [4]cmd [5-6]seq [7]flags
       [8..N]payload [N+1..N+2]CRC. CRC covers cmd + seq + flags + payload only
       (not sync bytes or length) — must match the ESP32 side exactly. */
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

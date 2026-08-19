/*
 * frame.h  
 *  Author: Muhmmad Salman
 */

#ifndef FRAME_H
#define FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define FRAME_MAX_PAYLOAD_SIZE   512U
#define FRAME_HEADER_SIZE        8U
#define FRAME_CRC_SIZE           2U
#define FRAME_MAX_FRAME_SIZE     (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD_SIZE + FRAME_CRC_SIZE)

#define FRAME_SYNC_BYTE_1        0xAAU
#define FRAME_SYNC_BYTE_2        0x55U

#define FRAME_FLAG_ACK           (1U << 0)
#define FRAME_FLAG_NACK          (1U << 1)
#define FRAME_FLAG_RESPONSE      (1U << 2)
#define FRAME_FLAG_EVENT         (1U << 3)
#define FRAME_FLAG_FRAGMENT      (1U << 4)

/* Command IDs. Frame doesn't interpret these — it just carries them.
   Meaning/handling is entirely a Link-layer (app) concern. */
#define CMD_HEARTBEAT           0x01U   /* ESP32 -> STM32 : keep-alive ping */
#define CMD_HEARTBEAT_ACK       0x02U   /* STM32 -> ESP32 : keep-alive pong */
#define CMD_WIFI_STATUS         0x10U   /* ESP32 -> STM32 : WiFi connected/disconnected */
#define CMD_RSSI                0x11U   /* ESP32 -> STM32 : WiFi signal strength */
#define CMD_WIFI_STATUS_REQ     0x12U   /* STM32 -> ESP32 : request current wifi status */
#define CMD_OTA_START           0x20U   /* ESP32 -> STM32 : begin OTA firmware transfer */
#define CMD_OTA_DATA            0x21U   /* ESP32 -> STM32 : firmware data chunk */
#define CMD_OTA_END             0x22U   /* ESP32 -> STM32 : OTA transfer complete */
#define CMD_OTA_ACK             0x23U   /* STM32 -> ESP32 : chunk received successfully */
#define CMD_TIME_SYNC           0x30U   /* ESP32 -> STM32 : current Unix timestamp */
#define CMD_TIME_REQ            0x31U   /* STM32 -> ESP32 : request current time */
#define CMD_SLOT_INFO_REQ       0x40U   /* ESP32 -> STM32, no payload */
#define CMD_SLOT_INFO_RESP      0x41U   /* STM32 -> ESP32, payload = uint8 target_slot */
#define CMD_OTA_READY           0x24U   /* STM32 -> ESP32 : slot erased, ready for data */
typedef enum {
    FRAME_OK    = 0,
    FRAME_ERROR = -1,
} frame_Status_t;

/** One validated, CRC-correct packet. Plain data — no RTOS types. */
typedef struct {
    uint8_t  cmd;
    uint16_t seq;
    uint8_t  flags;
    uint16_t payload_length;
    uint8_t  payload[FRAME_MAX_PAYLOAD_SIZE];
} frame_Packet_t;

typedef struct {
    uint32_t rx_packets_ok;   /* frames that passed CRC */
    uint32_t rx_crc_errors;   /* frames discarded — CRC mismatch */
} frame_Stats_t;

/**
 * @brief  Fired once per validated packet.
 * @note   Runs in whatever context calls frame_Poll() — a task, never
 *         an ISR. Keep implementations fast: copy and return.
 */
typedef void (*frame_PacketCallback_t)(const frame_Packet_t *packet);

frame_Status_t frame_Init(void);
void          frame_RegisterPacketCallback(frame_PacketCallback_t callback);
void          frame_GetStats(frame_Stats_t *out_stats);

/**
 * @brief  Build a frame and hand it to Com_Send().
 * @note   NOT reentrant (uses a single static frame buffer). If more
 *         than one task can call this, serialize access yourself —
 *         see link_Send() in link.c for a ready-made wrapper.
 */
frame_Status_t frame_Send(uint8_t cmd, uint16_t seq, uint8_t flags,
                         const uint8_t *payload, uint16_t payload_length);

/**
 * @brief  Drain whatever new bytes Com has buffered and feed them
 *         through the parser. Call this once per rx-task loop iteration.
 */
void frame_Poll(void);

/** CRC-16/IBM. MUST be byte-for-byte identical to the STM32 implementation. */
uint16_t frame_Crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_H */
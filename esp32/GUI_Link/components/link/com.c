/*
 * com.c  (ESP32 side)
 *  Author: Muhmmad Salman
 */

#include "com.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "COM";

#define COM_UART_PORT_NUM         UART_NUM_1

#define COM_UART_BAUD_RATE        115200U

#define COM_UART_TX_PIN           17   /* GPIO17 = TX */
#define COM_UART_RX_PIN           16   /* GPIO16 = RX */

/* ESP-IDF driver's own internal RX/TX ring buffers. */
#define COM_UART_RX_BUF_SIZE      1024U
#define COM_UART_TX_BUF_SIZE      1024U

/* Depth of the driver's UART event queue.
 */
#define COM_UART_EVENT_QUEUE_LEN  20U

/* Largest single buffer any upper layer will ever hand to Com_Send(). */
#define COM_MAX_TX_SIZE           600U

/* How long UART RX poll */
#define COM_RX_POLL_TIMEOUT_MS    10U

/* Com Tx to wait  */
#define COM_TX_WAIT_TIMEOUT_MS    100U

static QueueHandle_t        s_uart_event_queue = NULL;
static Com_EventCallback_t  s_event_cb         = NULL;

/*
configures baud/pins/parity, then calls
*/
Com_Status_t Com_Init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = COM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    if (uart_param_config(COM_UART_PORT_NUM, &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed");
        return COM_ERROR;
    }

    if (uart_set_pin(COM_UART_PORT_NUM, COM_UART_TX_PIN, COM_UART_RX_PIN,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed");
        return COM_ERROR;
    }
    
    // uart Driver 
    if (uart_driver_install(COM_UART_PORT_NUM, COM_UART_RX_BUF_SIZE,
                             COM_UART_TX_BUF_SIZE, COM_UART_EVENT_QUEUE_LEN,
                             &s_uart_event_queue, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed");
        return COM_ERROR;
    }

    return COM_OK;
}

/*
just stores the pointer in s_event_cb. Nothing clever
*/
void Com_RegisterEventCallback(Com_EventCallback_t callback)
{
    s_event_cb = callback;
}

/*
Writing Bytes 
*/
Com_Status_t Com_Send(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) || (length > COM_MAX_TX_SIZE)) {
        return COM_ERROR;
    }

    int written = uart_write_bytes(COM_UART_PORT_NUM, (const char *)data, length);
    if (written != (int)length) {
        return COM_ERROR;
    }

    // Blokcking wait for Transmission 
    if (uart_wait_tx_done(COM_UART_PORT_NUM,
                           pdMS_TO_TICKS(COM_TX_WAIT_TIMEOUT_MS)) != ESP_OK) {
        return COM_TIMEOUT;
    }

    return COM_OK;
}


/*
read the available bytes 
*/uint16_t Com_ReadAvailable(uint8_t *dst, uint16_t max_length)
{
    if ((dst == NULL) || (max_length == 0U)) {
        return 0U;
    }

    if (s_uart_event_queue != NULL) {
        uart_event_t event;
        while (xQueueReceive(s_uart_event_queue, &event, 0) == pdTRUE) {
            switch (event.type) {
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                /* Buffer overflow - data IS lost, flush is correct */
                uart_flush_input(COM_UART_PORT_NUM);
                xQueueReset(s_uart_event_queue);
                if (s_event_cb != NULL) {
                    s_event_cb(COM_EVENT_ERROR);
                }
                return 0U;
            
            case UART_BREAK:
                /* Break condition - line went low */
                if (s_event_cb != NULL) {
                    s_event_cb(COM_EVENT_ERROR);
                }
                break;
                
            case UART_FRAME_ERR:
            case UART_PARITY_ERR:
                /* Single-byte errors - DON'T flush!
                 * The bad byte fails CRC and is discarded by parser. */
                ESP_LOGW(TAG, "UART frame/parity error (single byte)");
                break;
                
            default:
                break;
            }
        }
    }

    int len = uart_read_bytes(COM_UART_PORT_NUM, dst, max_length,
                               pdMS_TO_TICKS(COM_RX_POLL_TIMEOUT_MS));
    if (len <= 0) {
        return 0U;
    }

    if (s_event_cb != NULL) {
        s_event_cb(COM_EVENT_RX_DATA);
    }

    return (uint16_t)len;
}
/*
flushes the input buffer and resets the
 event queue.
*/
Com_Status_t Com_Recover(void)
{
    uart_flush_input(COM_UART_PORT_NUM);
    if (s_uart_event_queue != NULL) {
        xQueueReset(s_uart_event_queue);
    }
    return COM_OK;
}
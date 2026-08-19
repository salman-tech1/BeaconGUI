
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>  /* sscanf */

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "Tasks_common.h"
#include "ota.h"
#include "link.h"
#include "frame.h"

static const char *TAG = "OTA";

#define SLOT_INFO_RESP_TIMEOUT_MS   3000U
#define GITHUB_API_URL              "https://api.github.com/repos/salman-tech1/BeaconView-Firmware/releases/latest"
#define HTTP_BUF_SIZE               8192
#define HTTP_RX_BUF_SIZE            4096
#define HTTP_TX_BUF_SIZE            2048
#define OTA_ACK_TIMEOUT_MS          10000U   
#define OTA_READY_TIMEOUT_MS        30000U   



static SemaphoreHandle_t s_slot_resp_sem = NULL;
static SemaphoreHandle_t s_ack_sem       = NULL;

static SemaphoreHandle_t s_ready_sem = NULL;

static char http_response_buf[HTTP_BUF_SIZE];
static int  http_response_len = 0;

static uint8_t        chunk_buf[FRAME_MAX_PAYLOAD_SIZE];
static uint16_t       chunk_fill     = 0;
static uint16_t       ota_seq        = 0;
static uint32_t       total_received = 0;
static uint32_t       chunk_index    = 0;
static uint32_t       running_crc    = 0xFFFFFFFFU;
static volatile bool  s_transfer_aborted = false;

static OTA_SlotQueryResult_t s_last_slot_query = {0};

/* ── Internal Prototypes ──────────────────────────────────────────────── */
static bool send_chunk_and_wait_ack(uint8_t cmd, uint16_t seq, const uint8_t *data, uint16_t len);
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
static esp_err_t download_event_handler(esp_http_client_event_t *evt);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static OTA_MGR_Status_t ota_query_target_slot(OTA_SlotQueryResult_t *out_result);
static OTA_MGR_Status_t ota_check_version(OTA_TargetSlot_t target_slot, OTA_FirmwareInfo_t *out_info);
static OTA_MGR_Status_t ota_download_and_transfer(const OTA_FirmwareInfo_t *info, uint8_t fw_major, uint8_t fw_minor, uint8_t fw_patch);
static void ota_task(void *pvParameters);



void ota_notify_ota_ready(void)
{
    if (s_ready_sem != NULL) xSemaphoreGive(s_ready_sem);
}

/* ── CRC32 ───────────────────────────────────────────────────────────── */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 1U) crc = (crc >> 1) ^ 0xEDB88320U;
            else crc >>= 1;
        }
    }
    return crc;
}

/* ── HTTP Event Handlers ──────────────────────────────────────────────── */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (http_response_len + evt->data_len < HTTP_BUF_SIZE) {
            memcpy(http_response_buf + http_response_len, evt->data, evt->data_len);
            http_response_len += evt->data_len;
        } else {
            ESP_LOGE(TAG, "GitHub API response truncated! Increase HTTP_BUF_SIZE");
        }
    }
    return ESP_OK;
}

static esp_err_t download_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        const uint8_t *src = (const uint8_t *)evt->data;
        int remaining = evt->data_len;

        total_received += (uint32_t)evt->data_len;
        running_crc = crc32_update(running_crc, src, (uint32_t)evt->data_len);

        while (remaining > 0) {
            uint16_t space = (uint16_t)(FRAME_MAX_PAYLOAD_SIZE - chunk_fill);
            uint16_t copy  = (remaining < (int)space) ? (uint16_t)remaining : space;

            memcpy(chunk_buf + chunk_fill, src, copy);
            chunk_fill += copy;
            src        += copy;
            remaining  -= copy;

            if (chunk_fill == FRAME_MAX_PAYLOAD_SIZE) {
                if (!send_chunk_and_wait_ack(CMD_OTA_DATA, ota_seq++, chunk_buf, FRAME_MAX_PAYLOAD_SIZE)) {
                    ESP_LOGE(TAG, "No ACK for chunk %lu — aborting", chunk_index);
                    s_transfer_aborted = true;
                    break;
                }
                chunk_index++;
                chunk_fill = 0;
            }
        }
    }
    return ESP_OK;
}

/* ── Link Layer Interactions ──────────────────────────────────────────── */
void ota_notify_slot_info_resp(const uint8_t *payload, uint16_t len)
{
    if (len >= 4U) {
        s_last_slot_query.target_slot   = (OTA_TargetSlot_t)payload[0];
        s_last_slot_query.running_major = payload[1];
        s_last_slot_query.running_minor = payload[2];
        s_last_slot_query.running_patch = payload[3];
    }
    if (s_slot_resp_sem != NULL) xSemaphoreGive(s_slot_resp_sem);
}

void ota_notify_ota_ack(void)
{
    if (s_ack_sem != NULL) xSemaphoreGive(s_ack_sem);
}

static bool send_chunk_and_wait_ack(uint8_t cmd, uint16_t seq, const uint8_t *data, uint16_t len)
{
    if (s_ack_sem == NULL) s_ack_sem = xSemaphoreCreateBinary();
    else xSemaphoreTake(s_ack_sem, 0);

    link_Send(cmd, seq, 0, data, len);
    return xSemaphoreTake(s_ack_sem, pdMS_TO_TICKS(OTA_ACK_TIMEOUT_MS)) == pdTRUE;
}

/* ── OTA Logic ────────────────────────────────────────────────────────── */
static OTA_MGR_Status_t ota_query_target_slot(OTA_SlotQueryResult_t *out_result)
{
    if (s_slot_resp_sem == NULL) s_slot_resp_sem = xSemaphoreCreateBinary();
    else xSemaphoreTake(s_slot_resp_sem, 0);

    ESP_LOGI(TAG, "Requesting target slot + version from STM32...");
    link_Send(CMD_SLOT_INFO_REQ, 0, 0, NULL, 0);

    if (xSemaphoreTake(s_slot_resp_sem, pdMS_TO_TICKS(SLOT_INFO_RESP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "STM32 did not respond to slot info request (timeout)");
        return OTA_MGR_ERR_SLOT_QUERY;
    }

    *out_result = s_last_slot_query;
    ESP_LOGI(TAG, "STM32 target slot: %c | running version: %u.%u.%u",
             (out_result->target_slot == OTA_TARGET_SLOT_A) ? 'A' : 'B',
             out_result->running_major, out_result->running_minor, out_result->running_patch);
    return OTA_MGR_OK;
}

static OTA_MGR_Status_t ota_check_version(OTA_TargetSlot_t target_slot, OTA_FirmwareInfo_t *out_info)
{
    memset(http_response_buf, 0, sizeof(http_response_buf));
    http_response_len = 0;

    esp_http_client_config_t config = {
        .url = GITHUB_API_URL, .event_handler = http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL, .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent", "ESP32-OTA");
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return OTA_MGR_ERR_HTTP;
    }

    cJSON *root = cJSON_Parse(http_response_buf);
    if (root == NULL) return OTA_MGR_ERR_HTTP;

    cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
    if (tag == NULL) { cJSON_Delete(root); return OTA_MGR_ERR_HTTP; }
    strncpy(out_info->version, tag->valuestring, sizeof(out_info->version) - 1);

    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (assets == NULL || cJSON_GetArraySize(assets) == 0) { cJSON_Delete(root); return OTA_MGR_ERR_HTTP; }

    const char *slot_needle = (target_slot == OTA_TARGET_SLOT_A) ? "slota" : "slotb";
    cJSON *asset = NULL;
    int asset_count = cJSON_GetArraySize(assets);

    for (int i = 0; i < asset_count; i++) {
        cJSON *candidate = cJSON_GetArrayItem(assets, i);
        cJSON *name = cJSON_GetObjectItem(candidate, "name");
        if (name == NULL || name->valuestring == NULL) continue;

        char namebuf[64];
        strncpy(namebuf, name->valuestring, sizeof(namebuf) - 1);
        namebuf[sizeof(namebuf) - 1] = '\0';
        for (char *p = namebuf; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);

        if (strstr(namebuf, slot_needle) != NULL) { asset = candidate; break; }
    }

    if (asset == NULL) {
        ESP_LOGW(TAG, "No asset matched '%s' — using first asset", slot_needle);
        asset = cJSON_GetArrayItem(assets, 0);
    }

    cJSON *dl_url = cJSON_GetObjectItem(asset, "browser_download_url");
    cJSON *asset_size = cJSON_GetObjectItem(asset, "size");
    if (dl_url == NULL || asset_size == NULL) { cJSON_Delete(root); return OTA_MGR_ERR_HTTP; }

    strncpy(out_info->download_url, dl_url->valuestring, sizeof(out_info->download_url) - 1);
    out_info->firmware_size = (uint32_t)asset_size->valueint;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Latest: %s | Slot: %c | Size: %lu bytes", out_info->version, 
             (target_slot == OTA_TARGET_SLOT_A) ? 'A' : 'B', out_info->firmware_size);
    return OTA_MGR_OK;
}

static OTA_MGR_Status_t ota_download_and_transfer(const OTA_FirmwareInfo_t *info, uint8_t fw_major, uint8_t fw_minor, uint8_t fw_patch)
{
    total_received = 0; chunk_index = 0; chunk_fill = 0; ota_seq = 0;
    running_crc = 0xFFFFFFFFU; s_transfer_aborted = false;

    if (s_ready_sem == NULL) s_ready_sem = xSemaphoreCreateBinary();
    else xSemaphoreTake(s_ready_sem, 0);   /* clear any stale signal */

    uint8_t start_payload[7];
    memcpy(&start_payload[0], &info->firmware_size, sizeof(uint32_t));
    start_payload[4] = fw_major; start_payload[5] = fw_minor; start_payload[6] = fw_patch;
    link_Send(CMD_OTA_START, ota_seq++, 0, start_payload, sizeof(start_payload));

    ESP_LOGI(TAG, "Waiting for STM32 to finish erasing target slot...");
    if (xSemaphoreTake(s_ready_sem, pdMS_TO_TICKS(OTA_READY_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGE(TAG, "No CMD_OTA_READY within %lu ms — aborting", (unsigned long)OTA_READY_TIMEOUT_MS);
        return OTA_MGR_ERR_TRANSFER;
    }
    ESP_LOGI(TAG, "STM32 ready — beginning download");



    esp_http_client_config_t config = {
        .url = info->download_url, .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = download_event_handler,
        .buffer_size = HTTP_RX_BUF_SIZE, .buffer_size_tx = HTTP_TX_BUF_SIZE, .max_redirection_count = 5,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent", "ESP32-OTA");
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) return OTA_MGR_ERR_DOWNLOAD;
    if (s_transfer_aborted) return OTA_MGR_ERR_TRANSFER;

    if (chunk_fill > 0) {
        if (!send_chunk_and_wait_ack(CMD_OTA_DATA, ota_seq++, chunk_buf, chunk_fill))
            return OTA_MGR_ERR_TRANSFER;
        chunk_index++; chunk_fill = 0;
    }

    if (total_received != info->firmware_size) return OTA_MGR_ERR_DOWNLOAD;

    uint32_t final_crc = running_crc ^ 0xFFFFFFFFU;
    ESP_LOGI(TAG, "CRC32: 0x%08lX", final_crc);
    link_Send(CMD_OTA_END, ota_seq++, 0, (const uint8_t *)&final_crc, (uint16_t)sizeof(final_crc));
    return OTA_MGR_OK;
}



static void ota_task(void *pvParameters)
{
    (void)pvParameters;
    OTA_FirmwareInfo_t     info = {0};
    OTA_SlotQueryResult_t  slot_query;

    ESP_LOGI(TAG, "=== OTA Update Check Started ===");

    if (ota_query_target_slot(&slot_query) != OTA_MGR_OK)
    {
        ESP_LOGE(TAG, "Could not determine target slot — aborting");
        vTaskDelete(NULL);
        return;
    }

    if (ota_check_version(slot_query.target_slot, &info) == OTA_MGR_OK)
    {
        unsigned latest_major = 0, latest_minor = 0, latest_patch = 0;
        const char *v = info.version;
        if (*v == 'v' || *v == 'V') v++;

        if (sscanf(v, "%u.%u.%u", &latest_major, &latest_minor, &latest_patch) == 3)
        {
            ESP_LOGI(TAG, "Running: %u.%u.%u | Latest: %s",
                     slot_query.running_major, slot_query.running_minor, 
                     slot_query.running_patch, info.version);

            bool newer = (latest_major > slot_query.running_major) ||
                         (latest_major == slot_query.running_major && latest_minor > slot_query.running_minor) ||
                         (latest_major == slot_query.running_major && latest_minor == slot_query.running_minor && latest_patch > slot_query.running_patch);

            if (!newer)
            {
                ESP_LOGI(TAG, "Firmware up-to-date — skipping OTA");
                vTaskDelete(NULL);
                return;
            }
            
            ESP_LOGI(TAG, "New firmware available — starting download");
            OTA_MGR_Status_t result = ota_download_and_transfer(
                &info, 
                (uint8_t)latest_major, 
                (uint8_t)latest_minor, 
                (uint8_t)latest_patch
            );
            
            if (result == OTA_MGR_OK)
            {
                ESP_LOGI(TAG, "=== OTA Transfer Complete — STM32 will reboot ===");
            }
            else
            {
                ESP_LOGE(TAG, "=== OTA Transfer FAILED: %d ===", result);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Could not parse version tag '%s' — proceeding blindly", info.version);
            ota_download_and_transfer(&info, 0, 0, 0);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to check version from GitHub");
    }

    vTaskDelete(NULL);
}


void ota_init(void)
{
    if (xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK_SIZE, NULL, OTA_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
    }
}
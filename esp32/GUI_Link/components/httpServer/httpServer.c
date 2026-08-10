/* http_server.c
 *  - Serves an embedded HTML page listing scanned WiFi networks.
 *  - Lets the user tap a network, type a password, and POST it.
 *  - On success, wifi.c switches the STA link live (AP stays up throughout).
 *
 * Reachable at http://beacon.local (mDNS, set up in wifi.c) or directly at
 * the AP's IP (192.168.0.2 by default) when a phone joins the ESP's own AP.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include "Tasks_common.h"
#include "wifi.h"
#include "httpServer.h"

static const char *TAG = "httpServer";

static httpd_handle_t s_server = NULL;

/* Set while a background reconnect (spawned by /api/connect) is running,
 * so a second request can't kick off a competing reconnect mid-flight. */
static volatile bool s_connect_in_progress = false;

/* index.html is embedded straight into the binary at build time — see
 * EMBED_TXTFILES in this component's CMakeLists.txt. Symbol names are
 * derived from the embedded file's path (web/index.html). */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

#define CONNECT_BODY_MAX 512



// to render the home screen when user t ype
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = (size_t)(index_html_end - index_html_start);

    return httpd_resp_send(req, (const char *)index_html_start, len);
}


// scan request if it get called it scan the wifi networks and 
// sends the available wifi networks 
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_ap_record_t records[WIFI_MAX_SCAN_RESULTS];
    uint16_t count = 0;

    esp_err_t err = wifi_scan_networks(records, WIFI_MAX_SCAN_RESULTS, &count);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "scan failed: 0x%x", err);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"scan_failed\"}");
    }

    cJSON *root = cJSON_CreateArray();
    for (uint16_t i = 0; i < count; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(root, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_sendstr(req, json ? json : "[]");

    cJSON_Delete(root);
    free(json);
    return send_err;
}

// Status for wifi it creates a jSon Query and send it to the 
// Client or webPage 
static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", wifi_is_connected());
    cJSON_AddStringToObject(root, "ip", wifi_get_ip());
    cJSON_AddNumberToObject(root, "rssi", wifi_get_rssi());

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json ? json : "{}");

    cJSON_Delete(root);
    free(json);
    return err;
}

typedef struct
{
    char ssid[33];
    char pass[65];
} connect_task_args_t;

static void connect_task(void *arg)
{
    connect_task_args_t *args = (connect_task_args_t *)arg;

    /* Give the "connecting" response we already sent a moment to actually
     * leave the radio before we tear down the STA link. This matters when
     * the client itself is browsing us over that same STA network — if we
     * disconnect immediately, we risk killing the socket mid-send (as seen
     * in testing: "error in send: 113" right as the STA interface reset). */
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t err = wifi_sta_connect_to(args->ssid, args->pass, 15000);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Background connect to '%s' succeeded — IP=%s", args->ssid, wifi_get_ip());
    }
    else
    {
        ESP_LOGW(TAG, "Background connect to '%s' failed: 0x%x", args->ssid, err);
    }

    free(args);
    s_connect_in_progress = false;
    vTaskDelete(NULL);
}

// this is a post handler when a 
// Browser sends a data it 
static esp_err_t connect_post_handler(httpd_req_t *req)
{

    // check the content length 
    if (req->content_len <= 0 || req->content_len >= CONNECT_BODY_MAX)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"bad_body\"}");
    }

    char buf[CONNECT_BODY_MAX];
    size_t total = 0;
    while (total < req->content_len)
    {
        int r = httpd_req_recv(req, buf + total, req->content_len - total);
        if (r == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue; /* retry on a socket read timeout */
        }
        if (r <= 0)
        {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"error\":\"recv_failed\"}");
        }
        total += (size_t)r;
    }
    buf[total] = '\0';

    // after parse the Buffer for JSON 
    // to find the ssid and password
    cJSON *body = cJSON_Parse(buf);
    if (body == NULL)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"invalid_json\"}");
    }

    // Get object from the Json Query string 
    cJSON *ssid_j = cJSON_GetObjectItemCaseSensitive(body, "ssid");
    cJSON *pass_j = cJSON_GetObjectItemCaseSensitive(body, "password");

    if (!cJSON_IsString(ssid_j) || ssid_j->valuestring[0] == '\0')
    {
        cJSON_Delete(body);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"missing_ssid\"}");
    }

    // 
    if (s_connect_in_progress)
    {
        cJSON_Delete(body);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"busy\",\"message\":\"A connection attempt is already in progress\"}");
    }

    // Dynamic memory for 
    connect_task_args_t *args = calloc(1, sizeof(*args));
    if (args == NULL)
    {
        cJSON_Delete(body);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"no_memory\"}");
    }
    strlcpy(args->ssid, ssid_j->valuestring, sizeof(args->ssid));
    strlcpy(args->pass, cJSON_IsString(pass_j) ? pass_j->valuestring : "", sizeof(args->pass));
    cJSON_Delete(body);

    ESP_LOGI(TAG, "Connect request: SSID='%s'", args->ssid);

    /* Reply FIRST, over the connection that's still alive right now — then
     * do the actual disconnect/reconnect in the background. Doing it the
     * other way around (reconnect, then reply) is what caused the socket
     * errors: the STA interface fully resets during reconnect, and if the
     * client reached us over that same STA network, its HTTP connection
     * dies before we get a chance to answer it. */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "connecting");
    cJSON_AddStringToObject(resp, "ssid", args->ssid);
    
    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_sendstr(req, json ? json : "{}");
    cJSON_Delete(resp);
    free(json);

    s_connect_in_progress = true;
    if (xTaskCreate(connect_task, "wifi_connect", 4096, args, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create background connect task");
        s_connect_in_progress = false;
        free(args);
    }

    return send_err;
}


esp_err_t http_server_start(void)
{
    if (s_server != NULL)
    {
        return ESP_OK; /* already running */
    }

    // Initialize config with default (port80 ,etc ) 
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size       = HTTP_SERVER_TASK_STACK_SIZE;  /* cJSON + blocking scan/connect need headroom */
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8; // MAX URI HANDLERS 
    // The core that the HTTP server will run on
    config.core_id = HTTP_SERVER_TASK_CORE_ID;
    // Adjust the default priority to 1 less than the wifi application task
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "HTTP server configure : starting server on port: %d with task priority : %d ", config.server_port, config.task_priority);

    // Creates and starts an HTTP server instance.
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", err);
        return err;
    }

    // These are URI handlers called from WebServer
    static const httpd_uri_t root_uri = {
        .uri = "/", 
        .method = HTTP_GET, 
        .handler = root_get_handler
    };
    static const httpd_uri_t scan_uri = {
        .uri = "/api/scan", 
        .method = HTTP_GET,
         .handler = scan_get_handler
    };
    static const httpd_uri_t status_uri = {
        .uri = "/api/status",
         .method = HTTP_GET, 
         .handler = status_get_handler
    };
    static const httpd_uri_t connect_uri = {
        .uri = "/api/connect",
         .method = HTTP_POST,
          .handler = connect_post_handler
    };

    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &connect_uri);

    ESP_LOGI(TAG, "Web server started — http://%s.local  or  http://<AP-IP>", WIFI_MDNS_HOSTNAME);
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server == NULL)
    {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}
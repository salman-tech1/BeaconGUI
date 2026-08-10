/* wifi_manager.c */
#include <string.h>
#include <stdlib.h> // memset / calloc / free
#include <stdint.h>
#include <arpa/inet.h>  /* for inet_pton */


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h" // network interface driver 
#include "netdb.h"
#include "esp_mac.h"

#include "nvs_flash.h" // this is library to write into the NVS partitions 
#include "nvs.h"      // 
#include "mdns.h"    // http://beacon.local instead of an IP
#include "wifi.h"

#include "system_info.h" // system info 


/* add a static sequence counter at the top of wifi_manager.c */

static const char *TAG = "WIFI";



/* 
Wifi connected and fail Bits for Event group 
 */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

 

static EventGroupHandle_t s_wifi_event_group = NULL; // handle for wifi group event 
static SemaphoreHandle_t  s_wifi_op_mutex     = NULL; // guards scan/connect against each other
static volatile bool      s_connected        = false;
static volatile int       s_retry_count      = 0;
static char               s_ip_str[16]       = "0.0.0.0";
static esp_netif_t       *s_netif_sta        = NULL;
static esp_netif_t       *s_netif_ap        = NULL ; 


static wifi_info_t  wif_info  ; 


// wifi event handler 
static void     wifi_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *event_data);

static esp_err_t load_credentials(char *ssid, size_t ssid_sz,
                                  char *pass, size_t pass_sz);

static esp_err_t start_mdns_service(void);



// id tells which event fired  
// this is called by idf event loop task 
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *event_data)
{
    if (base == WIFI_EVENT)
    {
        switch (id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started — connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *e = event_data;
            ESP_LOGI(TAG, "Associated: SSID=%.*s channel=%u",
                     e->ssid_len, e->ssid, e->channel);
        }
        break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *e = event_data;

if (s_connected)
{   
    s_connected = false;

    memset(&wif_info, 0, sizeof(wif_info));
    wif_info.wifi_rssi      = 0;
    wif_info.wifi_connected = false;

     // tell system_info WiFi dropped — publisher_task's change-detection
    // relies on this to notice a disconnect and push CMD_WIFI_STATUS.

    set_wifi_info(&wif_info);

    if (s_retry_count < WIFI_MAX_RETRY)
    {
        s_retry_count++;
        ESP_LOGW(TAG, "Connection LOST: reason=%u — retry %d/%d",
                 e->reason, s_retry_count, WIFI_MAX_RETRY);
        esp_wifi_connect();
    }
    else
    {
        ESP_LOGE(TAG, "Reconnection failed after %d attempts", WIFI_MAX_RETRY);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
}
        }
        break;

        /* ---------- AP Events ---------- */
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started — SSID: %s", WIFI_AP_SSID);
            break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *e = event_data;
            ESP_LOGI(TAG, "AP station connected — MAC: " MACSTR,
                     MAC2STR(e->mac));
                     
        }
        break;

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *e = event_data;
           ESP_LOGI(TAG, "AP station disconnected — MAC: " MACSTR,
                     MAC2STR(e->mac));
        }
        break;

        default:
            break;
        }
    }
    else if (base == IP_EVENT)
    {
        switch (id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *e = event_data;
            wifi_config_t wifi_cfg = {0};
            wifi_ap_record_t ap_info;
    
            snprintf(s_ip_str, sizeof(s_ip_str), IPSTR,
                     IP2STR(&e->ip_info.ip));
            
            s_connected   = true;
            s_retry_count = 0;
  
        
  memset(&wif_info, 0, sizeof(wif_info));

if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK)
{
    strlcpy(wif_info.wifi_ssid,     (char *)wifi_cfg.sta.ssid,     sizeof(wif_info.wifi_ssid));
    strlcpy(wif_info.wifi_password,  (char *)wifi_cfg.sta.password,  sizeof(wif_info.wifi_password));
}

strlcpy(wif_info.wifi_ip, s_ip_str, sizeof(wif_info.wifi_ip));
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)

    wif_info.wifi_rssi =  ap_info.rssi; 
    wif_info.wifi_connected = s_connected ; 
    
    // set the info to send 
    set_wifi_info(&wif_info) ; 

     ESP_LOGI(TAG, "STA SSID : %s PASSWORD %s Got IP: %s", wifi_cfg.sta.ssid ,wifi_cfg.sta.password,s_ip_str);
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        break;

        case IP_EVENT_ASSIGNED_IP_TO_CLIENT:
        {
            ip_event_assigned_ip_to_client_t *e = event_data;
            ESP_LOGI(TAG, "AP assigned IP to client: " IPSTR,
                     IP2STR(&e->ip));
        }
        break;

        default:
            break;
        }
    }
}

/* 
 * load_credentials
 *
 * Tries to read SSID and password from NVS.
 * If NVS is empty (first boot), copies the compile-time defaults instead.
 *
 * Returns ESP_OK always — if NVS read fails, defaults are used.
 */
static esp_err_t load_credentials(char *ssid, size_t ssid_sz,
                                  char *pass, size_t pass_sz)
{

    nvs_handle_t nvs_handle; // use nvs partition to get the wifi credential from 

    /* Open the NVS namespace in read-only mode.
     * NVS_READONLY means we cannot accidentally write to it here. */
    
    /*
     Namespace: "wif_cfg"
│  ├─ "version" = 1
│  └─ "size" = 1024
    */
     esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err == ESP_OK)
    {
        /* nvs_get_str fills the buffer and sets ssid_sz to actual length.
         * If the key does not exist, err is ESP_ERR_NVS_NOT_FOUND. */
        size_t n = ssid_sz;
        /*
        use the key to get the entry "ssid"

        */
        if (nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &n) != ESP_OK)
        {
            strlcpy(ssid, WIFI_DEFAULT_SSID, ssid_sz);
            ESP_LOGW(TAG, "No SSID in NVS — using default");
        }
        else
        {
            ESP_LOGI(TAG, "Loaded SSID from NVS: %s", ssid);
        }

        n = pass_sz;
        if (nvs_get_str(nvs_handle, NVS_KEY_PASS, pass, &n) != ESP_OK)
        {
            strlcpy(pass, WIFI_DEFAULT_PASS, pass_sz);
            ESP_LOGW(TAG, "No password in NVS — using default");
        }

        nvs_close(nvs_handle);
    }
    else
    {
        /* NVS namespace does not exist yet — use defaults.
         * This happens on first ever boot with a blank flash. */
        ESP_LOGW(TAG, "NVS open failed (0x%x) — using compile-time defaults",
                 err);
        strlcpy(ssid, WIFI_DEFAULT_SSID, ssid_sz);
        strlcpy(pass, WIFI_DEFAULT_PASS, pass_sz);
    }

    return ESP_OK;
}

/*
 * start_mdns_service
 *
 * Makes the device reachable as http://beacon.local instead of an IP.
 * The mDNS component listens on the preconfigured STA/AP/ETH netifs by
 * default, so this covers both "phone joined the ESP's own AP" and
 * "ESP joined the home router" cases with a single call.
 */
static esp_err_t start_mdns_service(void)
{
    // initialize mdns 
    esp_err_t err = mdns_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mdns_init failed: 0x%x", err);
        return err;
    }
    

    // |Set device hostname → `name.local`|
    ESP_ERROR_CHECK(mdns_hostname_set(WIFI_MDNS_HOSTNAME));
   // |Set friendly display name|
    ESP_ERROR_CHECK(mdns_instance_name_set(WIFI_MDNS_INSTANCE_NAME));

    /* Advertise the on-board webserver (port 80) so it also shows up in
     * mDNS/Bonjour browsers, and so http://beacon.local resolves once the
     * webserver is started. */
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));

    ESP_LOGI(TAG, "mDNS ready — reachable at http://%s.local", WIFI_MDNS_HOSTNAME);
    return ESP_OK;
}


/*
What the wifi_init do in order 
+ Create Event group for WIFI (For wifi connected disconnected Bits setting )
+ Create a mutex So that wifi connect and scan do not interfere each other 
+ init netif (network interface ) ( network interface so that the high level is connceted with the lower layer lwip )
+ Create Event Loop the ESP-IDF internal  (Create Event loops so that the wifi connect and IP assigned get called )
+ Create Default netif configuration for Sta as well AP 
+ Initialize the wifi driver with default configuration 
+ register handlers For wifi Connected , ip assigned  or so 
+ STA wifi configuration
+ AP configuration for WIFI 
*/

// nvs flash should be initilize before calling this function 
esp_err_t wifi_init(void)
{
    /* Create the event group — must happen before registering handlers
     * because the handlers call xEventGroupSetBits */
    memset(&wif_info, 0, sizeof(wif_info));

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "wifi_manager_init: Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    /* Guards wifi_scan_networks()/wifi_sta_connect_to() so two HTTP
     * requests (e.g. a scan and a connect) can't step on each other. */
    s_wifi_op_mutex = xSemaphoreCreateMutex();
    if (s_wifi_op_mutex == NULL)
    {
        ESP_LOGE(TAG, "wifi_manager_init: Failed to create op mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialise the underlying TCP/IP stack (lwIP).
     * This must be called once per boot, before any WiFi or socket calls. */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Create the default event loop.
     * This is the system-wide event bus. WiFi events, IP events, etc.
     * all go through here. Our handler is registered below. 
     * This uses the ESP-IDF event library */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create the default WiFi station and ap netif (network interface).
     * This binds the WiFi driver to the lwIP stack. */
    s_netif_sta = esp_netif_create_default_wifi_sta();
    
    if (s_netif_sta == NULL)
    {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_ERR_NO_MEM;
    }

    s_netif_ap  = esp_netif_create_default_wifi_ap() ; 
    if (s_netif_ap == NULL)
    {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_ERR_NO_MEM;
    }


    /* Initialise the WiFi driver with default config.
     * WIFI_INIT_CONFIG_DEFAULT() sets up internal buffers and tasks. */
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    /* Register our event handler for ALL WiFi events.
     * ESP_EVENT_ANY_ID means we get every WIFI_EVENT variant. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    ));

    /* Register our handler for the specific IP event we care about. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL
    ));

 ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_ASSIGNED_IP_TO_CLIENT,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    

                    // WIFI Configuartion 

    /* Load SSID and password from NVS (or fall back to defaults) */
    char ssid[64] = {0};
    char pass[64] = {0};

    // get password and ssid if stored in nvs keys 
    // other wise load the default password 
    load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    /* Build the WiFi station configuration struct */
    wifi_config_t wifi_sta_cfg = {0};

    // copy the ssid and password into the wifi_config struct 
    strlcpy((char *)wifi_sta_cfg.sta.ssid,     ssid, sizeof(wifi_sta_cfg.sta.ssid));
    strlcpy((char *)wifi_sta_cfg.sta.password, pass, sizeof(wifi_sta_cfg.sta.password));

    /* WPA2_PSK is the most common home/office security.
     * Setting threshold here means we refuse to connect to open APs
     * even if our SSID matches — security best practice. */
    wifi_sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    /* pmf_cfg: Protected Management Frames — required for WPA3,
     * capable means we support it but don't require it. */
    wifi_sta_cfg.sta.pmf_cfg.capable  = true;
    wifi_sta_cfg.sta.pmf_cfg.required = false;
    wifi_sta_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH ;  /* Support WPA3 if available */

    //    access point configuration  
    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .ssid_hidden = WIFI_AP_SSID_HIDDEN, // to make SSID Visible
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK , 
            .channel     = WIFI_AP_CHANNEL, 
            .beacon_interval = WIFI_AP_BEACON_INTERVAL, 

        },
    };

    ////////------------------------------------////////////////
     /* ---- Configure AP static IP ---- */
    esp_netif_ip_info_t ip;

    // stop the dhcp server because it's job is to assign IP to 
    // it's connected devices 
    if (esp_netif_dhcps_stop(s_netif_ap) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop dhcp client");
        return WIFI_ERR_FAILED;
    }

    memset(&ip, 0, sizeof(esp_netif_ip_info_t));
    // Set IP address: 192.168.1.100
    // This converts the IP address into Binary
    inet_pton(AF_INET, WIFI_AP_IP, &ip.ip); // assign access point IP
    inet_pton(AF_INET, WIFI_AP_GATEWAY, &ip.gw);
    inet_pton(AF_INET, WIFI_AP_NETMASK, &ip.netmask);

    if (esp_netif_set_ip_info(s_netif_ap, &ip) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set ip info");
        return WIFI_ERR_FAILED;
    }

    // start the DHCP to connect the mobile device
    //  This will assign IP to the clients
    if (esp_netif_dhcps_start(s_netif_ap) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start dhcp server ");
        return WIFI_ERR_FAILED;
    }
////////------------------------------------////////////////



    // we set the mode to access point + STA 
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_AP_BANDWIDTH));
   ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_WIFI_STA_POWER_SAVE));  


    /* Start the WiFi driver.
     * This triggers WIFI_EVENT_STA_START → our handler calls esp_wifi_connect(). */
    ESP_ERROR_CHECK(esp_wifi_start());

    /* AP is now up regardless of whether STA connects — start mDNS here
     * so http://beacon.local resolves immediately for anyone joining the
     * AP, even if the STA leg below times out or fails. */
    start_mdns_service();


    /* Block here until either CONNECTED_BIT or FAIL_BIT is set.
     * pdTRUE, pdFALSE: clear the bits we wait on (CONNECTED), 
     * don't clear bits we don't wait on.
     * timeout = WIFI_CONNECT_TIMEOUT_MS. */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,             /* clear bits on exit        */
        pdFALSE,            /* wait for ANY bit, not ALL */
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT)
    {
          ESP_LOGI(TAG, "STA Connected. IP=%s", s_ip_str);
          return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "STA Connection failed. Check SSID/password.");
        return WIFI_ERR_FAILED;
    }
    else
    {
        /* Neither bit set → timeout elapsed */
        ESP_LOGE(TAG, "Connection timeout after %dms",
                 WIFI_CONNECT_TIMEOUT_MS);
        return WIFI_ERR_TIMEOUT;
    }

    return WIFI_OK ; 
}


bool wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t wifi_wait_connected(uint32_t timeout_ms)
{
    /* If already connected return immediately */
    if (s_connected) return ESP_OK;

    /* Otherwise block on the event group.
     * Note: we must re-set the bit after it was cleared by init's waitBits,
     * so we set it again here if still connected. */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,            /* do NOT clear — other tasks may also be waiting */
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms)
    );

    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// get the ip address assigned by the router 
const char *wifi_get_ip(void)
{
    return s_ip_str;
}

int8_t wifi_get_rssi(void)
{
    if (!s_connected) return 0;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        return ap_info.rssi;

    return 0;
}

esp_err_t wifi_save_creds(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL) return ESP_ERR_INVALID_ARG;
    if (strlen(ssid) == 0)           return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: 0x%x", err);
        return err;
    }

    // Write the ssid and pass to flash 
    nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    nvs_set_str(nvs_handle, NVS_KEY_PASS, pass);

    /* nvs_commit flushes the write to flash.
     * Without this, a power cut before the internal write buffer
     * is flushed would lose the credentials. */
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "Credentials saved: SSID=%s", ssid);
    else
        ESP_LOGE(TAG, "NVS commit failed: 0x%x", err);

    return err;
}

esp_err_t wifi_clear_creds(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "wifi_clear_creds: NVS open failed (0x%x) — nothing to clear", err);
        return err;
    }
 
    nvs_erase_all(nvs_handle);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
 
    ESP_LOGI(TAG, "Stored WiFi credentials cleared — next boot uses compile-time defaults");
    return err;
}


esp_err_t wifi_scan_networks(wifi_ap_record_t *out_records,
                              uint16_t max_records,
                              uint16_t *out_count)
{
    if (out_records == NULL || out_count == NULL || max_records == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* A scan briefly borrows the radio, so serialize against any other
     * scan/connect call already in progress rather than racing it. 
     One operation happens than after this other 
     Wifi connect and scan cannot happens at the same time  */
    if (xSemaphoreTake(s_wifi_op_mutex, pdMS_TO_TICKS(20000)) != pdTRUE)
    {
        return WIFI_ERR_TIMEOUT;
    }

    // This tells esp32 how to scan 
    // Passive : ESP listens
    //Router talks
    // ESP hears it
    /*
    Active
    ESP
    "Anybody here?"
    ↓
    Routers reply immediately.
    Much faster.
    */
    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    /* Blocking scan — typically ~1-3s across all 2.4GHz channels.
     * Fine to call from an HTTP handler task; just don't call it from
     * a task with a tight watchdog/short timeout. */
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_scan_start failed: 0x%x", err);
        xSemaphoreGive(s_wifi_op_mutex);
        return err;
    }

    uint16_t raw_count = 0;
    /*
    Ask the driver
        How many APs did you find?
    */
    esp_wifi_scan_get_ap_num(&raw_count);
    if (raw_count == 0)
    {
        *out_count = 0;
        //release the semaphore we don't have to do with single mutex 
        xSemaphoreGive(s_wifi_op_mutex);
        return ESP_OK;
    }

     // allocate memory for ap's it find 
    wifi_ap_record_t *raw = calloc(raw_count, sizeof(wifi_ap_record_t));
    if (raw == NULL)
    {
        xSemaphoreGive(s_wifi_op_mutex);
        return ESP_ERR_NO_MEM;
    }

    /*
    Now the driver copies all scan results into
    raw[]
    Each record contains information like
    SSID
    RSSI
    Channel
    Authentication
    BSSID

    */
   // max ap records number 
   // raw is the ap records array 
    err = esp_wifi_scan_get_ap_records(&raw_count, raw);
    xSemaphoreGive(s_wifi_op_mutex);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records failed: 0x%x", err);
        free(raw);
        return err;
    }

    /* De-duplicate by SSID (multiple BSSIDs/repeaters broadcast the same
     * SSID) keeping the strongest RSSI, capped at the caller's buffer. */
    uint16_t unique = 0;
    for (uint16_t i = 0; i < raw_count && unique < max_records; i++)
    {
        // look for the NULL Terminator 
        if (raw[i].ssid[0] == '\0')
        {
            continue; /* hidden/blank SSID — nothing useful to show a user */
        }

        bool dup = false;
        for (uint16_t j = 0; j < unique; j++)
        {
            /*
            Duplicate removal
            */
            if (strcmp((char *)out_records[j].ssid, (char *)raw[i].ssid) == 0)
            {
                dup = true;
                if (raw[i].rssi > out_records[j].rssi)
                {
                    out_records[j] = raw[i];
                }
                break;
            }
        }
        if (!dup)
        {
            out_records[unique++] = raw[i];
        }
    }
    free(raw);

    /* Sort strongest-first (insertion sort — `unique` is small). */
    for (uint16_t i = 1; i < unique; i++)
    {
        wifi_ap_record_t key = out_records[i];
        int j = (int)i - 1;
        while (j >= 0 && out_records[j].rssi < key.rssi)
        {
            out_records[j + 1] = out_records[j];
            j--;
        }
        out_records[j + 1] = key;
    }

    // store the count 
    *out_count = unique;
    ESP_LOGI(TAG, "Scan complete: %u unique network(s) found", unique);
    return ESP_OK;
}

esp_err_t wifi_sta_connect_to(const char *ssid, const char *pass, uint32_t timeout_ms)
{
    if (ssid == NULL || strlen(ssid) == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    /*
    typedef struct
{
    struct
    {
        uint8_t ssid[32];
        uint8_t password[64];
    } sta;

} wifi_config_t;
    */
    // get the size of ssid array defined internally 
    if (strlen(ssid) >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
        (pass != NULL && strlen(pass) >= sizeof(((wifi_config_t *)0)->sta.password)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    // only scanning or Connecting must happens at a time 
    if (xSemaphoreTake(s_wifi_op_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        return WIFI_ERR_TIMEOUT;
    }

    /* Persist immediately so the choice survives a reboot even if the
     * connection attempt itself fails (user can retry from the same page
     * without having to re-type the SSID). */
    wifi_save_creds(ssid, pass ? pass : "");


    wifi_config_t sta_cfg = {0};
    // strlcpy instead of strcpy because this is safe 
    strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, pass ? pass : "", sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = (pass != NULL && strlen(pass) > 0)
                                          ? WIFI_AUTH_WPA2_PSK
                                          : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable  = true;
    sta_cfg.sta.pmf_cfg.required = false;
    sta_cfg.sta.sae_pwe_h2e       = WPA3_SAE_PWE_BOTH;

    // Clear a Bits 
    /*
    CONNECTED = 0   
    FAIL = 0
    Everything starts fresh.
    */
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry_count = 0;
    s_connected   = false;

    /* Drop whatever STA link we currently have (if any) before switching —
     * the AP interface is completely untouched by this. */
    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed: 0x%x", err);
        xSemaphoreGive(s_wifi_op_mutex);
        return err;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_connect failed: 0x%x", err);
        xSemaphoreGive(s_wifi_op_mutex);
        return err;
    }

    // wait for connection 
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    xSemaphoreGive(s_wifi_op_mutex);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Switched STA to '%s' — IP=%s", ssid, s_ip_str);
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "Failed to connect to '%s' (bad password / AP not found)", ssid);
        return WIFI_ERR_FAILED;
    }

    ESP_LOGE(TAG, "Timed out connecting to '%s' after %ums", ssid, (unsigned)timeout_ms);
    return WIFI_ERR_TIMEOUT;
}



idf_component_register (
    SRCS  "sntp.c"  
    INCLUDE_DIRS "." ".."
    REQUIRES  driver  "link" "httpServer" "system_info" "wifi" "lwip" "nvs_flash"  "esp_netif"  )


idf_component_register(
    SRCS  "wifi.c"  
    INCLUDE_DIRS "." "../"
    REQUIRES  driver "nvs_flash" "system_info" "esp_netif" "esp_wifi"  "esp_driver_gpio" "esp_event"   
)



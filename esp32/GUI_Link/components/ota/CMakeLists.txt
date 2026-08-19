


idf_component_register (
    SRCS  "ota.c"  
    INCLUDE_DIRS "." ".."
    REQUIRES  driver  esp_http_client esp-tls mbedtls   "lwip" "nvs_flash"  "esp_netif"   "link"       )
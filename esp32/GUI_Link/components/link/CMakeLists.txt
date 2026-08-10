idf_component_register(
    SRCS   "link.c" "frame.c" "com.c"
    INCLUDE_DIRS "." "../"
    REQUIRES  driver  system_info "esp_http_server"  "esp_driver_uart"
)
 
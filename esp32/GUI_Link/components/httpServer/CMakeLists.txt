idf_component_register(
    SRCS   "httpServer.c"
    INCLUDE_DIRS "." "../"
    EMBED_TXTFILES "index.html"
    REQUIRES  driver wifi "esp_http_server" 
)
 
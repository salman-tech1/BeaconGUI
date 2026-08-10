#ifndef http_SERVER_H
#define http_SERVER_H

#include "esp_err.h"


esp_err_t http_server_start(void);

/* Stops the HTTP server, if running. Safe to call even if not started. */
esp_err_t http_server_stop(void);

#endif
#pragma once

#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_connect(void);
void wifi_shutdown(void);


#ifdef __cplusplus
}
#endif

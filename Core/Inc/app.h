#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppMain(void);

// Current RTC time packed in FatFs's timestamp format, for get_fattime().
// Returns 0 (no timestamp) if the RTC isn't available or can't be read.
uint32_t AppGetFatTime(void);

#ifdef __cplusplus
}
#endif

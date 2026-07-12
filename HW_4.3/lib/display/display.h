#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "rtc_ds1307.h"

void display_init(i2c_master_bus_handle_t bus_handle);
void display_show_error(const char *msg);
void display_update(const ds1307_time_t *t, float temp_c, bool colon_on);

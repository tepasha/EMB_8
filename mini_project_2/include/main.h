#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "lights.h"

#define BUTTON_PIN GPIO_NUM_17

typedef enum state_t {
    IDLE,
    HOLDING,
    DEBOUNCE,
    SHORT_PRESS,
    LONG_PRESS
} state_t;

typedef struct button_handler_t{
    esp_timer_handle_t debounce_timer;
    esp_timer_handle_t long_press_timer;
    state_t state;
} button_handler_t;

typedef void (*button_callback_t)(void*);

extern button_handler_t button_handler;
extern state_t state;

button_callback_t get_long_timer_callback();
button_callback_t get_short_timer_callback();
button_callback_t get_button_callback();
state_t get_state();
void reset_state();

#include "pico/stdlib.h"

#include "bdm.h"
#include "pins.h"

void bdm_init()
{
    gpio_init(BDM_HRESET);
    gpio_put(BDM_HRESET, 0);
    gpio_set_dir(BDM_HRESET, GPIO_OUT);
}

// Pulsing HRESET resets the scope
void bdm_pulse_hreset()
{
    gpio_put(BDM_HRESET, 1);
    sleep_ms(20);
    gpio_put(BDM_HRESET, 0);
}


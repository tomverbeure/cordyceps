#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

int main(void)
{
    stdio_usb_init();

    int secs = 0;

    // An endless loop is needed otherwise USB reset won't be serviced.
    while(true){
        printf("%ds\n", secs);
        sleep_ms(1000);
        ++secs;
    }
}


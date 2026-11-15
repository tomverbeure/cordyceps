
# Raspberry Pico USB Reset

In the past, I've always used the BOOTSEL button on the Raspeberry Pico to
put it into its USB mass storage bootloader mode. If you don't have a dongle
connected to the SWCLK/SWDIO pins, this mode is how you flash new firmware.

However, it's often a pain to reach back to that button and it can't be
automated. There's an alternative to get into the mode: there's USB_RESET mode,
which allows you to enter BOOTSEL mode in 2 ways:

* USB_RESET interface

  This adds an extra USB interface that can trigger BOOTSEL mode. `picotool`
  is part of the pico-sdk and knows how to trigger it.

* USB serial 1200 baud mode

  If the USB host configures your USB CDC-ACM device to 1200 baud, it will
  also trigger BOOTSEL mode.

The disadvantage of the first way is that it consumes USB endpoint. When a design
has multiple functions, you may run out of endpoints. The disadvantage of the second
mode is that it's a bit of an ugly hack.

I'll be using the first mode until I run out of USB endpoints.


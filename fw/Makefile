
PICOTOOL = ./build/_deps/picotool/picotool

compile:
	cd build && make -j 8

all: flash

flash: compile
	$(PICOTOOL) load -v -x build/cordyceps.uf2 -f --vid 0

# Only works when plugged into the second USB port of my Mac
terminal:
	picocom --echo --baud 38400  /dev/cu.usbmodem1103

terminal2:
	picocom --echo --baud 38400  /dev/cu.usbmodem1101

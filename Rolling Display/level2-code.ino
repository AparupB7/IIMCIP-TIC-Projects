#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW // try PAROLA_HW / GENERIC_HW /
ICSTATION_HW if garbled
const int numberOfDisplays = 4;
const int pinCS = 18;
const int pinDIN = 19;
const int pinCLK = 21;
// Software SPI constructor: order is DIN, CLK, CS
MD_Parola display = MD_Parola(HARDWARE_TYPE, pinDIN, pinCLK, pinCS,
numberOfDisplays);
void setup() {
display.begin();
display.setIntensity(8); // 0-15
display.displayClear();
display.displayScroll("Hello ESP32-C6!", PA_LEFT, PA_SCROLL_LEFT, 50); // 50 =
speed delay in ms
}
void loop() {
if (display.displayAnimate()) { // returns true when the scroll finishes
display.displayReset(); // restart so it scrolls forever
}
}

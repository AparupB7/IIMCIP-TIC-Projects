#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
// Change this if your module is mirrored/garbled:
// MD_MAX72XX::FC16_HW, GENERIC_HW, PAROLA_HW or ICSTATION_HW
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
const int pinCS = 18;
// CLK - GPIO 21 (For C-6 Board)
// DIN - GPIO 19 (For C-6 Board)
const int pinCLK = 21;
const int pinDIN = 19;
const int numberOfDisplays = 4; // 4 horizontal x 1 vertical
MD_Parola display = MD_Parola(HARDWARE_TYPE, pinCS, numberOfDisplays);
void setup()
{

SPI.begin(pinCLK, -1, pinDIN, pinCS); // SCK, MISO (unused), MOSI, SS
initializeMatrix();
displayStaticText("Hello");
}
void loop()
{
}
void initializeMatrix()
{
display.begin();
display.setIntensity(15); // Brightness between 0 and 15
display.displayClear();
}
void displayStaticText(String text)
{
display.displayClear();
display.setTextAlignment(PA_LEFT);
display.print(text); // Static text is drawn immediately
}

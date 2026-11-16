#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// --- Shared I2C Pin Configuration ---
#define I2C_SDA 4
#define I2C_SCL 5
// --- Display Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 // Not used for I2C screens
// Initialize the I2C OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// --- GPIO Definitions ---
// BUTTON MODULE WIRING: VCC -> 3.3V, GND -> GND, OUT -> GPIO (activehigh)

const int DOT_BUTTON = 18; // moved from GPIO 1 (not available on this
board)
const int DASH_BUTTON = 19; // moved from GPIO 2 (strapping pin caused
phantom input)
// BUZZER WIRING: I/O -> GPIO 3, VCC -> 3.3V or 5V, GND -> GND
const int BUZZER = 3;
// --- Timing Variables (Stopwatches) ---
const unsigned long LETTER_TIMEOUT = 1000; // 1 second pause = New Letter
const unsigned long WORD_TIMEOUT = 3000; // 3 second pause = New Word
const int AUDIO_FEEDBACK_MS = 80; // How long the beep lasts
String morseBuffer = "";
String decodedText = "";
unsigned long lastInputTime = 0;
bool letterPending = false;
bool wordPending = false;
// --- Clear (long-press both buttons) ---
const unsigned long CLEAR_HOLD_TIME = 1500; // hold both buttons this long
to clear
unsigned long bothPressedStart = 0;
bool bothCurrentlyHeld = false;
bool clearTriggered = false;
// Active-HIGH button modules: idle state is LOW, pressed state is HIGH
int lastDotState = LOW;
int lastDashState = LOW;
// --- Translation Dictionary ---
char translateMorse(String morse) {
if (morse == ".-") return 'A'; if (morse == "-...") return 'B';
if (morse == "-.-.") return 'C'; if (morse == "-..") return 'D';
if (morse == ".") return 'E'; if (morse == "..-.") return 'F';
if (morse == "--.") return 'G'; if (morse == "....") return 'H';
if (morse == "..") return 'I'; if (morse == ".---") return 'J';
if (morse == "-.-") return 'K'; if (morse == ".-..") return 'L';
if (morse == "--") return 'M'; if (morse == "-.") return 'N';
if (morse == "---") return 'O'; if (morse == ".--.") return 'P';
if (morse == "--.-") return 'Q'; if (morse == ".-.") return 'R';

if (morse == "...") return 'S'; if (morse == "-") return 'T';
if (morse == "..-") return 'U'; if (morse == "...-") return 'V';
if (morse == ".--") return 'W'; if (morse == "-..-") return 'X';
if (morse == "-.--") return 'Y'; if (morse == "--..") return 'Z';
if (morse == "-----") return '0'; if (morse == ".----") return '1';
if (morse == "..---") return '2'; if (morse == "...--") return '3';
if (morse == "....-") return '4'; if (morse == ".....") return '5';
if (morse == "-....") return '6'; if (morse == "--...") return '7';
if (morse == "---..") return '8'; if (morse == "----.") return '9';
return '?'; // If the pattern doesn't exist
}
// --- Screen Drawing Function ---
void updateDisplay() {
display.clearDisplay();
display.setTextSize(2);
display.setTextColor(SSD1306_WHITE);
display.setCursor(0, 0);
display.print("Buf:");
display.print(morseBuffer); // Show dots and dashes
display.drawFastHLine(0, 24, 128, SSD1306_WHITE);
display.setTextSize(1);
display.setCursor(0, 32);
display.print("Text:");
display.setCursor(0, 45);
display.setTextSize(2);
// Cut off long text so it fits on the screen
if (decodedText.length() > 10) {
display.print(decodedText.substring(decodedText.length() - 10));
} else {
display.print(decodedText);
}
display.display();
}
void triggerBuzzer() {

digitalWrite(BUZZER, HIGH);
delay(AUDIO_FEEDBACK_MS);
digitalWrite(BUZZER, LOW);
}
void setup() {
Serial.begin(115200);
// Setup I2C Pins for the OLED
Wire.begin(I2C_SDA, I2C_SCL);
// Setup the button modules
// No internal pull-up needed - the module has its own onboard resistor
pinMode(DOT_BUTTON, INPUT);
pinMode(DASH_BUTTON, INPUT);
// Setup the buzzer output
pinMode(BUZZER, OUTPUT);
digitalWrite(BUZZER, LOW);
// Start the OLED screen (0x3C is the standard I2C address)
if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
Serial.println("OLED not found! Check wiring.");
for (;;); // Freeze if screen fails
}
display.clearDisplay();
display.setTextSize(1);
display.setTextColor(SSD1306_WHITE);
display.setCursor(10, 20);
display.print("System Ready");
display.display();
delay(1500);
updateDisplay();
lastInputTime = millis();
}
void loop() {
unsigned long currentTime = millis();
// Read the current state of both button modules

int currentDotState = digitalRead(DOT_BUTTON);
int currentDashState = digitalRead(DASH_BUTTON);
// 1. Did the user press the DOT button? (Active HIGH: LOW -> HIGH)
if (currentDotState == HIGH && lastDotState == LOW) {
morseBuffer += ".";
triggerBuzzer();
lastInputTime = currentTime; // Reset stopwatch
letterPending = true;
wordPending = true;
updateDisplay();
delay(150); // Debounce delay to ignore bouncy contacts
}
lastDotState = currentDotState;
// 2. Did the user press the DASH button? (Active HIGH: LOW -> HIGH)
if (currentDashState == HIGH && lastDashState == LOW) {
morseBuffer += "-";
triggerBuzzer();
lastInputTime = currentTime; // Reset stopwatch
letterPending = true;
wordPending = true;
updateDisplay();
delay(250); // Extra-long debounce delay - this module bounces more
than the Dot button
}
lastDashState = currentDashState;
// 2.5 Clear everything: hold BOTH Dot and Dash buttons together
if (currentDotState == HIGH && currentDashState == HIGH) {
if (!bothCurrentlyHeld) {
bothCurrentlyHeld = true;
bothPressedStart = currentTime;
clearTriggered = false;
} else if (!clearTriggered && (currentTime - bothPressedStart >=
CLEAR_HOLD_TIME)) {
morseBuffer = "";
decodedText = "";
letterPending = false;
wordPending = false;
clearTriggered = true;

// Two short beeps to confirm the clear
triggerBuzzer(); delay(80); triggerBuzzer();
Serial.println("Event:TEXT_CLEARED");
updateDisplay();
}
} else {
bothCurrentlyHeld = false;
clearTriggered = false;
}
// 3. Did 1 second pass? Translate the letter!
if (letterPending && (currentTime - lastInputTime >= LETTER_TIMEOUT)) {
char newChar = translateMorse(morseBuffer);
decodedText += newChar;
Serial.print("Decoded_Char:"); Serial.print(newChar);
Serial.print(",");
Serial.print("Full_Text:"); Serial.println(decodedText);
morseBuffer = "";
letterPending = false;
updateDisplay();
}
// 4. Did 3 seconds pass? Add a space!
if (wordPending && !letterPending && (currentTime - lastInputTime >=
WORD_TIMEOUT)) {
decodedText += " ";
wordPending = false;
Serial.print("Event:"); Serial.println("WORD_SPACE_ADDED");
updateDisplay();
}
}

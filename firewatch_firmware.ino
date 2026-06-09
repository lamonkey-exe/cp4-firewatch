/* =====================================================================
 *  FIREWATCH — full firmware (ESP32-S3 Feather)
 *  9 contact pads + 3 NeoPixel strips (8 LEDs each, one per grid row).
 *
 *  Pads (one wire to GPIO, other to shared foil ground -> GND, INPUT_PULLUP):
 *    cells 0 1 2 = A2 A1 A0   (top,  right-to-left wiring)
 *    cells 3 4 5 = A5 A4 A3   (mid,  right-to-left wiring)
 *    cells 6 7 8 =  6  9 10   (bottom, left-to-right)
 *
 *  Strips (data pins): row0=11, row1=12, row2=13. Each strip has 8 LEDs;
 *    we light px0 (left), px3+px4 (middle), px7 (right) per row.
 *    Each strip also needs 5V + GND (data pin carries no power).
 *
 *  Protocol over USB serial @ 115200, newline-delimited JSON:
 *    IN  : {"cmd":"set","cell":4,"r":255,"g":40,"b":0}
 *          {"cmd":"all","r":0,"g":0,"b":0}   (also "flash")
 *    OUT : {"btn":i}  on pad press   ·   {"hello":"firewatch"} on boot
 *
 *  Libraries: Adafruit NeoPixel, ArduinoJson (v7).
 * ===================================================================== */

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

// ---------- Strips: one per row ----------
#define LEDS_PER_STRIP 8
#define BRIGHTNESS     90      // keep modest for USB power

Adafruit_NeoPixel rowStrip[3] = {
  Adafruit_NeoPixel(LEDS_PER_STRIP, 11, NEO_GRB + NEO_KHZ800),  // row 0 (top)
  Adafruit_NeoPixel(LEDS_PER_STRIP, 12, NEO_GRB + NEO_KHZ800),  // row 1 (mid)
  Adafruit_NeoPixel(LEDS_PER_STRIP, 13, NEO_GRB + NEO_KHZ800)   // row 2 (bottom)
};

// For a given column (0=left,1=mid,2=right): which pixels on the strip light.
// "two middle" -> the center pair px3 & px4. Left/right are single pixels.
const int COL_PX[3][2] = {
  { 0, -1 },   // left   -> px0
  { 3,  4 },   // middle -> px3 + px4
  { 7, -1 }    // right  -> px7
};

// ---------- Pads ----------
const int PAD_PIN[9] = {
  A2, A1, A0,   // cells 0,1,2  top
  A5, A4, A3,   // cells 3,4,5  mid
   6,  9, 10    // cells 6,7,8  bottom
};
const unsigned long DEBOUNCE_MS = 40;

int  lastStable[9], lastReading[9];
unsigned long lastChange[9];

// ---------- Serial input ----------
static char lineBuf[160];
static uint8_t lineLen = 0;

// ---------------------------------------------------------------------
void setCell(int cell, uint8_t r, uint8_t g, uint8_t b) {
  if (cell < 0 || cell > 8) return;
  int row = cell / 3;          // 0=top,1=mid,2=bottom
  int col = cell % 3;          // 0=left,1=mid,2=right
  uint32_t c = rowStrip[row].Color(r, g, b);
  for (int k = 0; k < 2; k++) {
    int px = COL_PX[col][k];
    if (px >= 0) rowStrip[row].setPixelColor(px, c);
  }
}

void showAll() { for (int i = 0; i < 3; i++) rowStrip[i].show(); }

void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int c = 0; c < 9; c++) setCell(c, r, g, b);
}

// ---------------------------------------------------------------------
void handleLine(const char* s) {
  StaticJsonDocument<160> doc;
  if (deserializeJson(doc, s)) return;
  const char* cmd = doc["cmd"];
  if (!cmd) return;

  uint8_t r = doc["r"] | 0, g = doc["g"] | 0, b = doc["b"] | 0;

  if (strcmp(cmd, "set") == 0) {
    setCell(doc["cell"] | -1, r, g, b);
    showAll();
  } else if (strcmp(cmd, "all") == 0 || strcmp(cmd, "flash") == 0) {
    setAll(r, g, b);
    showAll();
  }
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 3; i++) {
    rowStrip[i].begin();
    rowStrip[i].setBrightness(BRIGHTNESS);
    rowStrip[i].clear();
    rowStrip[i].show();
  }

  for (int i = 0; i < 9; i++) {
    pinMode(PAD_PIN[i], INPUT_PULLUP);
    lastStable[i]  = HIGH;
    lastReading[i] = HIGH;
    lastChange[i]  = 0;
  }

  delay(50);
  Serial.println("{\"hello\":\"firewatch\"}");
}

// ---------------------------------------------------------------------
void loop() {
  // ---- serial in ----
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) { lineBuf[lineLen] = '\0'; handleLine(lineBuf); lineLen = 0; }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    } else lineLen = 0;
  }

  // ---- pads -> {"btn":i} on press ----
  unsigned long now = millis();
  for (int i = 0; i < 9; i++) {
    int reading = digitalRead(PAD_PIN[i]);
    if (reading != lastReading[i]) { lastChange[i] = now; lastReading[i] = reading; }
    if ((now - lastChange[i]) > DEBOUNCE_MS && reading != lastStable[i]) {
      lastStable[i] = reading;
      if (reading == LOW) {
        Serial.print("{\"btn\":"); Serial.print(i); Serial.println("}");
      }
    }
  }
}

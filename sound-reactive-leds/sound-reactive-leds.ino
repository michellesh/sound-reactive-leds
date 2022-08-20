/*
 * Web enabled FFT VU meter for a matrix, ESP32 and INMP441 digital mic.
 * The matrix width MUST be either 8 or a multiple of 16 but the height can
 * be any value. E.g. 8x8, 16x16, 8x10, 32x9 etc.
 *
 * We are using the LEDMatrx library for easy setup of a matrix with various
 * wiring options. Options are:
 *  HORIZONTAL_ZIGZAG_MATRIX
 *  HORIZONTAL_MATRIX
 *  VERTICAL_ZIGZAG_MATRIX
 *  VERTICAL_MATRIX
 * If your matrix has the first pixel somewhere other than the bottom left
 * (default) then you can reverse the X or Y axis by writing -M_WIDTH and /
 * or -M_HEIGHT in the cLEDMatrix initialisation.
 *
 * REQUIRED LIBRARIES
 * FastLED            Arduino libraries manager
 * ArduinoFFT         Arduino libraries manager
 * EEPROM             Built in
 * LEDMatrix          https://github.com/AaronLiddiment/LEDMatrix
 *
 * WIRING
 * LED data     D2 via 470R resistor
 * GND          GND
 * Vin          5V
 *
 * INMP441
 * VDD          3V3
 * GND          GND
 * L/R          GND
 * WS           D15
 * SCK          D14
 * SD           D32
 *
 * REFERENCES
 * Main code      Scott Marley            https://www.youtube.com/c/ScottMarley
 * Andrew Tuline et al     https://github.com/atuline/WLED
 */

#include "audio_reactive.h"
#include <EEPROM.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>

#include "Timer.h"
#include "colors.h"

#define EEPROM_SIZE 5
#define LED_PIN 2
#define M_WIDTH 8
#define M_HEIGHT 18
#define NUM_LEDS (M_WIDTH * M_HEIGHT)

#define EEPROM_BRIGHTNESS 0
#define EEPROM_GAIN 1
#define EEPROM_SQUELCH 2
#define EEPROM_PATTERN 3
#define EEPROM_DISPLAY_TIME 4

#define BRIGHTNESS_PIN 13
#define GAIN_PIN 12
#define SQUELCH_PIN 26
#define BUTTON_PIN 25

#define DEFAULT_BRIGHTNESS 100
#define DEFAULT_GAIN 30
#define DEFAULT_SQUELCH 9

#define PATTERN_SOUND 0
#define PATTERN_TWINKLE 1
#define PATTERN_PRIDE 2
#define PATTERN_HEARTBEAT 3
#define PATTERN_SOLID 4
#define NUM_PATTERNS 5

uint8_t numBands;
uint8_t barWidth;
uint8_t pattern;
uint8_t brightness;
uint16_t displayTime;
int buttonState = 0;

CRGB knobColor = CRGB::Red;

CRGB leds[NUM_LEDS];

uint8_t peak[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t prevFFTValue[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t barHeights[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Colors and palettes
DEFINE_GRADIENT_PALETTE(purple_gp){0,   0,   212, 255,     // blue
                                   255, 179, 0,   255};    // purple
DEFINE_GRADIENT_PALETTE(outrun_gp){0,   141, 0,   100,     // purple
                                   127, 255, 192, 0,       // yellow
                                   255, 0,   5,   255};    // blue
DEFINE_GRADIENT_PALETTE(greenblue_gp){0,   0, 255, 60,     // green
                                      64,  0, 236, 255,    // cyan
                                      128, 0, 5,   255,    // blue
                                      192, 0, 236, 255,    // cyan
                                      255, 0, 255, 60};    // green
DEFINE_GRADIENT_PALETTE(redyellow_gp){0,   200, 200, 200,  // white
                                      64,  255, 218, 0,    // yellow
                                      128, 231, 0,   0,    // red
                                      192, 255, 218, 0,    // yellow
                                      255, 200, 200, 200}; // white
CRGBPalette16 purplePal = purple_gp;
CRGBPalette16 outrunPal = outrun_gp;
CRGBPalette16 greenbluePal = greenblue_gp;
CRGBPalette16 heatPal = redyellow_gp;
uint8_t colorTimer = 0;

int MAX_BRIGHTNESS = 255;

void setup() {
  // FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS); // LED matrix
  FastLED.addLeds<WS2812B, LED_PIN, RGB>(leds, NUM_LEDS); // fairy lights
  // FastLED.addLeds<WS2813, LED_PIN, GRB>(leds, NUM_LEDS) // WS2815s
  //   .setCorrection(TypicalLEDStrip)
  //   .setDither(MAX_BRIGHTNESS < 255);
  Serial.begin(57600);

  setupAudio();

  if (M_WIDTH == 8)
    numBands = 8;
  else
    numBands = 16;
  barWidth = M_WIDTH / numBands;

  EEPROM.begin(EEPROM_SIZE);

  // It should not normally be possible to set the gain to 255
  // If this has happened, the EEPROM has probably never been written to
  // (new board?) so reset the values to something sane.
  if (EEPROM.read(EEPROM_GAIN) == 255) {
    EEPROM.write(EEPROM_BRIGHTNESS, 50);
    EEPROM.write(EEPROM_GAIN, 0);
    EEPROM.write(EEPROM_SQUELCH, 0);
    EEPROM.write(EEPROM_PATTERN, 0);
    EEPROM.write(EEPROM_DISPLAY_TIME, 10);
    EEPROM.commit();
  }

  /*
    // Read saved values from EEPROM
    FastLED.setBrightness(EEPROM.read(EEPROM_BRIGHTNESS));
    brightness = FastLED.getBrightness();
    Serial.print("brightness: ");
    Serial.println(brightness);
    gain = EEPROM.read(EEPROM_GAIN);
    Serial.print("gain: ");
    Serial.println(gain);
    squelch = EEPROM.read(EEPROM_SQUELCH);
    Serial.print("squelch: ");
    Serial.println(squelch);
    pattern = EEPROM.read(EEPROM_PATTERN);
    displayTime = EEPROM.read(EEPROM_DISPLAY_TIME);
  */
  brightness = DEFAULT_BRIGHTNESS;
  gain = DEFAULT_GAIN;
  squelch = DEFAULT_SQUELCH;
  pattern = 0;

  pinMode(BUTTON_PIN, INPUT);
}

void loop() {
  cycleColorPalette();

  if (pattern == PATTERN_HEARTBEAT) {
    fadeToBlackBy(leds, 100, 10);
  } else {
    FastLED.clear();
  }

  // Read button and potentiometers
  EVERY_N_MILLISECONDS(100) {
    int buttonRead = digitalRead(BUTTON_PIN);
    if (buttonRead == HIGH && buttonState == 0) {
      buttonState = 1;
      pattern = (pattern + 1) % NUM_PATTERNS; // Increment pattern
      Serial.print("pattern: ");
      Serial.println(pattern);
    } else if (buttonRead == LOW && buttonState == 1) {
      buttonState = 0;
    }

    int value = analogRead(SQUELCH_PIN);
    // Serial.print("value: ");
    // Serial.println(value);
    if (pattern == PATTERN_SOUND) {
      squelch = map(value, 4095, 1000, 0, 30);
      // Serial.print("squelch: ");
      // Serial.println(squelch);
    } else {
      int hue = map(value, 4095, 1000, 0, 255);
      // Serial.print("hue: ");
      // Serial.println(hue);
      // knobColor = CHSV(hue, 255, 255);
    }
    brightness = map(analogRead(BRIGHTNESS_PIN), 4095, 0, 0, 255);
    gain = map(analogRead(GAIN_PIN), 4095, 0, 0, 30);
  }

  uint8_t divisor = 1; // If 8 bands, we need to divide things by 2
  if (numBands == 8)
    divisor = 2; // and average each pair of bands together

  for (int i = 0; i < 16; i += divisor) {
    uint8_t fftValue;

    if (numBands == 8)
      fftValue = (fftResult[i] + fftResult[i + 1]) /
                 2; // Average every two bands if numBands = 8
    else
      fftValue = fftResult[i];

    fftValue = ((prevFFTValue[i / divisor] * 3) + fftValue) /
               4; // Dirty rolling average between frames to reduce flicker
    barHeights[i / divisor] = fftValue / (255 / M_HEIGHT); // Scale bar height

    if (barHeights[i / divisor] > peak[i / divisor]) // Move peak up
      peak[i / divisor] = min(M_HEIGHT, (int)barHeights[i / divisor]);

    prevFFTValue[i / divisor] =
        fftValue; // Save prevFFTValue for averaging later
  }

  drawPatterns();

  // Decay peak
  EVERY_N_MILLISECONDS(60) {
    for (uint8_t band = 0; band < numBands; band++)
      if (peak[band] > 0)
        peak[band] -= 1;
  }

  EVERY_N_SECONDS(30) {
    // Save values in EEPROM. Will only be commited if values have changed.
    EEPROM.write(EEPROM_BRIGHTNESS, brightness);
    EEPROM.write(EEPROM_GAIN, gain);
    EEPROM.write(EEPROM_SQUELCH, squelch);
    EEPROM.write(EEPROM_PATTERN, pattern);
    EEPROM.write(EEPROM_DISPLAY_TIME, displayTime);
    EEPROM.commit();
  }

  FastLED.setBrightness(brightness);
  FastLED.show();
  delay(10);
}

void drawPatterns() {

  // barAverage();
  // barSum();
  // barSumRibCage();
  // barSumBike();

  switch (pattern) {
  case PATTERN_SOUND:
    barSumRibCage();
    break;
  case PATTERN_TWINKLE:
    twinkle();
    break;
  case PATTERN_PRIDE:
    pride();
    break;
  case PATTERN_HEARTBEAT:
    heartbeat();
    break;
  default: // PATTERN_SOLID
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = knobColor;
    }
    break;
  }
}

//////////// Patterns ////////////

void rainbowBars(uint8_t band, uint8_t barHeight) {
  // from the beginning of each segment
  // int iStart = M_WIDTH * band;
  // for (int i = iStart; i < iStart + barHeight; i++) {
  //  leds[i] = CHSV(band * (255 / numBands), 255, 255);
  //}

  // from the middle of each segment
  int i = M_WIDTH * band;
  int half = M_WIDTH / 2;
  for (int x = 0; x < barHeight / 2; x++) {
    leds[i + half + x] = CHSV(band * (255 / numBands), 255, 255);
    leds[i + half - 1 - x] = CHSV(band * (255 / numBands), 255, 255);
  }
}

void barAverage() {
  int sum = 0;
  for (int band = 0; band < numBands; band++) {
    uint8_t barHeight = barHeights[band];
    sum += barHeight;
  }
  int averageHeight = sum / numBands;
  int height = map(averageHeight, 0, M_HEIGHT, 0, NUM_LEDS / 4);

  int middle = NUM_LEDS / 2;
  for (int x = 0; x < height; x++) {
    leds[x] = CHSV(x * (255 / numBands), 255, 255);
    leds[middle + x] = CHSV(x * (255 / numBands), 255, 255);
    leds[middle - 1 - x] = CHSV(x * (255 / numBands), 255, 255);
    leds[NUM_LEDS - 1 - x] = CHSV(x * (255 / numBands), 255, 255);
  }
}

void barSum() {
  int sum = 0;
  for (int band = 0; band < numBands; band++) {
    uint8_t barHeight = barHeights[band];
    sum += barHeight;
  }
  int height = map(sum, 0, NUM_LEDS, 0, NUM_LEDS / 2);

  int middle = NUM_LEDS / 2;
  for (int x = 0; x < height; x++) {
    int hue = map(x, 0, NUM_LEDS / 4, 0, 255);
    leds[x] = CHSV(hue, 255, 255);
    leds[middle + x] = CHSV(hue, 255, 255);
    leds[middle - 1 - x] = CHSV(hue, 255, 255);
    leds[NUM_LEDS - 1 - x] = CHSV(hue, 255, 255);
  }
}

void barSumRibCage() {
  int numStrands = 4;
  int numLEDs = 100;
  int offset = 2;

  int sum = 0;
  for (int band = 0; band < numBands; band++) {
    uint8_t barHeight = barHeights[band];
    sum += barHeight;
  }

  int height = map(sum, 0, NUM_LEDS, 0, numLEDs);
  for (int s = 0; s < numStrands; s++) {
    for (int x = 0; x < height; x++) {
      int index = x + (s * 25) + offset;
      if (index < numLEDs) {
        int brightness = map(x, 0, numLEDs, 0, 255);
        CRGB color = ColorFromPalette(currentPalette, brightness);
        leds[index] = color.nscale8(255 - brightness);
      }
    }
  }
}

/*
void barSumRibCage() {
  int numLEDs = 100;
  int offset = 2;
  int numStrands = 4;
  //int iStart[] = {2, 27, 52, 77};
  int strandLength = 25;

  int sum = 0;
  for (int band = 0; band < numBands; band++) {
    uint8_t barHeight = barHeights[band];
    sum += barHeight;
  }
  int height = map(sum, 0, NUM_LEDS, 0, numLEDs);

  //for (int s = 0; s < numStrands; s++) {
    for (int i = 0; i < height; i++) {
      int hue = map(i, 0, numLEDs, 0, 255);
      leds[i + 1] = CRGB(hue, 0, 0);
      leds[i + 26] = CRGB(hue, 0, 0);
      leds[i + 51] = CRGB(hue, 0, 0);
      leds[i + 75] = CRGB(hue, 0, 0);

      //int index = i + (s * strandLength) + offset;
      //if (index < numLEDs) {
      //  //leds[index] =  ColorFromPalette(currentPalette, hue);
      //  leds[index] = CRGB(hue, 0, 0);
      //}
      //leds[iStart[s] + i + 25 + offset] = ColorFromPalette(currentPalette,
hue);
      //leds[iStart[s] + i + 50 + offset] = ColorFromPalette(currentPalette,
hue);
      //leds[iStart[s] + i + 75 + offset] = ColorFromPalette(currentPalette,
hue);
    }
  //}
}
*/

void barSumBike() {
  int numLeds = 145;
  int numSegments = 5;
  int iStart[] = {0, 31, 62, 100, 124};
  int segmentLength[] = {31, 31, 38, 24, 21};
  bool reverse[] = {0, 1, 1, 0, 1};

  int sum = 0;
  for (int band = 0; band < numBands; band++) {
    uint8_t barHeight = barHeights[band];
    sum += barHeight;
  }

  for (int s = 0; s < numSegments; s++) {
    int height = map(sum, 0, NUM_LEDS, 0, segmentLength[s]);
    for (int x = 0; x < height; x++) {
      int hue = map(x, 0, segmentLength[s], 0, 255);
      if (reverse[s]) {
        leds[iStart[s] + segmentLength[s] - 1 - x] = CHSV(hue, 255, 255);
      } else {
        leds[iStart[s] + x] = CHSV(hue, 255, 255);
      }
    }
  }
}

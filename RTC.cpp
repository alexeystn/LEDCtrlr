#include <Arduino.h>
#include <FastLED.h>
#include <EEPROM.h>
#include <RTClib.h>

#define LED_STRIP_PIN     3
#define COLOR_ORDER       "GRB"
#define NUM_LEDS          8*40

#define KEY_MENU_PIN      24
#define KEY_MINUS_PIN     27
#define KEY_PLUS_PIN      26
#define MODE_PIN          A0

enum key_t {
  KEY_MENU = 0,
  KEY_MINUS,
  KEY_PLUS
};

enum menu_time_t {
  MENU_SECONDS = 0,
  MENU_MINUTES,
  MENU_HOURS
};

enum menu_disp_t {
  MENU_COLOR = 0,
  MENU_BRIGHTNESS, 
  MENU_ORIENTATION
};

enum menu_pos_t {
  MENU_POS_DISPLAY = 0,
  MENU_POS_TIME
};

#define EEPROM_ADDRESS    0

extern CRGB crgbLedsArr[];

uint8_t digits[6];
uint8_t digitPositions[] = {0, 6, 14, 20, 28, 34};

CRGB colors[] = {CRGB::Red, CRGB::OrangeRed, CRGB::Orange, CRGB::Green, 
                 CRGB::DarkCyan, CRGB::Blue, CRGB::DeepPink, CRGB::Gray};

uint8_t brightnessLevels[] = {1, 2, 4, 8, 16, 32};

#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))
#define NUM_LEVELS (sizeof(brightnessLevels) / sizeof(brightnessLevels[0]))

uint8_t configLevel = 0;
uint8_t configColor = 0;
uint8_t configRotated = 0;

uint32_t lastRefreshTime = 0;
uint32_t lastKeyPressedMs[3];
uint32_t keyPreviousState[3];
uint8_t keyHold[3];
uint8_t keyPin[3] = {KEY_MENU_PIN, KEY_MINUS_PIN, KEY_PLUS_PIN};

uint8_t configNotSaved = 0;
uint32_t configLastChangeMs = 0;

uint8_t menuPosition = 0;
uint8_t subMenuPosition = 0;

uint8_t newHour, newMinute, newSecond;

RTC_DS3231 rtc;

DateTime now, before;

uint8_t font[10][5] = {
  {0x7E, 0x81, 0x81, 0x81, 0x7E}, 
  {0x00, 0x41, 0xFF, 0x01, 0x00}, 
  {0x41, 0x83, 0x85, 0x89, 0x71}, 
  {0x42, 0x81, 0x91, 0x91, 0x6E}, 
  {0x0C, 0x14, 0x24, 0x44, 0xFF}, 
  {0xF2, 0x91, 0x91, 0x91, 0x8E}, 
  {0x3E, 0x51, 0x91, 0x91, 0x0E}, 
  {0x80, 0x83, 0x8C, 0xB0, 0xC0}, 
  {0x6E, 0x91, 0x91, 0x91, 0x6E}, 
  {0x70, 0x89, 0x89, 0x8A, 0x7C},
};

void configLoad(void) {
  uint8_t b = 0;
  b = EEPROM.read(EEPROM_ADDRESS);
  configColor = (b & 0xF0) >> 4;
  configLevel = (b & 0x0F) >> 1;
  configRotated = (b & 0x01);
  if (configColor >= NUM_COLORS) {
    configColor = 0;
  }
  if (configLevel >= NUM_LEVELS) {
    configLevel = 0;
  }  
}

void configSave(void) {
  uint8_t b = 0;
  b = (configColor << 4) + (configLevel << 1) + configRotated;
  EEPROM.write(EEPROM_ADDRESS, b);
}

bool setupRTC() {
  pinMode(MODE_PIN, INPUT_PULLUP);  
  configLoad();
  delay(100);
  if (digitalRead(MODE_PIN) == 0) {
    return false;
  }
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  } else {
    Serial.println("RTC found");
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    rtc.adjust(DateTime(2024, 1, 1, 0, 0, 0));
  }
  FastLED.addLeds<WS2811, LED_STRIP_PIN, GRB>(crgbLedsArr, NUM_LEDS);
  FastLED.setBrightness(brightnessLevels[configLevel]);
  FastLED.showColor(CRGB::Black);
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(keyPin[i], INPUT);
    digitalWrite(keyPin[i], HIGH);
    keyPreviousState[i] = 1;
  }
  return true;
}


void putDigit(uint8_t digit, uint8_t pos, uint8_t highlighted) {
  uint16_t n;
  uint8_t color = configColor;
  if (highlighted) {
    color = (color + NUM_COLORS / 2) % NUM_COLORS;
  }
  
  for (int x = 0; x < 5; x++) {
    uint8_t b = font[digit][x];
    for (int y = 0; y < 8; y++) {
      uint8_t xx = pos + x;
      if (b & 0x01) {
        if (( !configRotated && (xx % 2 == 1)) || ( configRotated && (xx % 2 == 0)))  {
          n = xx*8 + y;
        } else {
          n = xx*8 + 7 - y;
        }
        if (!configRotated) {
          crgbLedsArr[n] = colors[color];
        } else {
          crgbLedsArr[NUM_LEDS - 1 - n] = colors[color];
        }
      }
      b /= 2; 
    }
  }
}


void displayRtc(void) {
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    crgbLedsArr[i] = CRGB::Black;
  }
  FastLED.setBrightness(brightnessLevels[configLevel]);

  if (menuPosition == 0) {
    putDigit(now.hour()/10, digitPositions[0], 0);
    putDigit(now.hour()%10, digitPositions[1], 0);
    putDigit(now.minute()/10, digitPositions[2], 0);
    putDigit(now.minute()%10, digitPositions[3], 0);
    putDigit(now.second()/10, digitPositions[4], 0);
    putDigit(now.second()%10, digitPositions[5], 0);
  } else {
    putDigit(newHour/10, digitPositions[0], subMenuPosition == MENU_HOURS);
    putDigit(newHour%10, digitPositions[1], subMenuPosition == MENU_HOURS);
    putDigit(newMinute/10, digitPositions[2], subMenuPosition == MENU_MINUTES);
    putDigit(newMinute%10, digitPositions[3], subMenuPosition == MENU_MINUTES);
    putDigit(newSecond/10, digitPositions[4], subMenuPosition == MENU_SECONDS);
    putDigit(newSecond%10, digitPositions[5], subMenuPosition == MENU_SECONDS);
  }
  FastLED.show();
}

void adjustColor(enum key_t k) {
  if (k == KEY_PLUS) {
    configColor++;
    if (configColor == NUM_COLORS) {
      configColor = 0;
    }    
  }
  if (k == KEY_MINUS) {
    if (configColor > 0) {
      configColor--;
    } else {
      configColor = NUM_COLORS - 1;
    }    
  }
}

void adjustBrightness(enum key_t k) {
  if (k == KEY_PLUS) {
    if (configLevel < NUM_LEVELS-1)
    configLevel++;  
  }
  if (k == KEY_MINUS) {
    if (configLevel > 0) {
      configLevel--;
    }
  }
}

void adjustTime(uint8_t *var, uint8_t limit, enum key_t k) {
  if (k == KEY_MINUS) {
    if (*var == 0) {
      *var = limit - 1;
    } else {
      (*var)--;
    }
  }
  if (k == KEY_PLUS) {
    (*var)++;
    if (*var == limit) {
      *var = 0;
    }
  }
}

void doKeyPressAction(enum key_t k) {
  if (k == KEY_MENU) {
    subMenuPosition++;
    if (subMenuPosition >= 3) {
      subMenuPosition = 0;
    }
  } else { // PLUS / MINUS
    if (menuPosition == MENU_POS_DISPLAY) {
      switch (subMenuPosition) {
        case MENU_COLOR:
          adjustColor(k);
          break;
        case MENU_BRIGHTNESS:
          adjustBrightness(k);
          break;
        case MENU_ORIENTATION:
          configRotated ^= 1; 
      } 
      configNotSaved = 1;
      configLastChangeMs = millis();
    } else {
      switch (subMenuPosition) {
        case MENU_SECONDS:
          adjustTime(&newSecond, 60, k);
          break;
        case MENU_MINUTES:
          adjustTime(&newMinute, 60, k);
          break;
        case MENU_HOURS:
          adjustTime(&newHour, 24, k);
          break;
      }
    }
  }
  displayRtc();
}

void doKeyHoldAction(enum key_t k) {
  if (k == KEY_MENU) {
    if (menuPosition == MENU_POS_DISPLAY) {
      menuPosition = MENU_POS_TIME;
      newHour = now.hour();
      newMinute = now.minute();
      newSecond = now.second();
      subMenuPosition = MENU_SECONDS;
    } else {
      menuPosition = MENU_POS_DISPLAY;
      rtc.adjust(DateTime(2024, 1, 1, newHour, newMinute, newSecond)); 
    }      
  }
}

void processKeys() {  
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t keyCurrentState = digitalRead(keyPin[i]);
    if ((keyCurrentState == 0) && (keyPreviousState[i] == 1)) {
      if (millis() - lastKeyPressedMs[i] > 200) {
        doKeyPressAction(i);
        keyHold[i] = 1;
        lastKeyPressedMs[i] = millis();
        Serial.print("Press ");
        Serial.println(i);
      }
    }
    if ((keyCurrentState == 0) && (keyHold[i])) {
      if (millis() - lastKeyPressedMs[i] > 1000) {
        lastKeyPressedMs[i] = millis();
        Serial.print("Hold ");
        Serial.println(i);
        doKeyHoldAction(i);
        keyHold[i] = 0;
      }
    }
    if (keyCurrentState == 1) {
      keyHold[i] = 0;
      
    }
    keyPreviousState[i] = keyCurrentState;
  }
}

void processEeprom(void) {
  if (configNotSaved) {
    if (millis() > (configLastChangeMs + 2000)) {
      configSave();
      for (uint8_t i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, 1);
        delay(100);
        digitalWrite(LED_BUILTIN, 0);
        delay(100); 
      }
      configNotSaved = 0;
    } 
  }
}

void loopRTC() {
  now = rtc.now();
  if (now != before) {
    displayRtc();
    lastRefreshTime = millis();
  }
  before = now;  
  processKeys();
  processEeprom();
  delay(10);
}

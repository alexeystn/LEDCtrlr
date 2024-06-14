// LED Controller
//
// Serial commands:
//    Configure:        C NUMLEDS [CLRORDER]
//    Describe:         D
//    Max Num LEDs:     M
//    Set Pixel:        P LEDIDX COLOR
//    Read Pixel:       R LEDIDX
//    Fill All Pixels:  F COLOR
//    Send All Pixels:  A
//    Show:             S [COLOR]
//    Brightness:       B LEVEL
//    Serial Echo:      E 0 | 1 | off | on
//    Last Error:       L
//    Show Version:     V
//
// COLOR is RRGGBB value or single-letter specifier, see 'stringToCRGB()' function
// CLRORDER is three-letter string (default "GRB"), see 'initializeLedLib()' function
//
// For interactive operation via a terminal, enter "E1' to enable serial echo
//

#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <FastLED.h>

#define VERSION_STR "LED Controller v1.0"

#define STRIP_LED_PIN 3          // specifies the pin to connect the LED strip to on the Arduino (3 = D3)
#define SERIAL_BAUD_RATE 115200  // specifies the baud rate for serial communications

#define DEF_NUM_LEDS 8           // default number of LEDs in strip
#define DEF_COLOR_ORDER "GRB"    // default color order for LED strip
#define DEF_BRIGHTNESS  20       // default brightness value
#define STRIP_LED_TYPE WS2811    // specifies the type of LED strip
#define MAX_NUM_LEDS 320         // maximum number of LEDs supported on strip

#define MODULE_LED_PIN LED_BUILTIN  // status LED on Arduino module
#define MODULE_LED_ONSTATE HIGH
#define MODULE_LED_OFFSTATE LOW

CRGB crgbLedsArr[MAX_NUM_LEDS];

#define SERIAL_BUFFER_SIZE 20
byte serialBuffer[SERIAL_BUFFER_SIZE];
byte commandBuffer[SERIAL_BUFFER_SIZE];
int serialBufferIndex = 0;
int commandBufferSize = 0;
char *lastErrorStr = "";
const char *lnFeedStr = "\r\n";
const char *echoPromptStr = "\r\n> ";
const char promptChar = '>';
const char errorChar = 'E';
const char msgRecvdChar = '.';
bool serialEchoFlag = false;
bool serialCmdWaitingFlag = false;
bool receivingArrayFlag = false;
int receivingArrayIdx = 0;
uint32_t recvArrStartTimeMs = 0;

bool ledLibInitializedFlag = false;
int stripNumberLeds = DEF_NUM_LEDS;
byte stripBrightness = DEF_BRIGHTNESS;
char stripColorOrderStr[5] = DEF_COLOR_ORDER;

void initializeLedLib();
void processCommand(const char *procBuffer);
void writeMsgToSerial(const char *msgData);
void writeCharToSerial(char ch);
void copySerialMsgBuffer();
void setModuleLed(bool onFlag);

bool rtcModeEnabled = false;
extern bool setupRTC();
extern void loopRTC();

void setup()
{
    setModuleLed(true);
    Serial.begin(SERIAL_BAUD_RATE);  // Start serial interface

    while (!Serial) {};  // Wait for the Serial port to initialize

    writeMsgToSerial(VERSION_STR);
    if (serialEchoFlag)
        writeMsgToSerial(echoPromptStr);
    else
        writeCharToSerial(promptChar);

    delay(500);
    setModuleLed(false);

    if (setupRTC()) {
      rtcModeEnabled = true;
    }
}

void loop()
{
    if (rtcModeEnabled) {
      loopRTC();
      return;
    }
  
    static int cmdRecvDivVal = -1;

    uint32_t curTimeMs = millis();
    int curTimeDiv = (int)(curTimeMs / 100 % 40);
    if (commandBufferSize > 0)
    {
        setModuleLed(true);
        processCommand((const char *)commandBuffer);
        commandBufferSize = 0;
        if (serialEchoFlag)
            writeMsgToSerial(echoPromptStr);
        else
            writeCharToSerial(promptChar);
        cmdRecvDivVal = (curTimeDiv + 2) % 40;
        if (serialCmdWaitingFlag && serialBufferIndex > 0)
            copySerialMsgBuffer();
    }
    else
    {
        if (cmdRecvDivVal < 0)
        {
            if (curTimeDiv >= 39)
                setModuleLed(true);
            else if (curTimeDiv == 0)
                setModuleLed(false);
        }
        else
        {
            if (curTimeDiv == cmdRecvDivVal)
            {
                setModuleLed(false);
                cmdRecvDivVal = -1;
            }
        }
    }
}

void initializeLedLib()
{
    if (strcasecmp(stripColorOrderStr, "GRB") == 0)
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, GRB>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    else if (strcasecmp(stripColorOrderStr, "RGB") == 0)
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, RGB>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    else if (strcasecmp(stripColorOrderStr, "RBG") == 0)
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, RBG>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    else if (strcasecmp(stripColorOrderStr, "GBR") == 0)
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, GBR>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    else if (strcasecmp(stripColorOrderStr, "BRG") == 0)
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, BRG>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    else if (strcasecmp(stripColorOrderStr, "BGR") == 0)
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, BGR>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    else
        FastLED.addLeds<STRIP_LED_TYPE, STRIP_LED_PIN, GRB>(crgbLedsArr, stripNumberLeds).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(stripBrightness);
    ledLibInitializedFlag = true;
}

void sendErrorResponse(const char *msgData)
{
    lastErrorStr = msgData;
    if (serialEchoFlag)
    {
        writeMsgToSerial(lnFeedStr);
        writeMsgToSerial(msgData);
    }
    else
        writeCharToSerial(errorChar);
}

void queryLastErrorMsg()
{
    if (serialEchoFlag)
        writeMsgToSerial(lnFeedStr);
    writeMsgToSerial(lastErrorStr);
}

int findSecondParam(const char *str, const int & strLen)
{
    int p = 0;
    while (str[p] != ' ' && str[p] != ',' && ++p < strLen);  // scan through first param
    while (p < strLen && (str[p] == ' ' || str[p] == ','))   // scan through separator
        ++p;
    return p;
}

// C NUMLEDS CLRORDER
void configureLeds(const char *paramsStr)
{
    int strLen = strlen(paramsStr);
    if (strLen > 0)
    {
        int numVal = atoi(paramsStr);
        if (numVal > 0 && numVal <= MAX_NUM_LEDS)
        {
            stripNumberLeds = numVal;
            int p = findSecondParam(paramsStr, strLen);
            if (p > 0 && p < strLen)
            {
                strncpy(stripColorOrderStr, &paramsStr[p], 4);
                stripColorOrderStr[4] = '\0';
            }
            initializeLedLib();
        }
        else
            sendErrorResponse("LED count value out of range");
    }
}

void describeLeds()
{
    char outBuff[14];
    if (serialEchoFlag)
        writeMsgToSerial(lnFeedStr);
    writeMsgToSerial("NumLEDs=");
    writeMsgToSerial(itoa(stripNumberLeds, outBuff, 10));
    writeMsgToSerial(" ClrOrder=");
    writeMsgToSerial(stripColorOrderStr);
    writeMsgToSerial(" LEDPin=");
    writeMsgToSerial(itoa(STRIP_LED_PIN, outBuff, 10));
    writeMsgToSerial(" Bright=");
    writeMsgToSerial(itoa(stripBrightness, outBuff, 10));
}

void queryMaxNumLeds()
{
    char outBuff[8];
    if (serialEchoFlag)
        writeMsgToSerial(lnFeedStr);
    writeMsgToSerial(itoa(MAX_NUM_LEDS, outBuff, 10));
}

void setBrightness(const char *paramsStr)
{
    if (strlen(paramsStr) > 0)
    {
        stripBrightness = (byte)atoi(paramsStr);
        if (ledLibInitializedFlag)
        {
            FastLED.setBrightness(stripBrightness);
            FastLED.show();
        }
    }
    else
    {
        char outBuff[8];
        if (serialEchoFlag)
            writeMsgToSerial(lnFeedStr);
        writeMsgToSerial(itoa(stripBrightness, outBuff, 10));
    }
}

CRGB stringToCRGB(const char *str, int strLen)
{
    if (strLen > 1)
        return CRGB(strtol((const char *)str, NULL, 16) & 0x00FFFFFF);
    char clrChar = str[0];
    if (clrChar >= 'a' && clrChar <= 'z')
        clrChar -= (char)('a' - 'A');  // to uppercase
    switch (clrChar)
    {
        case 'R':
            return CRGB(CRGB::Red);
        case 'G':
            return CRGB(CRGB::Green);
        case 'B':
            return CRGB(CRGB::Blue);
        case 'Y':
            return CRGB(CRGB::Yellow);
        case 'W':
            return CRGB(CRGB::White);
        case 'O':
            return CRGB(CRGB::Orange);
        case 'V':
            return CRGB(CRGB::Violet);
        case 'P':
            return CRGB(CRGB::Pink);
        case 'M':
            return CRGB(CRGB::Magenta);
        case 'C':
            return CRGB(CRGB::Cyan);
        case 'T':
            return CRGB(CRGB::Teal);
        case 'A':
            return CRGB(CRGB::Gray);
        case 'N':
            return CRGB(CRGB::Brown);
        default:
            return CRGB(CRGB::Black);
    }
}

void setPixelColor(const char *paramsStr)
{
    int strLen = strlen(paramsStr);
    if (strLen > 0)
    {
        int ledIdx = atoi(paramsStr);
        if (ledIdx >= 0 && ledIdx < stripNumberLeds)
        {
            int p = findSecondParam(paramsStr, strLen);
            if (p > 0 && p < strLen)
            {
                CRGB crgbVal = stringToCRGB(&paramsStr[p], strLen-p);
                if (crgbVal != CRGB::Black || paramsStr[p] == '0')
                    crgbLedsArr[ledIdx] = crgbVal;
                else
                    sendErrorResponse("Invalid pixel color value");
            }
            else
                sendErrorResponse("Pixel color param not found");
        }
        else
            sendErrorResponse("LED index value out of range");
    }
}

void queryPixelColor(const char *paramsStr)
{
    int strLen = strlen(paramsStr);
    if (strLen > 0)
    {
        int ledIdx = atoi(paramsStr);
        if (ledIdx >= 0 && ledIdx < stripNumberLeds)
        {
            char outBuff[8];
            sprintf(outBuff,"%06lX", (uint32_t)(crgbLedsArr[ledIdx]) & 0x00FFFFFF );
            if (serialEchoFlag)
                writeMsgToSerial(lnFeedStr);
            writeMsgToSerial(outBuff);
        }
        else
            sendErrorResponse("LED index value out of range");
    }
}

void fillStripColor(const char *paramsStr)
{
    int strLen = strlen(paramsStr);
    if (strLen > 0)
    {
        CRGB crgbVal = stringToCRGB(paramsStr, strLen);
        if (crgbVal != CRGB::Black || paramsStr[0] == '0')
        {
            for (int idx=0; idx<stripNumberLeds; ++idx)
                crgbLedsArr[idx] = crgbVal;
        }
        else
            sendErrorResponse("Invalid fill color value");
    }
    else
        sendErrorResponse("Fill color param not found");
}

void showStripColor(const char *paramsStr)
{
    int strLen = strlen(paramsStr);
    if (strLen > 0)
    {
        CRGB crgbVal = stringToCRGB(paramsStr, strLen);
        if (crgbVal != CRGB::Black || paramsStr[0] == '0')
        {
            if (!ledLibInitializedFlag)
                initializeLedLib();
            FastLED.showColor(crgbVal);
        }
        else
            sendErrorResponse("Invalid pixel color value");
    }
    else
    {
        if (!ledLibInitializedFlag)
            initializeLedLib();
        FastLED.show();
    }
}

void setSerialEcho(const char *paramsStr)
{
    if (strlen(paramsStr) > 0)
    {
        if (paramsStr[0] == '0')
            serialEchoFlag = false;
        else if (paramsStr[0] == '1')
            serialEchoFlag = true;
        else if (strcasecmp(paramsStr, "off") == 0)
            serialEchoFlag = false;
        else if (strcasecmp(paramsStr, "on") == 0)
            serialEchoFlag = true;
        else
          sendErrorResponse("Invalid parameter");
    }
    else
    {
        if (serialEchoFlag)
            writeMsgToSerial(lnFeedStr);
        writeCharToSerial(serialEchoFlag ? '1' : '0');
    }
}

void processCommand(const char *procBuffer)
{
    int procBufSize = strlen(procBuffer);
    char cmdByte = procBuffer[0];
    if (cmdByte >= 'a' && cmdByte <= 'z')
        cmdByte -= (char)('a' - 'A');  // to uppercase
    int p = 1;
    while (p < procBufSize && procBuffer[p] == ' ')
        ++p;
    char *paramsStr = &procBuffer[p];
    switch(cmdByte)
    {
        case 'C':
            configureLeds(paramsStr);
            break;
        case 'D':
            describeLeds();
            break;
        case 'M':
            queryMaxNumLeds();
            break;
        case 'P':
            setPixelColor(paramsStr);
            break;
        case 'R':
            queryPixelColor(paramsStr);
            break;
        case 'F':
            fillStripColor(paramsStr);
            break;
        case 'A':  // receive all pixel RGB values as a stream of bytes
            receivingArrayIdx = 0;
            recvArrStartTimeMs = millis();
            receivingArrayFlag = true;
            break;
        case 'S':
            showStripColor(paramsStr);
            break;
        case 'B':
            setBrightness(paramsStr);
            break;
        case 'E':
            setSerialEcho(paramsStr);
            break;
        case 'L':
            queryLastErrorMsg();
            break;
        case 'V':
            if (serialEchoFlag)
                writeMsgToSerial(lnFeedStr);
            writeMsgToSerial(VERSION_STR);
            break;
        default:
            sendErrorResponse("Unrecognized command");
    }
}

void writeMsgToSerial(const char *msgData)
{
    int msgLen = strlen(msgData);
    Serial.write((byte *)msgData, msgLen);
}

void writeCharToSerial(char ch)
{
    Serial.write(ch);
}

void serialEvent()
{
    static int recvArrOrd = 0;
    static CRGB recvCRGBVal;

    int iterCount = 0;

    if (receivingArrayFlag && millis() > recvArrStartTimeMs + 5000)
    {  // too much time elapsed during receive of binary RGB-array data
        receivingArrayFlag = false;
        receivingArrayIdx = 0;
        recvArrOrd = 0;
        sendErrorResponse("Timeout during receive of array data");
    }
    while (Serial.available())
    {
        byte nextByte = Serial.read();
        if (receivingArrayFlag)
        {  // receiving binary RGB-array data
            recvCRGBVal.raw[recvArrOrd++] = nextByte;
            if (recvArrOrd >= 3)
            {
                recvArrOrd = 0;
                crgbLedsArr[receivingArrayIdx++] = recvCRGBVal;
                if (receivingArrayIdx >= stripNumberLeds)
                {
                    receivingArrayFlag = false;
                    receivingArrayIdx = 0;
                    if (serialEchoFlag)
                        writeMsgToSerial(echoPromptStr);
                    else
                        writeCharToSerial(promptChar);
                }
            }
        }
        else
        {
            if (!serialCmdWaitingFlag)
            {
                if (nextByte != (byte)0x0D)
                {
                    if (nextByte >= (byte)' ' && serialBufferIndex < SERIAL_BUFFER_SIZE-2)
                    {
                        if (serialEchoFlag)
                            Serial.write(nextByte);
                        serialBuffer[serialBufferIndex++] = nextByte;
                    }
                }
                else
                {
                    if (serialBufferIndex > 0)
                    {
                        if (commandBufferSize <= 0)
                            copySerialMsgBuffer();
                        else
                            serialCmdWaitingFlag = true;
                    }
                    else if (commandBufferSize <= 0)
                    {
                        if (serialEchoFlag)
                            writeMsgToSerial(echoPromptStr);
                        else
                            writeCharToSerial(promptChar);
                    }
                }
            }
            if (!receivingArrayFlag && ++iterCount > 20)  // if lots of data is coming in then the serial events can prevent the
                return;                                   //   'loop()' fn from running, so return if too many iterations in a row
        }
    }
}

void copySerialMsgBuffer()
{
    memcpy(commandBuffer, serialBuffer, serialBufferIndex);
    commandBufferSize = serialBufferIndex;
    commandBuffer[commandBufferSize] = '\0';
    serialBufferIndex = 0;
    writeCharToSerial(msgRecvdChar);
    serialCmdWaitingFlag = false;
}

void setModuleLed(bool onFlag)
{
    static bool currentStatusLedFlag = false;

    if (onFlag)
    {
        if (!currentStatusLedFlag)
        {
            currentStatusLedFlag = true;
            digitalWrite(MODULE_LED_PIN, MODULE_LED_ONSTATE);
        }
    }
    else
    {
        if (currentStatusLedFlag)
        {
            currentStatusLedFlag = false;
            digitalWrite(MODULE_LED_PIN, MODULE_LED_OFFSTATE);
        }
    }
}

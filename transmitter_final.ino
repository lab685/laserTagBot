/*
   =====================================================
   ESP32 TRANSMITTER FINAL CODE
   ESP-NOW JOYSTICK CONTROLLER
   FULL DEBUG VERSION
   =====================================================

   FEATURES:
   -----------------------------------------------------
   ✓ ESP-NOW Transmitter
   ✓ Full Debug Logging
   ✓ Enable/Disable Logs Easily
   ✓ Joystick Movement
   ✓ Laser Toggle Button
   ✓ Send Status Monitoring

   =====================================================
*/

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"

#define ESPNOW_WIFI_CHANNEL 4

//////////////////////////////////////////////////////////
// ENABLE / DISABLE LOGS
//////////////////////////////////////////////////////////

bool ENABLE_LOGS = true;

// Granular transmitter log categories. Use `logMask` to enable combinations.
enum LogCategory : uint8_t
{
    LOG_JOYSTICK = 1 << 0,
    LOG_LASER = 1 << 1,
    LOG_SEND = 1 << 2,
    LOG_SYSTEM = 1 << 3,
    LOG_ALL = 0xFF
};

uint8_t logMask = 0; // default: disable all logs

void setLogMask(uint8_t mask)
{
    logMask = mask;
}

void enableLog(uint8_t cat)
{
    logMask |= cat;
}

void disableLog(uint8_t cat)
{
    logMask &= ~cat;
}

inline bool isLogEnabled(uint8_t cat)
{
    return (logMask & cat) != 0;
}

uint8_t categoryFromName(const String &name)
{
    String s = name;
    s.toLowerCase();

    if (s == "joystick" || s == "joy")
        return LOG_JOYSTICK;
    if (s == "laser")
        return LOG_LASER;
    if (s == "send" || s == "tx")
        return LOG_SEND;
    if (s == "system" || s == "setup")
        return LOG_SYSTEM;
    if (s == "all")
        return LOG_ALL;
    if (s == "none")
        return 0;

    return 0xFF;
}

void printLogStatus()
{
    Serial.println();
    Serial.print("Log mask: 0x");
    Serial.println(logMask, HEX);
    Serial.print(" JOYSTICK: ");
    Serial.println(isLogEnabled(LOG_JOYSTICK) ? "ON" : "OFF");
    Serial.print(" LASER: ");
    Serial.println(isLogEnabled(LOG_LASER) ? "ON" : "OFF");
    Serial.print(" SEND: ");
    Serial.println(isLogEnabled(LOG_SEND) ? "ON" : "OFF");
    Serial.print(" SYSTEM: ");
    Serial.println(isLogEnabled(LOG_SYSTEM) ? "ON" : "OFF");
    Serial.println();
}

void handleLogCommand(String cmdRaw)
{
    cmdRaw.trim();
    if (cmdRaw.length() == 0)
        return;

    int sp = cmdRaw.indexOf(' ');
    String verb = (sp == -1) ? cmdRaw : cmdRaw.substring(0, sp);
    String arg = (sp == -1) ? String("") : cmdRaw.substring(sp + 1);
    verb.toLowerCase();
    arg.trim();
    arg.toLowerCase();

    if (verb == "help")
    {
        Serial.println("Log commands:");
        Serial.println("  show                  - show enabled logs");
        Serial.println("  set <hex|dec>         - set mask directly (e.g. set 0x0F)");
        Serial.println("  enable <name>         - enable category (joystick, laser, send, system, all)");
        Serial.println("  disable <name>        - disable category");
        Serial.println("  toggle <name>         - toggle category on/off");
        Serial.println("  help                  - this message");
        return;
    }

    if (verb == "show")
    {
        printLogStatus();
        return;
    }

    if (verb == "set")
    {
        if (arg.length() == 0)
        {
            Serial.println("set requires a value");
            return;
        }

        uint8_t val = 0;
        if (arg.startsWith("0x"))
            val = (uint8_t)strtoul(arg.c_str(), nullptr, 16);
        else
            val = (uint8_t)strtoul(arg.c_str(), nullptr, 10);

        setLogMask(val);
        Serial.print("Mask set to 0x");
        Serial.println(logMask, HEX);
        return;
    }

    if (verb == "enable" || verb == "disable" || verb == "toggle")
    {
        if (arg.length() == 0)
        {
            Serial.println("specify category: joystick, laser, send, system, all");
            return;
        }

        uint8_t cat = categoryFromName(arg);
        if (cat == 0xFF)
        {
            Serial.println("unknown category");
            return;
        }

        if (verb == "enable")
        {
            enableLog(cat);
            Serial.print("Enabled ");
            Serial.println(arg);
        }
        else if (verb == "disable")
        {
            disableLog(cat);
            Serial.print("Disabled ");
            Serial.println(arg);
        }
        else
        {
            if (isLogEnabled(cat))
            {
                disableLog(cat);
                Serial.print("Toggled OFF ");
                Serial.println(arg);
            }
            else
            {
                enableLog(cat);
                Serial.print("Toggled ON ");
                Serial.println(arg);
            }
        }

        printLogStatus();
        return;
    }

    Serial.println("unknown command; type 'help' for usage");
}

//////////////////////////////////////////////////////////
// PINS
//////////////////////////////////////////////////////////

// JOYSTICK
const int JOY_X = 35;
const int JOY_Y = 34;

// Raw joystick calibration. Adjust these to match the physical center.
int JOY_X_CENTER_RAW = 2048;
int JOY_Y_CENTER_RAW = 2048;
int JOY_DEADZONE_RAW = 100;

// LASER BUTTON (direct control)
const int LASER_BUTTON = 18;

// LED that mirrors laser state
const int LASER_LED = 16;

const unsigned long SEND_INTERVAL_MS = 20;
const unsigned long LASER_DEBOUNCE_MS = 20;
const int JOY_CALIBRATION_SAMPLES = 120;
const unsigned long JOY_CALIBRATION_DELAY_MS = 5;

//////////////////////////////////////////////////////////
// STRUCT
//////////////////////////////////////////////////////////

typedef struct struct_message
{

    int x;
    int y;
    bool laserOn;

} struct_message;

struct_message outgoingData;

//////////////////////////////////////////////////////////
// FORWARD DECLARATIONS
//////////////////////////////////////////////////////////

void logMsg(String msg);
void logCategory(uint8_t cat, const String &msg);
void IRAM_ATTR onLaserButtonEdge();
int mapAxisWithDeadzone(int rawValue, int centerValue, int deadzoneValue);
void calibrateJoystickCenter();
void blinkCalibrationPattern();

//////////////////////////////////////////////////////////
// BROADCAST PEER
//////////////////////////////////////////////////////////

class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer
{

public:
    ESP_NOW_Broadcast_Peer(uint8_t channel,
                           wifi_interface_t iface,
                           const uint8_t *lmk)
        : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR,
                       channel,
                       iface,
                       lmk) {}

    ~ESP_NOW_Broadcast_Peer()
    {

        remove();
    }

    bool begin()
    {

        if (!ESP_NOW.begin() || !add())
        {

            logCategory(LOG_SYSTEM, "[ERROR] Failed to initialize ESP-NOW or register broadcast peer");
            return false;
        }

        return true;
    }

    bool send_message(const uint8_t *data, size_t len)
    {

        if (!send(data, len))
        {

            logCategory(LOG_SEND, "[ERROR] Failed to broadcast message");
            return false;
        }

        return true;
    }
};

ESP_NOW_Broadcast_Peer broadcast_peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);

//////////////////////////////////////////////////////////
// STATES
//////////////////////////////////////////////////////////

bool laserState = false;
volatile bool laserButtonPressEdgeDetected = false;
volatile bool laserButtonReleaseEdgeDetected = false;

//////////////////////////////////////////////////////////
// LOG FUNCTION
//////////////////////////////////////////////////////////

void logMsg(String msg)
{

    if (ENABLE_LOGS)
    {

        Serial.println(msg);
    }
}

void logCategory(uint8_t cat, const String &msg)
{

    if (ENABLE_LOGS && isLogEnabled(cat))
    {

        Serial.println(msg);
    }
}

void IRAM_ATTR onLaserButtonEdge()
{

    if (digitalRead(LASER_BUTTON) == HIGH)
    {
        laserButtonPressEdgeDetected = true;
    }
    else
    {
        laserButtonReleaseEdgeDetected = true;
    }
}

int mapAxisWithDeadzone(int rawValue, int centerValue, int deadzoneValue)
{

    int lowerBound = centerValue - deadzoneValue;
    int upperBound = centerValue + deadzoneValue;

    if (rawValue >= lowerBound && rawValue <= upperBound)
    {

        return 0;
    }

    if (rawValue < lowerBound)
    {

        return map(rawValue, 0, lowerBound, 255, 0);
    }

    return map(rawValue, upperBound, 4095, 0, -255);
}

void blinkCalibrationPattern()
{
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(LASER_LED, HIGH);
        delay(120);
        digitalWrite(LASER_LED, LOW);
        delay(120);
    }
}

void calibrateJoystickCenter()
{
    long sumX = 0;
    long sumY = 0;

    logCategory(LOG_SYSTEM, "[JOY] Calibrating joystick center...");

    for (int i = 0; i < JOY_CALIBRATION_SAMPLES; i++)
    {
        sumX += analogRead(JOY_X);
        sumY += analogRead(JOY_Y);
        delay(JOY_CALIBRATION_DELAY_MS);
    }

    JOY_X_CENTER_RAW = (int)(sumX / JOY_CALIBRATION_SAMPLES);
    JOY_Y_CENTER_RAW = (int)(sumY / JOY_CALIBRATION_SAMPLES);

    if (ENABLE_LOGS)
    {
        Serial.print("[JOY] Center X: ");
        Serial.println(JOY_X_CENTER_RAW);
        Serial.print("[JOY] Center Y: ");
        Serial.println(JOY_Y_CENTER_RAW);
    }

    blinkCalibrationPattern();
}

//////////////////////////////////////////////////////////
// SETUP
//////////////////////////////////////////////////////////

void setup()
{

    Serial.begin(115200);

    logCategory(LOG_SYSTEM, "");
    logCategory(LOG_SYSTEM, "=================================");
    logCategory(LOG_SYSTEM, "ESP32 TRANSMITTER STARTING");
    logCategory(LOG_SYSTEM, "=================================");

    ////////////////////////////////////////////////////////
    // INPUTS
    ////////////////////////////////////////////////////////

    // Use internal pull-down so a pressed switch drives the pin HIGH
    pinMode(LASER_BUTTON, INPUT_PULLDOWN);
    pinMode(LASER_LED, OUTPUT);
    digitalWrite(LASER_LED, LOW);

    calibrateJoystickCenter();

    attachInterrupt(digitalPinToInterrupt(LASER_BUTTON),
                    onLaserButtonEdge,
                    CHANGE);

    logCategory(LOG_SYSTEM, "[OK] Inputs Initialized");

    ////////////////////////////////////////////////////////
    // WIFI
    ////////////////////////////////////////////////////////

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

    while (!WiFi.STA.started())
    {

        delay(100);
    }

    logCategory(LOG_SYSTEM, "[OK] WiFi STA Mode Started");

    Serial.print("[MAC] ");
    Serial.println(WiFi.macAddress());

    ////////////////////////////////////////////////////////
    // ESPNOW
    ////////////////////////////////////////////////////////

    logCategory(LOG_SYSTEM, "[INIT] Initializing ESP-NOW broadcast peer...");

    if (!broadcast_peer.begin())
    {

        logCategory(LOG_SYSTEM, "[ERROR] Broadcast peer setup failed");

        while (true)
            ;
    }

    logCategory(LOG_SYSTEM, "[OK] Broadcast Peer Ready");

    logCategory(LOG_SYSTEM, "=================================");
    logCategory(LOG_SYSTEM, "TRANSMITTER READY");
    logCategory(LOG_SYSTEM, "=================================");

    Serial.println("Transmitter logging control available. Type 'help' for commands.");
}

//////////////////////////////////////////////////////////
// LOOP
//////////////////////////////////////////////////////////

void loop()
{

    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0)
            handleLogCommand(cmd);
    }

    static unsigned long lastDebounceTime = 0;
    static unsigned long lastSendTime = 0;

    bool forceImmediateSend = false;

    ////////////////////////////////////////////////////////
    // READ JOYSTICK
    ////////////////////////////////////////////////////////

    int rawX = analogRead(JOY_X);
    int rawY = analogRead(JOY_Y);

    ////////////////////////////////////////////////////////
    // MAP VALUES WITH RAW DEADZONE
    ////////////////////////////////////////////////////////

    int mappedX = mapAxisWithDeadzone(rawX, JOY_X_CENTER_RAW, JOY_DEADZONE_RAW);
    int mappedY = mapAxisWithDeadzone(rawY, JOY_Y_CENTER_RAW, JOY_DEADZONE_RAW);

    if (ENABLE_LOGS && isLogEnabled(LOG_JOYSTICK))
    {
        Serial.println();
        Serial.println("=========== JOYSTICK READINGS ===========");
        Serial.print("RAW X: ");
        Serial.println(rawX);
        Serial.print("RAW Y: ");
        Serial.println(rawY);
        Serial.print("MAPPED X: ");
        Serial.println(mappedX);
        Serial.print("MAPPED Y: ");
        Serial.println(mappedY);
        Serial.println("=========================================");
    }

    ////////////////////////////////////////////////////////
    // LASER BUTTON TOGGLE
    ////////////////////////////////////////////////////////

    bool edgeDetected = false;
    noInterrupts();
    if (laserButtonPressEdgeDetected || laserButtonReleaseEdgeDetected)
    {
        laserButtonPressEdgeDetected = false;
        laserButtonReleaseEdgeDetected = false;
        edgeDetected = true;
    }
    interrupts();

    if (edgeDetected)
    {

        unsigned long now = millis();

        if ((unsigned long)(now - lastDebounceTime) >= LASER_DEBOUNCE_MS)
        {

            lastDebounceTime = now;

            bool newState = (digitalRead(LASER_BUTTON) == HIGH);

            if (newState != laserState)
            {

                laserState = newState;
                forceImmediateSend = true;

                // Mirror to local LED
                digitalWrite(LASER_LED, laserState ? HIGH : LOW);

                // Log only on change
                if (ENABLE_LOGS && isLogEnabled(LOG_LASER))
                {
                    if (laserState)
                        logMsg("[LASER] ON");
                    else
                        logMsg("[LASER] OFF");
                }
            }
        }
    }

    ////////////////////////////////////////////////////////
    // FILL STRUCT
    ////////////////////////////////////////////////////////

    int leftSpeed = mappedY + mappedX;
    int rightSpeed = mappedY - mappedX;

    int maxSpeed = max(abs(leftSpeed), abs(rightSpeed));

    if (maxSpeed > 255)
    {

        leftSpeed = (long)leftSpeed * 255 / maxSpeed;
        rightSpeed = (long)rightSpeed * 255 / maxSpeed;
    }

    outgoingData.x = leftSpeed;
    outgoingData.y = rightSpeed;
    outgoingData.laserOn = laserState;

    ////////////////////////////////////////////////////////
    // SEND DATA
    ////////////////////////////////////////////////////////

    bool sendOk = true;
    bool shouldSend = forceImmediateSend ||
                      ((unsigned long)(millis() - lastSendTime) >= SEND_INTERVAL_MS);

    if (shouldSend)
    {

        sendOk = broadcast_peer.send_message(
            (uint8_t *)&outgoingData,
            sizeof(outgoingData));
        lastSendTime = millis();

        if (ENABLE_LOGS && isLogEnabled(LOG_SEND))
        {
            Serial.println();
            Serial.println("========== TX SEND ==========");
            Serial.print("LEFT SPEED: ");
            Serial.println(leftSpeed);
            Serial.print("RIGHT SPEED: ");
            Serial.println(rightSpeed);
            Serial.print("LASER: ");
            Serial.println(laserState ? "ON" : "OFF");
            Serial.println(sendOk ? "[ESP NOW] Broadcast Packet Queued" : "[ERROR] Broadcast Send Failed");
            Serial.println("=============================");
        }
    }

 //   delay(5);
}
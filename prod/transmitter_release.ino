/*
   =====================================================
   ESP32 TRANSMITTER RELEASE CODE
   ESP-NOW JOYSTICK CONTROLLER (LEAN)
   =====================================================

   Behavior matches transmitter_final.ino core logic:
   - Raw joystick center auto-calibration at startup
   - Laser button edge handling with debounce
   - Pre-mixed left/right motor values over ESP-NOW

   Logging and runtime test/command code removed for low overhead.
*/

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"

#define ESPNOW_WIFI_CHANNEL 4

// Joystick
const int JOY_X = 35;
const int JOY_Y = 34;

int JOY_X_CENTER_RAW = 2048;
int JOY_Y_CENTER_RAW = 2048;
int JOY_DEADZONE_RAW = 100;

// Laser button and LED indicator
const int LASER_BUTTON = 18;
const int LASER_LED = 16;

const unsigned long SEND_INTERVAL_MS = 20;
const unsigned long LASER_DEBOUNCE_MS = 20;
const int JOY_CALIBRATION_SAMPLES = 100;
const unsigned long JOY_CALIBRATION_DELAY_MS = 5;

typedef struct struct_message
{
    int x;
    int y;
    bool laserOn;
} struct_message;

struct_message outgoingData;

class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer
{
public:
    ESP_NOW_Broadcast_Peer(uint8_t channel, wifi_interface_t iface, const uint8_t *lmk)
        : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, iface, lmk) {}

    ~ESP_NOW_Broadcast_Peer()
    {
        remove();
    }

    bool begin()
    {
        return ESP_NOW.begin() && add();
    }

    bool send_message(const uint8_t *data, size_t len)
    {
        return send(data, len);
    }
};

ESP_NOW_Broadcast_Peer broadcast_peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);

bool laserState = false;
volatile bool laserButtonPressEdgeDetected = false;
volatile bool laserButtonReleaseEdgeDetected = false;

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

void blinkCalibrationDonePattern()
{
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(LASER_LED, HIGH);
        delay(120);
        digitalWrite(LASER_LED, LOW);
        delay(120);
    }
}

int applyJoystickCurve(int value)
{
    if (value == 0)
        return 0;

    const float expo = 0.65f;
    float normalized = (float)abs(value) / 255.0f;
    float curved = (expo * normalized * normalized * normalized) +
                   ((1.0f - expo) * normalized);
    int output = (int)(curved * 255.0f + 0.5f);

    return value > 0 ? output : -output;
}

void calibrateJoystickCenter()
{
    long sumX = 0;
    long sumY = 0;
    bool ledState = false;

    digitalWrite(LASER_LED, LOW);

    for (int i = 0; i < JOY_CALIBRATION_SAMPLES; i++)
    {
        sumX += analogRead(JOY_X);
        sumY += analogRead(JOY_Y);

        if ((i % 5) == 0)
        {
            ledState = !ledState;
            digitalWrite(LASER_LED, ledState ? HIGH : LOW);
        }

        delay(JOY_CALIBRATION_DELAY_MS);
    }

    digitalWrite(LASER_LED, LOW);

    JOY_X_CENTER_RAW = (int)(sumX / JOY_CALIBRATION_SAMPLES);
    JOY_Y_CENTER_RAW = (int)(sumY / JOY_CALIBRATION_SAMPLES);

    blinkCalibrationDonePattern();
}

void setup()
{
    pinMode(LASER_BUTTON, INPUT_PULLDOWN);
    pinMode(LASER_LED, OUTPUT);
    digitalWrite(LASER_LED, LOW);

    calibrateJoystickCenter();

    attachInterrupt(digitalPinToInterrupt(LASER_BUTTON), onLaserButtonEdge, CHANGE);

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

    while (!WiFi.STA.started())
    {
        delay(100);
    }

    if (!broadcast_peer.begin())
    {
        while (true)
        {
            delay(1000);
        }
    }
}

void loop()
{
    static unsigned long lastDebounceTime = 0;
    static unsigned long lastSendTime = 0;

    bool forceImmediateSend = false;

    int rawX = analogRead(JOY_X);
    int rawY = analogRead(JOY_Y);

    int mappedX = mapAxisWithDeadzone(rawX, JOY_X_CENTER_RAW, JOY_DEADZONE_RAW);
    int mappedY = mapAxisWithDeadzone(rawY, JOY_Y_CENTER_RAW, JOY_DEADZONE_RAW);

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
                digitalWrite(LASER_LED, laserState ? HIGH : LOW);
            }
        }
    }

    // apply same non-linear response curve to each axis before mixing
    mappedX = applyJoystickCurve(mappedX);
    mappedY = applyJoystickCurve(mappedY);

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

    bool shouldSend = forceImmediateSend ||
                      ((unsigned long)(millis() - lastSendTime) >= SEND_INTERVAL_MS);

    if (shouldSend)
    {
        broadcast_peer.send_message((uint8_t *)&outgoingData, sizeof(outgoingData));
        lastSendTime = millis();
    }
}

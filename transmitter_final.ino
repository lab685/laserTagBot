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

#define ESPNOW_WIFI_CHANNEL 2

//////////////////////////////////////////////////////////
// ENABLE / DISABLE LOGS
//////////////////////////////////////////////////////////

bool ENABLE_LOGS = true;

//////////////////////////////////////////////////////////
// PINS
//////////////////////////////////////////////////////////

// JOYSTICK
const int JOY_X = 35;
const int JOY_Y = 34;

// LASER BUTTON (direct control)
const int LASER_BUTTON = 18;

// LED that mirrors laser state
const int LASER_LED = 16;

const unsigned long SEND_INTERVAL_MS = 20;
const unsigned long LASER_DEBOUNCE_MS = 20;

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
void IRAM_ATTR onLaserButtonEdge();

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

            logMsg("[ERROR] Failed to initialize ESP-NOW or register broadcast peer");
            return false;
        }

        return true;
    }

    bool send_message(const uint8_t *data, size_t len)
    {

        if (!send(data, len))
        {

            logMsg("[ERROR] Failed to broadcast message");
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
volatile bool laserButtonEdgeDetected = false;

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

void IRAM_ATTR onLaserButtonEdge()
{

    laserButtonEdgeDetected = true;
}

//////////////////////////////////////////////////////////
// SETUP
//////////////////////////////////////////////////////////

void setup()
{

    Serial.begin(115200);

    logMsg("");
    logMsg("=================================");
    logMsg("ESP32 TRANSMITTER STARTING");
    logMsg("=================================");

    ////////////////////////////////////////////////////////
    // INPUTS
    ////////////////////////////////////////////////////////

    // Use internal pull-down so a pressed switch drives the pin HIGH
    pinMode(LASER_BUTTON, INPUT_PULLDOWN);
    pinMode(LASER_LED, OUTPUT);
    digitalWrite(LASER_LED, LOW);
    attachInterrupt(digitalPinToInterrupt(LASER_BUTTON),
                    onLaserButtonEdge,
                    CHANGE);

    logMsg("[OK] Inputs Initialized");

    ////////////////////////////////////////////////////////
    // WIFI
    ////////////////////////////////////////////////////////

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

    while (!WiFi.STA.started())
    {

        delay(100);
    }

    logMsg("[OK] WiFi STA Mode Started");

    Serial.print("[MAC] ");
    Serial.println(WiFi.macAddress());

    ////////////////////////////////////////////////////////
    // ESPNOW
    ////////////////////////////////////////////////////////

    logMsg("[INIT] Initializing ESP-NOW broadcast peer...");

    if (!broadcast_peer.begin())
    {

        logMsg("[ERROR] Broadcast peer setup failed");

        while (true)
            ;
    }

    logMsg("[OK] Broadcast Peer Ready");

    logMsg("=================================");
    logMsg("TRANSMITTER READY");
    logMsg("=================================");
}

//////////////////////////////////////////////////////////
// LOOP
//////////////////////////////////////////////////////////

void loop()
{

    static unsigned long lastDebounceTime = 0;
    static unsigned long lastSendTime = 0;

    bool forceImmediateSend = false;

    ////////////////////////////////////////////////////////
    // READ JOYSTICK
    ////////////////////////////////////////////////////////

    int rawX = analogRead(JOY_X);
    int rawY = analogRead(JOY_Y);

    ////////////////////////////////////////////////////////
    // MAP VALUES
    ////////////////////////////////////////////////////////

    int mappedX = map(rawX, 0, 4095, 255, -255);
    int mappedY = map(rawY, 0, 4095, -255, 255);

    ////////////////////////////////////////////////////////
    // DEADZONE
    ////////////////////////////////////////////////////////

    if (abs(mappedX) < 100)
        mappedX = 0;
    if (abs(mappedY) < 100)
        mappedY = 0;

    ////////////////////////////////////////////////////////
    // INVERT Y AXIS
    ////////////////////////////////////////////////////////

    mappedY = -mappedY;

    ////////////////////////////////////////////////////////
    // LASER BUTTON TOGGLE
    ////////////////////////////////////////////////////////

    bool edgeDetected = false;
    noInterrupts();
    if (laserButtonEdgeDetected)
    {
        laserButtonEdgeDetected = false;
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
                if (ENABLE_LOGS)
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
    }

    ////////////////////////////////////////////////////////
    // FULL DEBUG LOGGING
    ////////////////////////////////////////////////////////

    if (ENABLE_LOGS && shouldSend)
    {

        Serial.println();
        Serial.println("========== TRANSMITTER DEBUG ==========");

        Serial.print("RAW X: ");
        Serial.println(rawX);

        Serial.print("RAW Y: ");
        Serial.println(rawY);

        Serial.print("MAPPED X: ");
        Serial.println(mappedX);

        Serial.print("MAPPED Y: ");
        Serial.println(mappedY);

        Serial.print("LEFT SPEED: ");
        Serial.println(leftSpeed);

        Serial.print("RIGHT SPEED: ");
        Serial.println(rightSpeed);

        Serial.print("LASER: ");

        if (laserState)
        {

            Serial.println("ON");
        }
        else
        {

            Serial.println("OFF");
        }

        if (sendOk)
        {

            Serial.println("[ESP NOW] Broadcast Packet Queued");
        }
        else
        {

            Serial.println("[ERROR] Broadcast Send Failed");
        }

        Serial.println("=======================================");
    }

    delay(5);
}
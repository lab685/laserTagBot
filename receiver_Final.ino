/*
   =========================================================
   ESP32 RECEIVER FINAL CODE
   ESP-NOW + MOTORS + LASER + SERVO + LDR DETECTION
   =========================================================

   FEATURES:
   ---------------------------------------------------------
   ✓ ESP-NOW Receiver
   ✓ 4 Motor Control using 2x TB6612FNG
   ✓ Laser ON/OFF from transmitter
   ✓ Servo Trigger on LDR detection
   ✓ LDR Monitoring
   ✓ Connection Timeout Safety
   ✓ Full Debug Logging
   ✓ Enable/Disable Logs Easily

   =========================================================
*/

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <ESP32Servo.h>

#include <vector>

#define ESPNOW_WIFI_CHANNEL 2

////////////////////////////////////////////////////////////
// ENABLE/DISABLE LOGGING
////////////////////////////////////////////////////////////

bool ENABLE_LOGS = true;

////////////////////////////////////////////////////////////
// MOTOR PINS
////////////////////////////////////////////////////////////

// LEFT SIDE DRIVER
const int LEFT_IN1 = 21;
const int LEFT_IN2 = 22;
const int LEFT_PWM = 23;

// RIGHT SIDE DRIVER
const int RIGHT_IN1 = 17;
const int RIGHT_IN2 = 18;
const int RIGHT_PWM = 19;

////////////////////////////////////////////////////////////
// OTHER COMPONENTS
////////////////////////////////////////////////////////////

const int LASER_PIN = 14;

// IMPORTANT:
// GPIO 34 DOES NOT WORK FOR SERVO (INPUT ONLY)
// USE GPIO 27 INSTEAD
const int SERVO_PIN = 27;

// LDR SENSORS
const int LDR_LEFT = 32;
const int LDR_RIGHT = 33;

////////////////////////////////////////////////////////////
// SETTINGS
////////////////////////////////////////////////////////////

const int PWM_FREQ = 1000;
const int PWM_RESOLUTION = 8;

const int MAX_SPEED = 180;

const int LDR_THRESHOLD = 4000;

const unsigned long HIT_RECOVERY_DELAY = 1000;

const unsigned long CONNECTION_TIMEOUT = 2000;

////////////////////////////////////////////////////////////
// STATES
////////////////////////////////////////////////////////////

bool transmitterConnected = false;

unsigned long lastPacketTime = 0;

bool hitDetected = false;

unsigned long hitClearSince = 0;

////////////////////////////////////////////////////////////
// SERVO
////////////////////////////////////////////////////////////

Servo myServo;

////////////////////////////////////////////////////////////
// ESPNOW STRUCT
////////////////////////////////////////////////////////////

typedef struct struct_message
{

    int x;
    int y;
    bool laserOn;

} struct_message;

struct_message incomingData;

////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS
////////////////////////////////////////////////////////////

void logMsg(String msg);

void handleIncomingPacket(const uint8_t *incomingDataBytes,
                          size_t len,
                          bool broadcast);

void applyMotorSpeeds(int leftSpeed, int rightSpeed);

int scaleSpeedToMax(int speed);

////////////////////////////////////////////////////////////
// PEER CLASS
////////////////////////////////////////////////////////////

class ESP_NOW_Peer_Class : public ESP_NOW_Peer
{

public:
    ESP_NOW_Peer_Class(const uint8_t *mac_addr,
                       uint8_t channel,
                       wifi_interface_t iface,
                       const uint8_t *lmk)
        : ESP_NOW_Peer(mac_addr,
                       channel,
                       iface,
                       lmk) {}

    bool add_peer()
    {

        if (!add())
        {

            logMsg("[ERROR] Peer Add Failed");
            return false;
        }

        return true;
    }

    void onReceive(const uint8_t *data,
                   size_t len,
                   bool broadcast)
    {

        handleIncomingPacket(data, len, broadcast);
    }
};

////////////////////////////////////////////////////////////
// GLOBALS
////////////////////////////////////////////////////////////

std::vector<ESP_NOW_Peer_Class *> masters;

////////////////////////////////////////////////////////////
// NEW PEER CALLBACK
////////////////////////////////////////////////////////////

void register_new_master(
    const esp_now_recv_info_t *info,
    const uint8_t *data,
    int len,
    void *arg)
{

    (void)data;
    (void)len;
    (void)arg;

    if (memcmp(info->des_addr,
               ESP_NOW.BROADCAST_ADDR,
               6) == 0)
    {

        ESP_NOW_Peer_Class *new_master =
            new ESP_NOW_Peer_Class(
                info->src_addr,
                ESPNOW_WIFI_CHANNEL,
                WIFI_IF_STA,
                nullptr);

        if (!new_master->add_peer())
        {

            delete new_master;
            return;
        }

        masters.push_back(new_master);

        Serial.println("[OK] Master Registered");
    }
}

////////////////////////////////////////////////////////////
// LOG FUNCTION
////////////////////////////////////////////////////////////

void logMsg(String msg)
{

    if (ENABLE_LOGS)
    {

        Serial.println(msg);
    }
}

////////////////////////////////////////////////////////////
// SETUP
////////////////////////////////////////////////////////////

void setup()
{

    Serial.begin(115200);

    logMsg("");
    logMsg("=======================================");
    logMsg("ESP32 RECEIVER STARTING");
    logMsg("=======================================");

    //////////////////////////////////////////////////////////
    // MOTOR PINS
    //////////////////////////////////////////////////////////

    pinMode(LEFT_IN1, OUTPUT);
    pinMode(LEFT_IN2, OUTPUT);

    pinMode(RIGHT_IN1, OUTPUT);
    pinMode(RIGHT_IN2, OUTPUT);

    //////////////////////////////////////////////////////////
    // LASER
    //////////////////////////////////////////////////////////

    pinMode(LASER_PIN, OUTPUT);

    // LASER OFF AT START
    digitalWrite(LASER_PIN, LOW);

    //////////////////////////////////////////////////////////
    // LDR
    //////////////////////////////////////////////////////////

    pinMode(LDR_LEFT, INPUT);
    pinMode(LDR_RIGHT, INPUT);

    //////////////////////////////////////////////////////////
    // PWM
    //////////////////////////////////////////////////////////

    ledcAttach(LEFT_PWM, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(RIGHT_PWM, PWM_FREQ, PWM_RESOLUTION);

    //////////////////////////////////////////////////////////
    // SERVO
    //////////////////////////////////////////////////////////

    myServo.setPeriodHertz(50);

    myServo.attach(SERVO_PIN, 500, 2400);

    // SERVO AT 0 DEGREE AT START
    myServo.write(0);

    logMsg("[SERVO] Initialized at 0 degree");

    //////////////////////////////////////////////////////////
    // STOP MOTORS
    //////////////////////////////////////////////////////////

    stopMotors();

    //////////////////////////////////////////////////////////
    // WIFI
    //////////////////////////////////////////////////////////

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

    while (!WiFi.STA.started())
    {

        delay(100);
    }

    logMsg("[WIFI] STA Mode Started");

    Serial.print("[MAC] ");
    Serial.println(WiFi.macAddress());

    //////////////////////////////////////////////////////////
    // ESPNOW
    //////////////////////////////////////////////////////////

    if (!ESP_NOW.begin())
    {

        logMsg("[ERROR] ESP-NOW Init Failed");

        while (true)
            ;
    }

    logMsg("[OK] ESP-NOW Initialized");

    ESP_NOW.onNewPeer(register_new_master,
                      nullptr);

    logMsg("[OK] New Peer Callback Registered");

    logMsg("=======================================");
    logMsg("RECEIVER READY");
    logMsg("=======================================");
}

////////////////////////////////////////////////////////////
// LOOP
////////////////////////////////////////////////////////////

void loop()
{

    //////////////////////////////////////////////////////////
    // CONNECTION TIMEOUT
    //////////////////////////////////////////////////////////

    if (transmitterConnected &&
        millis() - lastPacketTime > CONNECTION_TIMEOUT)
    {

        transmitterConnected = false;

        logMsg("");
        logMsg("=======================================");
        logMsg("[WARNING] TRANSMITTER DISCONNECTED");
        logMsg("[ACTION] STOPPING ALL SYSTEMS");
        logMsg("=======================================");

        stopMotors();

        digitalWrite(LASER_PIN, LOW);
    }

    //////////////////////////////////////////////////////////
    // READ LDR
    //////////////////////////////////////////////////////////

    static unsigned long lastLDRLog = 0;

    int leftLDR = analogRead(LDR_LEFT);
    int rightLDR = analogRead(LDR_RIGHT);

    bool ldrTriggered = (leftLDR > LDR_THRESHOLD ||
                         rightLDR > LDR_THRESHOLD);

    if (ldrTriggered)
    {

        if (!hitDetected)
        {

            hitDetected = true;

            logMsg("");
            logMsg("=======================================");
            logMsg("[HIT DETECTED]");
            logMsg("[ACTION] STOPPING MOTORS");
            logMsg("[ACTION] MOVING SERVO TO 90");
            logMsg("=======================================");
        }

        hitClearSince = 0;

        // Enforce hit mode continuously while LDR stays above threshold.
        stopMotors();
        digitalWrite(LASER_PIN, LOW);
        myServo.write(90);
    }
    else if (hitDetected)
    {

        if (hitClearSince == 0)
        {

            hitClearSince = millis();
        }
        else if (millis() - hitClearSince > HIT_RECOVERY_DELAY)
        {

            hitDetected = false;
            hitClearSince = 0;

            myServo.write(0);

            logMsg("");
            logMsg("=======================================");
            logMsg("[RECOVERED] HIT MODE CLEARED");
            logMsg("=======================================");
        }
    }

    //////////////////////////////////////////////////////////
    // LOG LDR VALUES
    //////////////////////////////////////////////////////////

    if (millis() - lastLDRLog > 500)
    {

        lastLDRLog = millis();

        if (ENABLE_LOGS)
        {

            Serial.println();
            Serial.println("=========== LDR READINGS ===========");

            Serial.print("LEFT LDR  : ");
            Serial.println(leftLDR);

            Serial.print("RIGHT LDR : ");
            Serial.println(rightLDR);

            Serial.println("====================================");
        }
    }

    delay(10);
}

////////////////////////////////////////////////////////////
// PACKET HANDLER
////////////////////////////////////////////////////////////

void handleIncomingPacket(const uint8_t *incomingDataBytes,
                          size_t len,
                          bool broadcast)
{

    (void)broadcast;

    if (len < sizeof(incomingData))
    {

        logMsg("[ERROR] Incoming packet too small");
        return;
    }

    //////////////////////////////////////////////////////////
    // CONNECTION DETECTED
    //////////////////////////////////////////////////////////

    if (!transmitterConnected)
    {

        transmitterConnected = true;

        logMsg("");
        logMsg("=======================================");
        logMsg("[CONNECTED] TRANSMITTER CONNECTED");
        logMsg("=======================================");
    }

    lastPacketTime = millis();

    //////////////////////////////////////////////////////////
    // COPY DATA
    //////////////////////////////////////////////////////////

    memcpy(&incomingData,
           incomingDataBytes,
           sizeof(incomingData));

    //////////////////////////////////////////////////////////
    // IGNORE CONTROLS IF HIT DETECTED
    //////////////////////////////////////////////////////////

    if (hitDetected)
    {
        return;
    }

    //////////////////////////////////////////////////////////
    // LOG RECEIVED DATA
    //////////////////////////////////////////////////////////

    if (ENABLE_LOGS)
    {

        Serial.println();
        Serial.println("=========== PACKET RECEIVED ==========");

        Serial.print("LEFT SPEED: ");
        Serial.println(incomingData.x);

        Serial.print("RIGHT SPEED: ");
        Serial.println(incomingData.y);

        Serial.print("Laser: ");
        Serial.println(incomingData.laserOn ? "ON" : "OFF");

        Serial.println("======================================");
    }

    //////////////////////////////////////////////////////////
    // LASER CONTROL
    //////////////////////////////////////////////////////////

    digitalWrite(LASER_PIN, incomingData.laserOn);

    //////////////////////////////////////////////////////////
    // MOTOR SPEEDS ARE PRE-MIXED BY TRANSMITTER
    //////////////////////////////////////////////////////////

    int leftSpeed = constrain(incomingData.x, -255, 255);
    int rightSpeed = constrain(incomingData.y, -255, 255);

    if (leftSpeed == 0 && rightSpeed == 0)
    {

        stopMotors();
        logMsg("[ACTION] STOP");
        return;
    }

    applyMotorSpeeds(leftSpeed, rightSpeed);

    if (ENABLE_LOGS)
    {

        Serial.print("LEFT SPEED: ");
        Serial.println(leftSpeed);

        Serial.print("RIGHT SPEED: ");
        Serial.println(rightSpeed);
    }
}

////////////////////////////////////////////////////////////
// MOTOR FUNCTIONS
////////////////////////////////////////////////////////////

void moveForward(int leftSpeed, int rightSpeed)
{

    digitalWrite(LEFT_IN1, HIGH);
    digitalWrite(LEFT_IN2, LOW);

    digitalWrite(RIGHT_IN1, HIGH);
    digitalWrite(RIGHT_IN2, LOW);

    ledcWrite(LEFT_PWM, leftSpeed);
    ledcWrite(RIGHT_PWM, rightSpeed);
}

////////////////////////////////////////////////////////////

void moveBackward(int leftSpeed, int rightSpeed)
{

    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, HIGH);

    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, HIGH);

    ledcWrite(LEFT_PWM, leftSpeed);
    ledcWrite(RIGHT_PWM, rightSpeed);
}

////////////////////////////////////////////////////////////

void turnLeft(int speedValue)
{

    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, HIGH);

    digitalWrite(RIGHT_IN1, HIGH);
    digitalWrite(RIGHT_IN2, LOW);

    ledcWrite(LEFT_PWM, speedValue);
    ledcWrite(RIGHT_PWM, speedValue);
}

////////////////////////////////////////////////////////////

void turnRight(int speedValue)
{

    digitalWrite(LEFT_IN1, HIGH);
    digitalWrite(LEFT_IN2, LOW);

    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, HIGH);

    ledcWrite(LEFT_PWM, speedValue);
    ledcWrite(RIGHT_PWM, speedValue);
}

////////////////////////////////////////////////////////////

void stopMotors()
{

    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, LOW);

    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, LOW);

    ledcWrite(LEFT_PWM, 0);
    ledcWrite(RIGHT_PWM, 0);

    logMsg("[MOTOR] STOPPED");
}

////////////////////////////////////////////////////////////
// APPLY PRE-MIXED MOTOR SPEEDS
////////////////////////////////////////////////////////////

void applyMotorSpeeds(int leftSpeed, int rightSpeed)
{

    leftSpeed = scaleSpeedToMax(leftSpeed);
    rightSpeed = scaleSpeedToMax(rightSpeed);

    if (leftSpeed > 0)
    {

        digitalWrite(LEFT_IN1, HIGH);
        digitalWrite(LEFT_IN2, LOW);
    }
    else if (leftSpeed < 0)
    {

        digitalWrite(LEFT_IN1, LOW);
        digitalWrite(LEFT_IN2, HIGH);
        leftSpeed = abs(leftSpeed);
    }
    else
    {

        digitalWrite(LEFT_IN1, LOW);
        digitalWrite(LEFT_IN2, LOW);
    }

    if (rightSpeed > 0)
    {

        digitalWrite(RIGHT_IN1, HIGH);
        digitalWrite(RIGHT_IN2, LOW);
    }
    else if (rightSpeed < 0)
    {

        digitalWrite(RIGHT_IN1, LOW);
        digitalWrite(RIGHT_IN2, HIGH);
        rightSpeed = abs(rightSpeed);
    }
    else
    {

        digitalWrite(RIGHT_IN1, LOW);
        digitalWrite(RIGHT_IN2, LOW);
    }

    ledcWrite(LEFT_PWM, constrain(leftSpeed, 0, MAX_SPEED));
    ledcWrite(RIGHT_PWM, constrain(rightSpeed, 0, MAX_SPEED));
}

////////////////////////////////////////////////////////////
// SCALE SPEED TO RECEIVER MAX
////////////////////////////////////////////////////////////

int scaleSpeedToMax(int speed)
{

    if (speed == 0)
    {

        return 0;
    }

    int scaledSpeed = map(abs(speed), 0, 255, 0, MAX_SPEED);

    return speed > 0 ? scaledSpeed : -scaledSpeed;
}
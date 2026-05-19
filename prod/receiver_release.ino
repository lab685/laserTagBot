/*
   =========================================================
   ESP32 RECEIVER RELEASE CODE
   ESP-NOW + MOTORS + LASER + SERVO + LDR DETECTION (LEAN)
   =========================================================

   Behavior matches receiver_Final.ino core logic:
   - Receives pre-mixed motor values and laser state over ESP-NOW
   - LDR hit detection (> threshold) forces stop + laser off + servo 90
   - Recovery delay returns servo to 0
   - Connection timeout safety stop

   Logging and serial test/command code removed for low overhead.
*/

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <ESP32Servo.h>

#include <vector>

#define ESPNOW_WIFI_CHANNEL 4

// Left side driver
const int LEFT_IN1 = 21;
const int LEFT_IN2 = 22;
const int LEFT_PWM = 23;

// Right side driver
const int RIGHT_IN1 = 17;
const int RIGHT_IN2 = 18;
const int RIGHT_PWM = 19;

// Other components
const int LASER_PIN = 14;
const int SERVO_PIN = 13;
const int LDR_LEFT = 32;
const int LDR_RIGHT = 33;

// Settings
const int PWM_FREQ = 1000;
const int PWM_RESOLUTION = 8;
const int MAX_SPEED = 180;
const int LDR_THRESHOLD = 4000;
const int SERVO_MIN_US = 1000;
const int SERVO_MAX_US = 2000;
const unsigned long HIT_RECOVERY_DELAY = 1000;
const unsigned long CONNECTION_TIMEOUT = 2000;

// States
bool transmitterConnected = false;
unsigned long lastPacketTime = 0;

bool hitDetected = false;
unsigned long hitClearSince = 0;

bool motorsActive = false;

// Servo
Servo myServo;

// ESP-NOW payload
typedef struct struct_message
{
    int x;
    int y;
    bool laserOn;
} struct_message;

struct_message incomingData;

void handleIncomingPacket(const uint8_t *incomingDataBytes, size_t len, bool broadcast);
void applyMotorSpeeds(int leftSpeed, int rightSpeed);
void stopMotors();
int scaleSpeedToMax(int speed);

class ESP_NOW_Peer_Class : public ESP_NOW_Peer
{
public:
    ESP_NOW_Peer_Class(const uint8_t *mac_addr,
                       uint8_t channel,
                       wifi_interface_t iface,
                       const uint8_t *lmk)
        : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}

    bool add_peer()
    {
        return add();
    }

    void onReceive(const uint8_t *data, size_t len, bool broadcast)
    {
        handleIncomingPacket(data, len, broadcast);
    }
};

std::vector<ESP_NOW_Peer_Class *> masters;

void register_new_master(const esp_now_recv_info_t *info,
                         const uint8_t *data,
                         int len,
                         void *arg)
{
    (void)data;
    (void)len;
    (void)arg;

    if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) != 0)
    {
        return;
    }

    ESP_NOW_Peer_Class *new_master =
        new ESP_NOW_Peer_Class(info->src_addr,
                               ESPNOW_WIFI_CHANNEL,
                               WIFI_IF_STA,
                               nullptr);

    if (!new_master->add_peer())
    {
        delete new_master;
        return;
    }

    masters.push_back(new_master);
}

void setup()
{
    pinMode(LEFT_IN1, OUTPUT);
    pinMode(LEFT_IN2, OUTPUT);
    pinMode(RIGHT_IN1, OUTPUT);
    pinMode(RIGHT_IN2, OUTPUT);

    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);

    pinMode(LDR_LEFT, INPUT);
    pinMode(LDR_RIGHT, INPUT);

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    myServo.setPeriodHertz(50);
    myServo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
    myServo.write(0);

    ledcAttach(LEFT_PWM, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(RIGHT_PWM, PWM_FREQ, PWM_RESOLUTION);

    stopMotors();

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

    while (!WiFi.STA.started())
    {
        delay(100);
    }

    if (!ESP_NOW.begin())
    {
        while (true)
        {
            delay(1000);
        }
    }

    ESP_NOW.onNewPeer(register_new_master, nullptr);
}

void loop()
{
    if (transmitterConnected && (millis() - lastPacketTime > CONNECTION_TIMEOUT))
    {
        transmitterConnected = false;
        stopMotors();
        digitalWrite(LASER_PIN, LOW);
    }

    int leftLDR = analogRead(LDR_LEFT);
    int rightLDR = analogRead(LDR_RIGHT);

    bool ldrTriggered = (leftLDR > LDR_THRESHOLD || rightLDR > LDR_THRESHOLD);

    if (ldrTriggered)
    {
        // Latch hit state permanently until restart
        hitDetected = true;

        stopMotors();
        digitalWrite(LASER_PIN, LOW);
        myServo.write(180);
    }
    // else: when latched there is no auto-recovery; system remains stopped
}

void handleIncomingPacket(const uint8_t *incomingDataBytes,
                          size_t len,
                          bool broadcast)
{
    (void)broadcast;

    if (len < sizeof(incomingData))
    {
        return;
    }

    transmitterConnected = true;
    lastPacketTime = millis();

    memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));

    if (hitDetected)
    {
        return;
    }

    digitalWrite(LASER_PIN, incomingData.laserOn ? HIGH : LOW);

    int leftSpeed = constrain(incomingData.x, -255, 255);
    int rightSpeed = constrain(incomingData.y, -255, 255);

    if (leftSpeed == 0 && rightSpeed == 0)
    {
        if (motorsActive)
        {
            stopMotors();
        }
        return;
    }

    applyMotorSpeeds(leftSpeed, rightSpeed);
}

void stopMotors()
{
    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, LOW);
    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, LOW);

    ledcWrite(LEFT_PWM, 0);
    ledcWrite(RIGHT_PWM, 0);

    motorsActive = false;
}

void applyMotorSpeeds(int leftSpeed, int rightSpeed)
{
    leftSpeed = scaleSpeedToMax(leftSpeed);
    rightSpeed = scaleSpeedToMax(rightSpeed);

    motorsActive = true;

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

int scaleSpeedToMax(int speed)
{
    if (speed == 0)
    {
        return 0;
    }

    int scaledSpeed = map(abs(speed), 0, 255, 0, MAX_SPEED);
    return speed > 0 ? scaledSpeed : -scaledSpeed;
}

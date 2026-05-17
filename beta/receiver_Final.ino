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

#define ESPNOW_WIFI_CHANNEL 4

////////////////////////////////////////////////////////////
// ENABLE/DISABLE LOGGING
////////////////////////////////////////////////////////////

bool ENABLE_LOGS = true;

// Granular log categories. Use `logMask` to enable combinations.
// Example: enable LDR + PACKET => setLogMask(LOG_LDR | LOG_PACKET);
enum LogCategory : uint8_t
{
    LOG_LDR = 1 << 0,
    LOG_PACKET = 1 << 1,       // received joystick/packet values
    LOG_MOTOR = 1 << 2,        // motor state changes (stop/start)
    LOG_SERVO = 1 << 3,        // servo actions/recovery
    LOG_MOTOR_OUTPUT = 1 << 4, // numeric motor outputs (left/right PWM)
    LOG_ALL = 0xFF
};

uint8_t logMask = 0; // default: disable all logs

// Forward declaration so serial command handler can move servo for testing.
extern Servo myServo;
extern bool servoManualHold;
extern bool laserManualOverride;
extern bool laserManualState;
extern const int LASER_PIN;

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

// Map string names to categories. Accepts: ldr, packet, motor, servo, motorout, all, none
uint8_t categoryFromName(const String &name)
{
    String s = name;
    s.toLowerCase();

    if (s == "ldr")
        return LOG_LDR;
    if (s == "packet" || s == "joystick" || s == "rx")
        return LOG_PACKET;
    if (s == "motor")
        return LOG_MOTOR;
    if (s == "servo")
        return LOG_SERVO;
    if (s == "motorout" || s == "motor_output" || s == "output")
        return LOG_MOTOR_OUTPUT;
    if (s == "all")
        return LOG_ALL;
    if (s == "none")
        return 0;

    return 0xFF; // invalid
}

void printLogStatus()
{
    Serial.println();
    Serial.print("Log mask: 0x");
    Serial.println(logMask, HEX);
    Serial.print(" LDR: ");
    Serial.println(isLogEnabled(LOG_LDR) ? "ON" : "OFF");
    Serial.print(" PACKET: ");
    Serial.println(isLogEnabled(LOG_PACKET) ? "ON" : "OFF");
    Serial.print(" MOTOR: ");
    Serial.println(isLogEnabled(LOG_MOTOR) ? "ON" : "OFF");
    Serial.print(" SERVO: ");
    Serial.println(isLogEnabled(LOG_SERVO) ? "ON" : "OFF");
    Serial.print(" MOTOR_OUTPUT: ");
    Serial.println(isLogEnabled(LOG_MOTOR_OUTPUT) ? "ON" : "OFF");
    Serial.println();
}

void handleLogCommand(const String &cmdRaw)
{
    String cmd = cmdRaw;
    cmd.trim();
    if (cmd.length() == 0)
        return;

    // split first token
    int sp = cmd.indexOf(' ');
    String verb = (sp == -1) ? cmd : cmd.substring(0, sp);
    String arg = (sp == -1) ? String("") : cmd.substring(sp + 1);
    verb.toLowerCase();
    arg.trim();
    arg.toLowerCase();

    if (verb == "help")
    {
        Serial.println("Log commands:");
        Serial.println("  show                  - show enabled logs");
        Serial.println("  set <hex|dec>         - set mask directly (e.g. set 0x1F)");
        Serial.println("  enable <name>         - enable a category (ldr, packet, motor, servo, motorout, all)");
        Serial.println("  disable <name>        - disable a category");
        Serial.println("  toggle <name>         - toggle a category");
        Serial.println("  servo <0-180>         - move servo directly for testing");
        Serial.println("  servo sweep           - run servo sweep test 0->180->0");
        Serial.println("  servohold on|off      - keep manual servo position / return to auto");
        Serial.println("  laser on|off|toggle   - force receiver laser for testing");
        Serial.println("  laser auto            - return laser control to transmitter");
        Serial.println("  help                  - this message");
        return;
    }

    if (verb == "servohold")
    {
        if (arg == "on")
        {
            servoManualHold = true;
            Serial.println("[SERVO TEST] hold ON (auto LDR servo writes paused)");
            return;
        }

        if (arg == "off")
        {
            servoManualHold = false;
            Serial.println("[SERVO TEST] hold OFF (auto LDR servo writes resumed)");
            return;
        }

        Serial.println("usage: servohold on|off");
        return;
    }

    if (verb == "laser")
    {
        if (arg == "auto")
        {
            laserManualOverride = false;
            Serial.println("[LASER TEST] auto mode ON (transmitter controls laser)");
            return;
        }

        if (arg == "on" || arg == "off" || arg == "toggle")
        {
            laserManualOverride = true;

            if (arg == "toggle")
            {
                laserManualState = !laserManualState;
            }
            else
            {
                laserManualState = (arg == "on");
            }

            digitalWrite(LASER_PIN, laserManualState ? HIGH : LOW);

            Serial.print("[LASER TEST] forced ");
            Serial.println(laserManualState ? "ON" : "OFF");
            return;
        }

        Serial.println("usage: laser on|off|toggle|auto");
        return;
    }

    if (verb == "servo")
    {
        if (arg.length() == 0)
        {
            Serial.println("servo requires an angle: 0-180");
            return;
        }

        if (arg == "sweep")
        {
            servoManualHold = true;

            for (int pos = 0; pos <= 180; pos += 5)
            {
                myServo.write(pos);
                //  delay(15);
            }

            for (int pos = 180; pos >= 0; pos -= 5)
            {
                myServo.write(pos);
                //  delay(15);
            }

            Serial.println("[SERVO TEST] sweep complete (hold remains ON)");
            return;
        }

        char *endPtr = nullptr;
        long angle = strtol(arg.c_str(), &endPtr, 10);
        if (endPtr == arg.c_str() || *endPtr != '\0')
        {
            Serial.println("invalid servo angle; use an integer 0-180");
            return;
        }

        angle = constrain(angle, 0, 180);
        servoManualHold = true;
        myServo.write((int)angle);

        Serial.print("[SERVO TEST] moved to ");
        Serial.print((int)angle);
        Serial.println(" deg (hold ON)");
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
            Serial.println("specify category: ldr, packet, motor, servo, motorout, all");
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
const int SERVO_PIN = 13;

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

const int SERVO_MIN_US = 1000;
const int SERVO_MAX_US = 2000;

const unsigned long HIT_RECOVERY_DELAY = 1000;

const unsigned long CONNECTION_TIMEOUT = 2000;

////////////////////////////////////////////////////////////
// STATES
////////////////////////////////////////////////////////////

bool transmitterConnected = false;

unsigned long lastPacketTime = 0;

bool hitDetected = false;

unsigned long hitClearSince = 0;

bool servoManualHold = false;
bool laserManualOverride = false;
bool laserManualState = false;

// Track whether motors are currently active (moving). Used to avoid
// logging a STOP action immediately when a transmitter connects while
// joysticks are centered.
bool motorsActive = false;

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

    // Print minimal runtime log control help
    Serial.println("Receiver logging control available. Type 'help' for commands.");

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

    logMsg("[SERVO] Initialized at 0 degree");
    if (ENABLE_LOGS && isLogEnabled(LOG_SERVO))
    {
        Serial.println("[SERVO] Initialized at 0 degree");
    }

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

    // Handle serial log control commands first (non-blocking)
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0)
            handleLogCommand(cmd);
    }

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

        if (!laserManualOverride)
        {
            digitalWrite(LASER_PIN, LOW);
        }
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

            if (ENABLE_LOGS && (isLogEnabled(LOG_MOTOR) || isLogEnabled(LOG_SERVO)))
            {
                Serial.println();
                Serial.println("=======================================");
                if (isLogEnabled(LOG_MOTOR))
                    Serial.println("[HIT DETECTED]");
                if (isLogEnabled(LOG_MOTOR))
                    Serial.println("[ACTION] STOPPING MOTORS");
                if (isLogEnabled(LOG_SERVO))
                    Serial.println("[ACTION] MOVING SERVO TO 90");
                Serial.println("=======================================");
            }
        }

        hitClearSince = 0;

        // Enforce hit mode continuously while LDR stays above threshold.
        stopMotors();
        if (!laserManualOverride)
        {
            digitalWrite(LASER_PIN, LOW);
        }
        if (!servoManualHold)
        {
            myServo.write(90);
        }
        if (ENABLE_LOGS && isLogEnabled(LOG_SERVO))
        {
            Serial.println("[SERVO] moved to 90 due to LDR trigger");
        }
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

            if (!servoManualHold)
            {
                myServo.write(0);
            }

            logMsg("");
            logMsg("=======================================");
            if (ENABLE_LOGS && isLogEnabled(LOG_SERVO))
            {
                Serial.println("[RECOVERED] HIT MODE CLEARED");
            }
            logMsg("=======================================");
        }
    }

    //////////////////////////////////////////////////////////
    // LOG LDR VALUES
    //////////////////////////////////////////////////////////

    if (millis() - lastLDRLog > 500)
    {

        lastLDRLog = millis();

        if (ENABLE_LOGS && isLogEnabled(LOG_LDR))
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

    // delay(10);
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

    static struct_message lastLoggedIncomingData = {0, 0, false};
    bool packetChanged = (incomingData.x != lastLoggedIncomingData.x ||
                          incomingData.y != lastLoggedIncomingData.y ||
                          incomingData.laserOn != lastLoggedIncomingData.laserOn);

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

    if (ENABLE_LOGS && packetChanged && isLogEnabled(LOG_PACKET))
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

        lastLoggedIncomingData = incomingData;
    }

    //////////////////////////////////////////////////////////
    // LASER CONTROL
    //////////////////////////////////////////////////////////

    if (!laserManualOverride)
    {
        digitalWrite(LASER_PIN, incomingData.laserOn);
    }
    else
    {
        digitalWrite(LASER_PIN, laserManualState ? HIGH : LOW);
    }

    //////////////////////////////////////////////////////////
    // MOTOR SPEEDS ARE PRE-MIXED BY TRANSMITTER
    //////////////////////////////////////////////////////////

    int leftSpeed = constrain(incomingData.x, -255, 255);
    int rightSpeed = constrain(incomingData.y, -255, 255);

    if (leftSpeed == 0 && rightSpeed == 0)
    {

        if (motorsActive)
        {
            stopMotors();

            if (ENABLE_LOGS && isLogEnabled(LOG_MOTOR))
            {
                Serial.println("[ACTION] STOP");
            }
        }

        return;
    }

    applyMotorSpeeds(leftSpeed, rightSpeed);

    if (ENABLE_LOGS && packetChanged && isLogEnabled(LOG_MOTOR_OUTPUT))
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

    motorsActive = false;
    if (ENABLE_LOGS && isLogEnabled(LOG_MOTOR))
    {
        Serial.println("[MOTOR] STOPPED");
    }
}

////////////////////////////////////////////////////////////
// APPLY PRE-MIXED MOTOR SPEEDS
////////////////////////////////////////////////////////////

void applyMotorSpeeds(int leftSpeed, int rightSpeed)
{

    leftSpeed = scaleSpeedToMax(leftSpeed);
    rightSpeed = scaleSpeedToMax(rightSpeed);

    // Mark motors as active since we're about to drive them
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
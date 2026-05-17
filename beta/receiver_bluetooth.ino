/*
   =========================================================
    ESP32 RECEIVER BLUETOOTH CODE
   BT CONTROL + MOTORS + LASER + SERVO + LDR DETECTION
   =========================================================

   BLUETOOTH COMMANDS
   ---------------------------------------------------------
   fs -> forward
   bs -> backward
   ls -> left
   rs -> right
   ss -> stop (optional)

   No PWM values are expected from Bluetooth. Movement uses
   fixed command speeds mapped to receiver motor limits.

   =========================================================
*/

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <ESP32Servo.h>

#define BT_DEVICE_NAME "LaserTagBot_BT"

////////////////////////////////////////////////////////////
// ENABLE/DISABLE LOGGING
////////////////////////////////////////////////////////////

bool ENABLE_LOGS = true;

enum LogCategory : uint8_t
{
    LOG_LDR = 1 << 0,
    LOG_BT = 1 << 1,
    LOG_MOTOR = 1 << 2,
    LOG_SERVO = 1 << 3,
    LOG_MOTOR_OUTPUT = 1 << 4,
    LOG_ALL = 0xFF
};

uint8_t logMask = 0;

////////////////////////////////////////////////////////////
// MOTOR PINS
////////////////////////////////////////////////////////////

const int LEFT_IN1 = 21;
const int LEFT_IN2 = 22;
const int LEFT_PWM = 23;

const int RIGHT_IN1 = 17;
const int RIGHT_IN2 = 18;
const int RIGHT_PWM = 19;

////////////////////////////////////////////////////////////
// OTHER COMPONENTS
////////////////////////////////////////////////////////////

const int LASER_PIN = 14;
const int SERVO_PIN = 13;
const int LDR_LEFT = 32;
const int LDR_RIGHT = 33;

////////////////////////////////////////////////////////////
// SETTINGS
////////////////////////////////////////////////////////////

const int PWM_FREQ = 1000;
const int PWM_RESOLUTION = 8;

const int MAX_SPEED = 180;
const int COMMAND_SPEED = 255;

const int LDR_THRESHOLD = 4000;

const int SERVO_MIN_US = 1000;
const int SERVO_MAX_US = 2000;

const unsigned long HIT_RECOVERY_DELAY = 1000;
const unsigned long CONNECTION_TIMEOUT = 2000;

////////////////////////////////////////////////////////////
// BLUETOOTH
////////////////////////////////////////////////////////////

BluetoothSerial SerialBT;

////////////////////////////////////////////////////////////
// STATES
////////////////////////////////////////////////////////////

bool controllerConnected = false;
unsigned long lastCommandTime = 0;

bool hitDetected = false;
unsigned long hitClearSince = 0;

bool servoManualHold = false;
bool laserManualOverride = false;
bool laserManualState = false;

bool motorsActive = false;

////////////////////////////////////////////////////////////
// SERVO
////////////////////////////////////////////////////////////

Servo myServo;

////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS
////////////////////////////////////////////////////////////

void stopMotors();
void applyMotorSpeeds(int leftSpeed, int rightSpeed);
int scaleSpeedToMax(int speed);
void handleBluetoothCommand(String command);
void processBluetoothInput();
void handleLogCommand(const String &cmdRaw);

////////////////////////////////////////////////////////////
// LOG HELPERS
////////////////////////////////////////////////////////////

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

void logMsg(const String &msg)
{
    if (ENABLE_LOGS)
    {
        Serial.println(msg);
    }
}

uint8_t categoryFromName(const String &name)
{
    String s = name;
    s.toLowerCase();

    if (s == "ldr")
        return LOG_LDR;
    if (s == "bt" || s == "packet" || s == "rx")
        return LOG_BT;
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

    return 0xFF;
}

void printLogStatus()
{
    Serial.println();
    Serial.print("Log mask: 0x");
    Serial.println(logMask, HEX);
    Serial.print(" LDR: ");
    Serial.println(isLogEnabled(LOG_LDR) ? "ON" : "OFF");
    Serial.print(" BT: ");
    Serial.println(isLogEnabled(LOG_BT) ? "ON" : "OFF");
    Serial.print(" MOTOR: ");
    Serial.println(isLogEnabled(LOG_MOTOR) ? "ON" : "OFF");
    Serial.print(" SERVO: ");
    Serial.println(isLogEnabled(LOG_SERVO) ? "ON" : "OFF");
    Serial.print(" MOTOR_OUTPUT: ");
    Serial.println(isLogEnabled(LOG_MOTOR_OUTPUT) ? "ON" : "OFF");
    Serial.println();
}

////////////////////////////////////////////////////////////
// LASER
////////////////////////////////////////////////////////////

void writeLaser(bool state)
{
    digitalWrite(LASER_PIN, state ? HIGH : LOW);
}

////////////////////////////////////////////////////////////
// SERIAL COMMANDS (LOG + TEST)
////////////////////////////////////////////////////////////

void handleLogCommand(const String &cmdRaw)
{
    String cmd = cmdRaw;
    cmd.trim();
    if (cmd.length() == 0)
        return;

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
        Serial.println("  enable <name>         - ldr, bt, motor, servo, motorout, all");
        Serial.println("  disable <name>        - disable a category");
        Serial.println("  toggle <name>         - toggle a category");
        Serial.println("  servo <0-180>         - move servo directly");
        Serial.println("  servo sweep           - run servo sweep test");
        Serial.println("  servohold on|off      - pause/resume auto servo writes");
        Serial.println("  laser on|off|toggle   - force receiver laser");
        Serial.println("  laser auto            - return laser to command control");
        Serial.println("  help                  - this message");
        return;
    }

    if (verb == "servohold")
    {
        if (arg == "on")
        {
            servoManualHold = true;
            Serial.println("[SERVO TEST] hold ON");
            return;
        }
        if (arg == "off")
        {
            servoManualHold = false;
            Serial.println("[SERVO TEST] hold OFF");
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
            Serial.println("[LASER TEST] auto mode ON");
            return;
        }

        if (arg == "on" || arg == "off" || arg == "toggle")
        {
            laserManualOverride = true;
            if (arg == "toggle")
                laserManualState = !laserManualState;
            else
                laserManualState = (arg == "on");

            writeLaser(laserManualState);
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
            }
            for (int pos = 180; pos >= 0; pos -= 5)
            {
                myServo.write(pos);
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
            Serial.println("specify category: ldr, bt, motor, servo, motorout, all");
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
// MOTOR FUNCTIONS
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

////////////////////////////////////////////////////////////
// BLUETOOTH INPUT
////////////////////////////////////////////////////////////

void handleBluetoothCommand(String command)
{
    command.trim();
    command.toLowerCase();
    if (command.length() == 0)
    {
        return;
    }

    if (!controllerConnected)
    {
        controllerConnected = true;
        Serial.println();
        Serial.println("=======================================");
        Serial.println("[CONNECTED] BLUETOOTH CONTROLLER CONNECTED");
        Serial.println("=======================================");
    }

    lastCommandTime = millis();

    int leftSpeed = 0;
    int rightSpeed = 0;
    bool commandRecognized = true;

    if (command == "fs")
    {
        leftSpeed = COMMAND_SPEED;
        rightSpeed = COMMAND_SPEED;
    }
    else if (command == "bs")
    {
        leftSpeed = -COMMAND_SPEED;
        rightSpeed = -COMMAND_SPEED;
    }
    else if (command == "ls")
    {
        leftSpeed = -COMMAND_SPEED;
        rightSpeed = COMMAND_SPEED;
    }
    else if (command == "rs")
    {
        leftSpeed = COMMAND_SPEED;
        rightSpeed = -COMMAND_SPEED;
    }
    else if (command == "ss" || command == "stop")
    {
        leftSpeed = 0;
        rightSpeed = 0;
    }
    else if (command == "laser")
    {
        if (!laserManualOverride)
        {
            static bool btLaserState = false;
            btLaserState = !btLaserState;
            writeLaser(btLaserState);
            SerialBT.println(btLaserState ? "[CMD] LASER ON" : "[CMD] LASER OFF");
        }
        return;
    }
    else
    {
        commandRecognized = false;
    }

    if (!commandRecognized)
    {
        if (ENABLE_LOGS && isLogEnabled(LOG_BT))
        {
            Serial.print("[BT] Unknown command: ");
            Serial.println(command);
        }
        return;
    }

    if (hitDetected)
    {
        return;
    }

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
    }
    else
    {
        applyMotorSpeeds(leftSpeed, rightSpeed);
        if (ENABLE_LOGS && isLogEnabled(LOG_MOTOR_OUTPUT))
        {
            Serial.print("LEFT SPEED: ");
            Serial.println(leftSpeed);
            Serial.print("RIGHT SPEED: ");
            Serial.println(rightSpeed);
        }
    }

    if (ENABLE_LOGS && isLogEnabled(LOG_BT))
    {
        Serial.print("[BT RX] ");
        Serial.println(command);
    }
}

void processBluetoothInput()
{
    static String btLine;

    while (SerialBT.available())
    {
        char c = (char)SerialBT.read();

        if (c == '\n' || c == '\r')
        {
            btLine.trim();
            if (btLine.length() > 0)
            {
                handleBluetoothCommand(btLine);
                btLine = "";
            }
            continue;
        }

        btLine += c;

        // Process short movement tokens immediately when no more bytes are queued.
        if (btLine.length() == 2 && !SerialBT.available())
        {
            String quick = btLine;
            quick.toLowerCase();
            if (quick == "fs" || quick == "bs" || quick == "ls" || quick == "rs" || quick == "ss")
            {
                handleBluetoothCommand(btLine);
                btLine = "";
                continue;
            }
        }

        // Safety cap for malformed long inputs.
        if (btLine.length() > 64)
        {
            btLine = "";
        }
    }
}

////////////////////////////////////////////////////////////
// SETUP
////////////////////////////////////////////////////////////

void setup()
{
    Serial.begin(115200);
    SerialBT.begin(BT_DEVICE_NAME);

    logMsg("");
    logMsg("=======================================");
    logMsg("ESP32 RECEIVER BLUETOOTH STARTING");
    logMsg("=======================================");

    Serial.println("Receiver logging control available. Type 'help' for commands.");

    pinMode(LEFT_IN1, OUTPUT);
    pinMode(LEFT_IN2, OUTPUT);
    pinMode(RIGHT_IN1, OUTPUT);
    pinMode(RIGHT_IN2, OUTPUT);

    pinMode(LASER_PIN, OUTPUT);
    writeLaser(false);

    pinMode(LDR_LEFT, INPUT);
    pinMode(LDR_RIGHT, INPUT);

    ledcAttach(LEFT_PWM, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(RIGHT_PWM, PWM_FREQ, PWM_RESOLUTION);

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    myServo.setPeriodHertz(50);
    myServo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
    myServo.write(0);

    if (ENABLE_LOGS && isLogEnabled(LOG_SERVO))
    {
        Serial.println("[SERVO] Initialized at 0 degree");
    }

    stopMotors();

    Serial.print("Bluetooth ready. Pair with '");
    Serial.print(BT_DEVICE_NAME);
    Serial.println("'.");
    Serial.println("Control commands: fs, bs, ls, rs, ss");

    logMsg("=======================================");
    logMsg("RECEIVER READY");
    logMsg("=======================================");
}

////////////////////////////////////////////////////////////
// LOOP
////////////////////////////////////////////////////////////

void loop()
{
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0)
            handleLogCommand(cmd);
    }

    processBluetoothInput();

    bool hasBtClient = SerialBT.hasClient();
    if (controllerConnected && hasBtClient && (millis() - lastCommandTime > CONNECTION_TIMEOUT))
    {
        controllerConnected = false;

        logMsg("");
        logMsg("=======================================");
        logMsg("[WARNING] CONTROLLER TIMEOUT");
        logMsg("[ACTION] STOPPING ALL SYSTEMS");
        logMsg("=======================================");

        stopMotors();
        if (!laserManualOverride)
        {
            writeLaser(false);
        }
    }

    static unsigned long lastLDRLog = 0;

    int leftLDR = analogRead(LDR_LEFT);
    int rightLDR = analogRead(LDR_RIGHT);

    bool ldrTriggered = (leftLDR > LDR_THRESHOLD || rightLDR > LDR_THRESHOLD);

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
        stopMotors();
        if (!laserManualOverride)
        {
            writeLaser(false);
        }
        if (!servoManualHold)
        {
            myServo.write(90);
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

    delay(10);
}

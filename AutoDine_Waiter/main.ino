/*
 * AutoDine V4.0 — Waiter Robot Firmware
 * Board:   Arduino Uno
 * Motor:   Adafruit Motor Shield V2 (I2C)
 * Sensor:  HC-SR04  TRIG=7  ECHO=8
 * BT:      HC-05 on Serial (pins 0/1, 9600 baud)
 *
 * Commands (via Bluetooth from phone / chef):
 *   'G' = Go forward until obstacle < 30 cm, then stop
 *   'B' = Reverse same distance back to home
 *   'S' = Emergency stop
 *
 * Buzzer patterns handled on Host Unit (ESP32). Robot just drives.
 */

#include <Wire.h>
#include <Adafruit_MotorShield.h>

/* ---- Pins ---------------------------------------------------- */
#define TRIG_PIN  7
#define ECHO_PIN  8

/* ---- Constants ----------------------------------------------- */
#define OBSTACLE_CM      30     /* stop if ultrasonic reads < this  */
#define MOTOR_SPEED      180    /* 0-255                             */
#define PULSE_TIMEOUT_US 25000  /* ~430 cm max range                */

/* ---- Motor Shield -------------------------------------------- */
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_DCMotor *motorFL = NULL;  /* Front Left  — M1 */
Adafruit_DCMotor *motorFR = NULL;  /* Front Right — M2 */
Adafruit_DCMotor *motorRL = NULL;  /* Rear Left   — M3 */
Adafruit_DCMotor *motorRR = NULL;  /* Rear Right  — M4 */

/* ---- State --------------------------------------------------- */
enum RobotState { IDLE, GOING_FORWARD, REVERSING, STOPPED };
RobotState robotState = IDLE;

unsigned long steps_forward = 0;   /* ms spent going forward */
unsigned long move_start    = 0;

/* ================================================================
 *  Ultrasonic — returns distance in cm (0 = timeout / error)
 * ================================================================ */
long getDistanceCm() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, PULSE_TIMEOUT_US);
    if (duration == 0) return 999;          /* no echo = clear path */
    return duration / 58L;                  /* cm */
}

/* ================================================================
 *  Motor helpers
 * ================================================================ */
void set_all_motors(int dir, int speed) {
    motorFL->setSpeed(speed);
    motorFR->setSpeed(speed);
    motorRL->setSpeed(speed);
    motorRR->setSpeed(speed);
    motorFL->run(dir);
    motorFR->run(dir);
    motorRL->run(dir);
    motorRR->run(dir);
}

void drive_forward()  { set_all_motors(FORWARD,  MOTOR_SPEED); }
void drive_backward() { set_all_motors(BACKWARD, MOTOR_SPEED); }
void stop_motors()    { set_all_motors(RELEASE,  0); }

/* ================================================================
 *  setup
 * ================================================================ */
void setup() {
    Serial.begin(9600);   /* HC-05 Bluetooth */
    Serial.println("AutoDine Waiter Robot V4.0 ready");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    AFMS.begin();         /* I2C, default 1.6 kHz */

    motorFL = AFMS.getMotor(1);
    motorFR = AFMS.getMotor(2);
    motorRL = AFMS.getMotor(3);
    motorRR = AFMS.getMotor(4);

    stop_motors();
    robotState = IDLE;
    Serial.println("Motors initialised. Waiting for command (G/B/S)...");
}

/* ================================================================
 *  loop
 * ================================================================ */
void loop() {
    /* ---- Handle incoming Bluetooth commands -------------------- */
    if (Serial.available() > 0) {
        char cmd = (char)Serial.read();
        /* Flush remaining bytes in buffer */
        while (Serial.available()) Serial.read();

        if (cmd == 'G' || cmd == 'g') {
            if (robotState != GOING_FORWARD) {
                Serial.println("CMD: GO FORWARD");
                robotState   = GOING_FORWARD;
                steps_forward = 0;
                move_start    = millis();
                drive_forward();
            }
        } else if (cmd == 'B' || cmd == 'b') {
            if (robotState != REVERSING) {
                Serial.println("CMD: REVERSE");
                robotState = REVERSING;
                move_start = millis();
                drive_backward();
            }
        } else if (cmd == 'S' || cmd == 's') {
            Serial.println("CMD: EMERGENCY STOP");
            stop_motors();
            robotState = STOPPED;
        }
    }

    /* ---- State machine ----------------------------------------- */
    switch (robotState) {

        case GOING_FORWARD: {
            long dist = getDistanceCm();
            if (dist < OBSTACLE_CM) {
                stop_motors();
                steps_forward = millis() - move_start; /* record travel time */
                robotState    = IDLE;
                Serial.print("Obstacle! Stopped after ");
                Serial.print(steps_forward);
                Serial.println(" ms. Send 'B' to return.");
            }
            break;
        }

        case REVERSING: {
            unsigned long elapsed = millis() - move_start;
            if (elapsed >= steps_forward) {
                stop_motors();
                robotState    = IDLE;
                steps_forward = 0;
                Serial.println("Returned to home position.");
            }
            break;
        }

        case IDLE:
        case STOPPED:
        default:
            break;
    }

    delay(50); /* 20 Hz sensor poll rate */
}

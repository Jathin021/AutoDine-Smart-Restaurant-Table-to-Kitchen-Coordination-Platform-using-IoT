/*
 * WAITER ROBOT - CALIBRATED RETURN TIME FIX
 * Fixes: Wheels rotate 1 sec too long on return
 */

#include <AFMotor.h>
#include <SoftwareSerial.h>

#define TRIG_PIN A4
#define ECHO_PIN A5
#define BT_RX A2
#define BT_TX A3

AF_DCMotor motor1(1);
AF_DCMotor motor2(2);
AF_DCMotor motor3(3);
AF_DCMotor motor4(4);

#define MOTOR_SPEED 180
#define SAFE_DISTANCE 20
#define TABLE_WAIT_TIME 10000
#define ULTRASONIC_INTERVAL 50

// SEPARATE DURATIONS FOR FORWARD AND RETURN
#define DURATION_TABLE1_FORWARD 5200    // Forward to 100cm
#define DURATION_TABLE1_RETURN  5600    // Return from 100cm (1 sec less)
#define DURATION_TABLE2_FORWARD 7800    // Forward to 150cm
#define DURATION_TABLE2_RETURN  8200    // Return from 150cm (1 sec less)

enum RobotState {
  IDLE,
  COMMAND_RECEIVED,
  MOVE_FORWARD,
  OBSTACLE_WAIT_FORWARD,
  DESTINATION_REACHED,
  WAIT_AT_TABLE,
  MOVE_BACKWARD,
  OBSTACLE_WAIT_BACKWARD,
  HOME_REACHED
};

RobotState currentState = IDLE;
SoftwareSerial bluetooth(BT_RX, BT_TX);

unsigned long targetDuration = 0;
unsigned long returnDuration = 0;      // NEW: separate return duration
unsigned long moveStartTime = 0;
unsigned long elapsedBeforePause = 0;
unsigned long waitStartTime = 0;
unsigned long lastDistanceCheck = 0;

int currentDistance = 0;
char currentCommand = '0';

void setup() {
  Serial.begin(9600);
  bluetooth.begin(9600);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  motor1.setSpeed(MOTOR_SPEED);
  motor2.setSpeed(MOTOR_SPEED);
  motor3.setSpeed(MOTOR_SPEED);
  motor4.setSpeed(MOTOR_SPEED);
  stopMotors();
  
  Serial.println(F("===================================="));
  Serial.println(F("WAITER ROBOT - RETURN TIME FIXED"));
  Serial.println(F("===================================="));
  Serial.println(F("Send '1' for Table-1 (100cm)"));
  Serial.println(F("Send '2' for Table-2 (150cm)"));
  Serial.println(F("===================================="));
  
  delay(1000);
}

void loop() {
  if (millis() - lastDistanceCheck >= ULTRASONIC_INTERVAL) {
    currentDistance = readDistance();
    lastDistanceCheck = millis();
  }
  
  if (bluetooth.available()) {
    char cmd = bluetooth.read();
    if (cmd == '1' || cmd == '2') {
      handleCommand(cmd);
    }
  }
  
  executeStateMachine();
}

void executeStateMachine() {
  switch (currentState) {
    
    case IDLE:
      break;
      
    case COMMAND_RECEIVED:
      currentState = MOVE_FORWARD;
      moveStartTime = millis();
      elapsedBeforePause = 0;
      moveForward();
      Serial.print(F(">> Moving to Table-"));
      Serial.println(currentCommand);
      bluetooth.print("MOVING-T");
      bluetooth.println(currentCommand);
      break;
      
    case MOVE_FORWARD:
      if (isObstacleDetected()) {
        pauseMovement(true);
        currentState = OBSTACLE_WAIT_FORWARD;
        Serial.println(F(">> OBSTACLE - WAITING"));
        bluetooth.println("OBSTACLE");
      }
      else if (isDestinationReached()) {
        currentState = DESTINATION_REACHED;
        stopMotors();
        Serial.println(F(">> DESTINATION REACHED"));
        bluetooth.println("ARRIVED");
      }
      break;
      
    case OBSTACLE_WAIT_FORWARD:
      if (isObstacleCleared()) {
        resumeMovement(true);
        currentState = MOVE_FORWARD;
        Serial.println(F(">> RESUMING"));
        bluetooth.println("RESUME");
      }
      break;
      
    case DESTINATION_REACHED:
      currentState = WAIT_AT_TABLE;
      waitStartTime = millis();
      Serial.println(F(">> Waiting 10 seconds..."));
      bluetooth.println("WAITING-10s");
      break;
      
    case WAIT_AT_TABLE:
      if (millis() - waitStartTime >= TABLE_WAIT_TIME) {
        currentState = MOVE_BACKWARD;
        
        // USE SHORTER RETURN DURATION
        targetDuration = returnDuration;
        
        moveStartTime = millis();
        elapsedBeforePause = 0;
        moveBackward();
        Serial.print(F(">> Returning HOME ("));
        Serial.print(returnDuration);
        Serial.println(F(" ms)"));
        bluetooth.println("RETURNING");
      }
      break;
      
    case MOVE_BACKWARD:
      if (isObstacleDetected()) {
        pauseMovement(false);
        currentState = OBSTACLE_WAIT_BACKWARD;
        Serial.println(F(">> OBSTACLE on return"));
        bluetooth.println("OBSTACLE-RETURN");
      }
      else if (isDestinationReached()) {
        currentState = HOME_REACHED;
        stopMotors();
        Serial.println(F(">> HOME REACHED"));
        bluetooth.println("HOME");
      }
      break;
      
    case OBSTACLE_WAIT_BACKWARD:
      if (isObstacleCleared()) {
        resumeMovement(false);
        currentState = MOVE_BACKWARD;
        Serial.println(F(">> RESUMING return"));
        bluetooth.println("RESUME-RETURN");
      }
      break;
      
    case HOME_REACHED:
      currentState = IDLE;
      Serial.println(F("===================================="));
      Serial.println(F("Mission Complete - READY"));
      Serial.println(F("===================================="));
      bluetooth.println("READY");
      break;
  }
}

void handleCommand(char cmd) {
  if (currentState != IDLE) {
    Serial.println(F(">> BUSY"));
    bluetooth.println("BUSY");
    return;
  }
  
  currentCommand = cmd;
  
  if (cmd == '1') {
    targetDuration = DURATION_TABLE1_FORWARD;
    returnDuration = DURATION_TABLE1_RETURN;   // Set return duration
    Serial.println(F(">> CMD: Table-1"));
    Serial.print(F("   Forward: "));
    Serial.print(targetDuration);
    Serial.print(F(" ms | Return: "));
    Serial.print(returnDuration);
    Serial.println(F(" ms"));
    bluetooth.println("OK-T1");
  } 
  else if (cmd == '2') {
    targetDuration = DURATION_TABLE2_FORWARD;
    returnDuration = DURATION_TABLE2_RETURN;   // Set return duration
    Serial.println(F(">> CMD: Table-2"));
    Serial.print(F("   Forward: "));
    Serial.print(targetDuration);
    Serial.print(F(" ms | Return: "));
    Serial.print(returnDuration);
    Serial.println(F(" ms"));
    bluetooth.println("OK-T2");
  }
  
  currentState = COMMAND_RECEIVED;
}

void moveForward() {
  motor1.run(FORWARD);
  motor2.run(FORWARD);
  motor3.run(BACKWARD);
  motor4.run(BACKWARD);
}

void moveBackward() {
  motor1.run(BACKWARD);
  motor2.run(BACKWARD);
  motor3.run(FORWARD);
  motor4.run(FORWARD);
}

void stopMotors() {
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
}

bool isObstacleDetected() {
  return (currentDistance > 0 && currentDistance < SAFE_DISTANCE);
}

bool isObstacleCleared() {
  return (currentDistance >= SAFE_DISTANCE || currentDistance == 0);
}

bool isDestinationReached() {
  return ((millis() - moveStartTime) >= targetDuration);
}

void pauseMovement(bool isForward) {
  elapsedBeforePause = millis() - moveStartTime;
  stopMotors();
}

void resumeMovement(bool isForward) {
  moveStartTime = millis() - elapsedBeforePause;
  if (isForward) {
    moveForward();
  } else {
    moveBackward();
  }
}

int readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  if (duration == 0) {
    return 0;
  }
  
  int distance = duration * 0.034 / 2;
  
  if (distance < 2 || distance > 400) {
    return 0;
  }
  
  return distance;
}

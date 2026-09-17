/*
  Stone Pickup Code - fixed / restructured
  -----------------------------------------
  Changelog vs your original file (see chat for full explanation):
   - Fixed several lines that would not compile (missing ';', "printIn" typo,
     a broken if/else brace, an uninitialized variable declaration).
   - Removed the blocking `while` loops that never re-read the HuskyLens, which
     caused box_size / xCenter to go stale -> effectively infinite loops.
   - Removed the delay(5000) at the end of loop() (it froze the robot for 5s
     every cycle).
   - Rebuilt the logic as a simple state machine (SEARCHING -> CENTERING ->
     APPROACHING -> PICKING) that re-reads the HuskyLens every single loop(),
     so it's non-blocking and reacts every cycle.
   - Added real Servo control for the two gripper arms (close_servo was only
     a variable before, nothing ever moved).
   - Added placeholder motor-driver functions (forward/turnLeft/turnRight/stop)
     since your original turn()/forward() were just comments. PINS AND LOGIC
     HERE ARE GUESSES based on a generic 2-motor driver (e.g. L298N/TB6612)
     - change them to match your actual wiring.
   - When multiple stones are visible at once (very likely with a scattered
     flock of 6 colors), the code now targets the single LARGEST box (i.e.
     nearest stone) each cycle instead of whatever HuskyLens happens to
     return first.
   - Stores the picked stone's color ID in `color_id` so your placing code
     can read it later.
*/

#include <HUSKYLENS.h>
#include <HuskyLensProtocolCore.h>   // pulled in by HUSKYLENS.h already on most installs; harmless to keep
#include <HUSKYLENSMindPlus.h>       // same as above
#include <Wire.h>
#include <Servo.h>

// NOTE: removed <SoftwareSerial.h> (unused - you talk to HuskyLens over I2C/Wire)
// and removed <DFRobot_HuskyLens.h> (unused - your `huskylens` object is type
// HUSKYLENS from HUSKYLENS.h, not DFRobot_HuskyLens; having both libraries
// included can cause symbol clashes / bloat for no benefit).

HUSKYLENS huskylens;

// ---------------- Servo (gripper) setup ----------------
// TODO: set these to the pins you actually wired the two arm servos to.
const int LEFT_SERVO_PIN  = 5;
const int RIGHT_SERVO_PIN = 6;

// TODO: calibrate these angles on your actual gripper geometry.
const int LEFT_ARM_OPEN_ANGLE   = 60;
const int LEFT_ARM_CLOSE_ANGLE  = 130;
const int RIGHT_ARM_OPEN_ANGLE  = 130;
const int RIGHT_ARM_CLOSE_ANGLE = 60;

Servo leftArmServo;
Servo rightArmServo;

// ---------------- Motor driver setup ----------------
// TODO: these pins/this logic are a generic placeholder for a 2-motor
// H-bridge driver (L298N / TB6612-style). Replace with your real driver's
// pins and logic.
// IMPORTANT: the Servo library uses Timer1 on an Uno/Nano. That disables
// analogWrite() PWM on pins 9 and 10 while ANY servo is attached - so the
// motor speed (PWM) pins below deliberately avoid 9 and 10. Don't reuse
// 9/10 for analogWrite elsewhere in your code.
const int MOTOR_LEFT_IN1  = 4;
const int MOTOR_LEFT_IN2  = 7;
const int MOTOR_RIGHT_IN1 = 8;
const int MOTOR_RIGHT_IN2 = 12;
const int MOTOR_LEFT_PWM  = 3;   // ENA
const int MOTOR_RIGHT_PWM = 11;  // ENB

const int DRIVE_SPEED = 150; // 0-255, tune for your motors
const int TURN_SPEED  = 120; // 0-255, tune for your motors

// ---------------- HuskyLens frame / target selection ----------------
const int FRAME_CENTER_MIN = 120;   // left edge of the "centered" band
const int FRAME_CENTER_MAX = 200;   // right edge of the "centered" band

const int MAX_BLOCKS = 10;
HUSKYLENSResult blocks[MAX_BLOCKS];
int blockCount = 0;

// ---------------- Pickup state machine ----------------
enum PickupState { SEARCHING, CENTERING, APPROACHING, PICKING };
PickupState pickupState = SEARCHING;

bool close_servo = false;   // reflects current gripper state (false = open)
int  color_id = -1;         // color ID of the stone we just picked, for placing mode

bool pick_up_mode  = true;
bool placing_mode  = false;

long minimum_box_size = 3000;  // TODO: calibrate - the width*height at which the arms can reach the stone

void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(MOTOR_LEFT_IN1, OUTPUT);
  pinMode(MOTOR_LEFT_IN2, OUTPUT);
  pinMode(MOTOR_RIGHT_IN1, OUTPUT);
  pinMode(MOTOR_RIGHT_IN2, OUTPUT);
  pinMode(MOTOR_LEFT_PWM, OUTPUT);
  pinMode(MOTOR_RIGHT_PWM, OUTPUT);
  stopMotors();

  leftArmServo.attach(LEFT_SERVO_PIN);
  rightArmServo.attach(RIGHT_SERVO_PIN);
  openArms();

  Serial.println("Connecting to HuskyLens...");
  while (!huskylens.begin(Wire)) {
    Serial.println("HuskyLens connection failed!");
    delay(1000);
  }
  Serial.println("HuskyLens connected!");

  // Make sure the sensor is actually in color-recognition mode, since the
  // IDs/names below only make sense in that algorithm.
  huskylens.writeAlgorithm(ALGORITHM_COLOR_RECOGNITION);

  setNameWithRetry("Orange", 1);
  setNameWithRetry("Blue", 2);
  setNameWithRetry("Purple", 3);
  setNameWithRetry("Green", 4);
  setNameWithRetry("Cyan", 5);
  setNameWithRetry("Red", 6);

  // Optional: rush into the pile once to scatter the stones. Blocking here
  // is fine since it's a one-off move before the main loop starts.
  // startCeremony();
}

void loop() {
  if (pick_up_mode) {
    readHuskyLens();
    int targetIdx = findLargestBlock();
    runPickupState(targetIdx);
  }

  if (placing_mode) {
    // TODO: placing code goes here (next phase - not covered yet).
  }
}

// ----------------------------------------------------------------------
// Reads one fresh frame from HuskyLens and fills `blocks[]` /`blockCount`
// with every COMMAND_RETURN_BLOCK result. Called once per loop() so the
// state machine below is always acting on current data.
// ----------------------------------------------------------------------
void readHuskyLens() {
  blockCount = 0;

  if (!huskylens.request()) {
    Serial.println("Request failed!");
    return;
  }

  while (huskylens.available() && blockCount < MAX_BLOCKS) {
    HUSKYLENSResult r = huskylens.read();
    if (r.command == COMMAND_RETURN_BLOCK) {
      blocks[blockCount] = r;
      blockCount++;
    }
  }
}

// Picks the biggest (= nearest) detected stone this frame, since several
// of the 6 colors will often be visible at once in a scattered flock.
int findLargestBlock() {
  int bestIdx = -1;
  long bestSize = -1;
  for (int i = 0; i < blockCount; i++) {
    long size = (long)blocks[i].width * (long)blocks[i].height;
    if (size > bestSize) {
      bestSize = size;
      bestIdx = i;
    }
  }
  return bestIdx;
}

// ----------------------------------------------------------------------
// One tick of the pickup state machine. targetIdx is -1 if no block was
// seen this frame, otherwise it indexes into blocks[].
// ----------------------------------------------------------------------
void runPickupState(int targetIdx) {
  switch (pickupState) {

    case SEARCHING:
      openArms();
      if (targetIdx == -1) {
        // หันจนกว่าจะเจอ - turn until a stone is found
        turnSearch();
      } else {
        Serial.println("Stone detected!");
        stopMotors();
        pickupState = CENTERING;
      }
      break;

    case CENTERING:
      if (targetIdx == -1) {
        // lost the stone (moved out of frame) - go back to searching
        pickupState = SEARCHING;
        break;
      }
      {
        int xC = blocks[targetIdx].xCenter;
        if (xC < FRAME_CENTER_MIN) {
          // TODO: double-check this matches your camera's left/right
          // mounting - swap turnLeft()/turnRight() here if it turns the
          // wrong way on the real robot.
          turnRight();
        } else if (xC > FRAME_CENTER_MAX) {
          turnLeft();
        } else {
          stopMotors();
          pickupState = APPROACHING;
        }
      }
      break;

    case APPROACHING:
      if (targetIdx == -1) {
        pickupState = SEARCHING;
        break;
      }
      {
        int xC = blocks[targetIdx].xCenter;
        // if it drifted off-center while we were driving forward, recenter first
        if (xC < FRAME_CENTER_MIN || xC > FRAME_CENTER_MAX) {
          pickupState = CENTERING;
          break;
        }

        long boxSize = (long)blocks[targetIdx].width * (long)blocks[targetIdx].height;
        if (boxSize >= minimum_box_size) {
          stopMotors();
          pickupState = PICKING;
        } else {
          Serial.println("Still can't pick up the stone, need to go closer!");
          forward();
        }
      }
      break;

    case PICKING: {
      color_id = blocks[targetIdx].ID;

      Serial.print("Picking stone, ID = ");
      Serial.println(color_id);
      printColorName(color_id);

      Serial.print("Block: x=");
      Serial.print(blocks[targetIdx].xCenter);
      Serial.print(", y=");
      Serial.print(blocks[targetIdx].yCenter);
      Serial.print(", width=");
      Serial.print(blocks[targetIdx].width);
      Serial.print(", height=");
      Serial.print(blocks[targetIdx].height);
      Serial.print(", box_size=");
      Serial.println((long)blocks[targetIdx].width * (long)blocks[targetIdx].height);

      Serial.println("Closing arms!");
      closeArms();
      delay(400); // TODO: calibrate - give the servos time to actually close before driving off

      Serial.println("Switching to placing mode!");
      pick_up_mode  = false;
      placing_mode  = true;
      pickupState   = SEARCHING; // reset, ready for the next pickup cycle later
      break;
    }
  }
}

void printColorName(int id) {
  switch (id) {
    case 1: Serial.println("Orange"); break;
    case 2: Serial.println("Blue");   break;
    case 3: Serial.println("Purple"); break;
    case 4: Serial.println("Green");  break;
    case 5: Serial.println("Cyan");   break;
    case 6: Serial.println("Red");    break;
    default: Serial.println("Unknown"); break;
  }
}

void setNameWithRetry(const char* name, int id) {
  while (!huskylens.setCustomName(name, id)) {
    Serial.print("Retrying custom name for ID ");
    Serial.println(id);
    delay(100);
  }
}

// ---------------- Gripper control ----------------
void openArms() {
  leftArmServo.write(LEFT_ARM_OPEN_ANGLE);
  rightArmServo.write(RIGHT_ARM_OPEN_ANGLE);
  close_servo = false;
}

void closeArms() {
  leftArmServo.write(LEFT_ARM_CLOSE_ANGLE);
  rightArmServo.write(RIGHT_ARM_CLOSE_ANGLE);
  close_servo = true;
}

// ---------------- Motor control (placeholder - adjust to your driver) ----------------
void forward() {
  digitalWrite(MOTOR_LEFT_IN1, HIGH);  digitalWrite(MOTOR_LEFT_IN2, LOW);
  digitalWrite(MOTOR_RIGHT_IN1, HIGH); digitalWrite(MOTOR_RIGHT_IN2, LOW);
  analogWrite(MOTOR_LEFT_PWM, DRIVE_SPEED);
  analogWrite(MOTOR_RIGHT_PWM, DRIVE_SPEED);
}

void turnLeft() {
  digitalWrite(MOTOR_LEFT_IN1, LOW);   digitalWrite(MOTOR_LEFT_IN2, HIGH);
  digitalWrite(MOTOR_RIGHT_IN1, HIGH); digitalWrite(MOTOR_RIGHT_IN2, LOW);
  analogWrite(MOTOR_LEFT_PWM, TURN_SPEED);
  analogWrite(MOTOR_RIGHT_PWM, TURN_SPEED);
}

void turnRight() {
  digitalWrite(MOTOR_LEFT_IN1, HIGH);  digitalWrite(MOTOR_LEFT_IN2, LOW);
  digitalWrite(MOTOR_RIGHT_IN1, LOW);  digitalWrite(MOTOR_RIGHT_IN2, HIGH);
  analogWrite(MOTOR_LEFT_PWM, TURN_SPEED);
  analogWrite(MOTOR_RIGHT_PWM, TURN_SPEED);
}

void stopMotors() {
  digitalWrite(MOTOR_LEFT_IN1, LOW);  digitalWrite(MOTOR_LEFT_IN2, LOW);
  digitalWrite(MOTOR_RIGHT_IN1, LOW); digitalWrite(MOTOR_RIGHT_IN2, LOW);
  analogWrite(MOTOR_LEFT_PWM, 0);
  analogWrite(MOTOR_RIGHT_PWM, 0);
}

void turnSearch() {
  // หันจนกว่าจะเจอ - simple constant-direction search turn.
  // TODO: consider a sweep pattern (turn a bit, pause, re-check) instead of
  // spinning continuously, so HuskyLens has time to lock onto a stone.
  turnRight();
}

// Optional one-time startup move: rush into the pile to scatter stones.
// Blocking is OK here since it only runs once, before loop() takes over.
void startCeremony() {
  forward();
  delay(1200); // TODO: tune distance/duration for your field
  stopMotors();
}

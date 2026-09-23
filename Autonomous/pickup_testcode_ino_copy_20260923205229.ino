#include <HUSKYLENS.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include  <InEngMotor.h>
//#include <WiFi.h>
//#include <WiFiUdp.h>

HUSKYLENS huskylens;

// ---------------- WiFi / UDP ----------------
// const char* WIFI_SSID     = "YOUR_WIFI_NAME";      // TODO
// const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";  // TODO

// const unsigned int LOCAL_UDP_PORT = 4210;  // this ESP32 listens here for nav commands
// const char* LAPTOP_IP   = "192.168.1.20";  // TODO: your laptop's IP (check with ipconfig/ifconfig)
// const unsigned int LAPTOP_PORT = 4211;     // must match LISTEN_PORT in field_vision.py

// IPAddress local_IP(192, 168, 1, 50);   // the fixed IP you want the ESP32 to have
// IPAddress gateway(192, 168, 1, 1);     // your router's IP (see below)
// IPAddress subnet(255, 255, 255, 0);    // usually this for small networks


// Tip: WiFi routers hand out IPs by DHCP, which can change between matches.
// If possible, reserve a static IP for both the laptop and the ESP32 on
// your router (or use WiFi.config() below) so you don't have to re-check
// and re-flash these addresses before every run.

// WiFiUDP udp;
// char packetBuffer[64];

// ---------------- Servo (gripper) ----------------
// Recommended ESP32 PWM-capable pins: 2,4,12-19,21-23,25-27,32-33
const int LEFT_SERVO_PIN  = 19;   // TODO match your wiring
const int RIGHT_SERVO_PIN = 32;

const int LEFT_ARM_OPEN_ANGLE   = 0;
const int LEFT_ARM_CLOSE_ANGLE  = 99;
const int RIGHT_ARM_OPEN_ANGLE  = 180;
const int RIGHT_ARM_CLOSE_ANGLE = 81;

Servo leftArmServo;
Servo rightArmServo;

// ---------------- Motor driver (placeholder - adjust to your driver) ----------------
/*
const int MOTOR_LEFT_IN1  = 26;
const int MOTOR_LEFT_IN2  = 27;
const int MOTOR_RIGHT_IN1 = 32;
const int MOTOR_RIGHT_IN2 = 33;
const int MOTOR_LEFT_PWM  = 25;
const int MOTOR_RIGHT_PWM = 4;
*/

const int DRIVE_SPEED = 50;  // 0-255, tune for your motors
const int TURN_SPEED  = 50;  // 0-255, tune for your motors

// ---------------- HuskyLens frame / target selection ----------------
const int FRAME_CENTER_MIN = 120;
const int FRAME_CENTER_MAX = 200;

// Create an array that hold up maximum box of detection results
const int MAX_BLOCKS = 10;
HUSKYLENSResult blocks[MAX_BLOCKS];
int blockCount = 0;

// Type of exactly 4 possible values
enum PickupState { SEARCHING, CENTERING, APPROACHING, PICKING };
PickupState pickupState = SEARCHING;

bool close_servo = true;   // reflects current gripper state (false = open)
int  color_id = -1;         // color ID of the stone currently held
bool pick_up_mode = true;
bool placing_mode = false;
//bool needToAnnouncePickup = false;

long minimum_box_size = 6800;  // TODO calibrate

// ---------------- Placing mode ----------------
// String lastNavCommand = "";
// unsigned long lastNavPacketTime = 0;
// const unsigned long NAV_TIMEOUT_MS = 1000;  // stop driving if the laptop goes quiet
// ---------------- ------------- ---------------


void stopMotors() {
  inengmotor.stop(); 
}

void turnSearch() {
  turnRight();  // simple constant-direction search; could add a sweep pattern later
}

// ---------------- Motor control (placeholder - adjust to your driver) ----------------
void forward() {
  inengmotor.forward(200, 200); 
}

void backward() {
  inengmotor.backward(200, 200); 

}

void turnLeft() {
  inengmotor.turnLeft(80, 150);
}

void turnRight() {
  inengmotor.turnRight(180, 80);
}


void setup() {
  Serial.begin(115200);
  Wire.begin();

  /*
  pinMode(MOTOR_LEFT_IN1, OUTPUT);
  pinMode(MOTOR_LEFT_IN2, OUTPUT);
  pinMode(MOTOR_RIGHT_IN1, OUTPUT);
  pinMode(MOTOR_RIGHT_IN2, OUTPUT);
  */
  stopMotors();

  leftArmServo.attach(LEFT_SERVO_PIN);
  rightArmServo.attach(RIGHT_SERVO_PIN);

  closeArms();
  delay(5000);
  openArms();
  delay(5000);
  stopMotors();

  Serial.println("Connecting to HuskyLens...");
  while (!huskylens.begin(Wire)) {
    Serial.println("HuskyLens connection failed!");
    delay(1000);
  }
  Serial.println("HuskyLens connected!");
  huskylens.writeAlgorithm(ALGORITHM_COLOR_RECOGNITION);

  setNameWithRetry("Orange", 1);
  setNameWithRetry("Blue", 2);
  setNameWithRetry("Purple", 3);
  setNameWithRetry("Green", 4);
  setNameWithRetry("Cyan", 5);
  setNameWithRetry("Red", 6);

  /*
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP config failed!");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());  // should now print 192.168.1.50 every time
}

  Serial.println();
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());  // handy to confirm WiFi connected

  udp.begin(LOCAL_UDP_PORT);
*/
  // วิ่งชนหินเหมือน snooker
  //forward();
  //delay(5000);
  //stopMotors();
  //turnRight();
  //delay(2000);

}

void loop() {
  if (pick_up_mode) {
    readHuskyLens();
    int targetIdx = findLargestBlock();
    runPickupState(targetIdx);
  }
/*
  if (placing_mode) {
    runPlacingState();
  }
  */
}

// ============================================================
// Pickup mode 
// ============================================================

void readHuskyLens() {
  blockCount = 0;
  if (!huskylens.request()) {
    Serial.println("Request failed!");
    return;
  }
  while (huskylens.available() && blockCount < MAX_BLOCKS) {
    HUSKYLENSResult r = huskylens.read();
    // get a struct of detected block info

    if (r.command == COMMAND_RETURN_BLOCK) {
      blocks[blockCount] = r;
      blockCount++;
    }
  }
}

int findLargestBlock() {
  int bestIdx = -1;
  long bestSize = -1;
  for (int i = 0; i < blockCount; i++) {
    // find largest detected block by it's area
    long size = (long)blocks[i].width * (long)blocks[i].height;
    if (size > bestSize) {
      bestSize = size;
      bestIdx = i;
    }
  }
  return bestIdx;
}

void runPickupState(int targetIdx) {
  switch (pickupState) {
    
// to do
    case SEARCHING:
    Serial.println("Searching state.");
      openArms();
      if (targetIdx == -1) {
        Serial.println("Still searching.");
        turnSearch();
      } else {
        Serial.println("Stone detected!");
        stopMotors();
        pickupState = CENTERING;
      }
      break;

    case CENTERING:
    Serial.println("Centering state.");
    // if target block disappear
      if (targetIdx == -1) {
        Serial.println("Centering failed.");
        pickupState = SEARCHING;
        break;
      }
      {
        int xC = blocks[targetIdx].xCenter;
        if (xC < FRAME_CENTER_MIN) {
          Serial.println("Turning Right");
          turnRight();  // TODO: verify direction matches your camera mount
        } else if (xC > FRAME_CENTER_MAX) {
          Serial.println("Turning Left");
          turnLeft();
        } else {
          Serial.println("Object is at center.");
          stopMotors();
          pickupState = APPROACHING;
        }
      }
      break;

    case APPROACHING:
    Serial.println("Approaching state");
      if (targetIdx == -1) {
        pickupState = SEARCHING;
        break;
      }
      {
        int xC = blocks[targetIdx].xCenter;
        if (xC < FRAME_CENTER_MIN || xC > FRAME_CENTER_MAX) {
          pickupState = CENTERING;
          break;
        }
        long boxSize = (long)blocks[targetIdx].width * (long)blocks[targetIdx].height;
        if (boxSize >= minimum_box_size) {
          stopMotors();
          pickupState = PICKING;
        } else {
          forward();
          Serial.println("Moving closer to the stone.");
          
        }
      }
      break;

    case PICKING: {
      Serial.println("Picking state");
      color_id = blocks[targetIdx].ID;
      Serial.print("Picking stone, ID = ");
      Serial.println(color_id);
      printColorName(color_id);

      Serial.println("Closing arms!");
      closeArms();
      delay(2000);  // let the servos actually finish closing before driving off
      stopMotors();

      Serial.println("Switching to placing mode!");
      //pick_up_mode = false;
      //placing_mode = true;
      //needToAnnouncePickup = true;
      pickupState = SEARCHING;  // reset, ready for the next pickup cycle later
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
void closeArms() {
  rightArmServo.write(RIGHT_ARM_CLOSE_ANGLE);
  leftArmServo.write(LEFT_ARM_CLOSE_ANGLE);
  close_servo = false;
}

void openArms() {
  rightArmServo.write(RIGHT_ARM_OPEN_ANGLE);
  leftArmServo.write(LEFT_ARM_OPEN_ANGLE);
  close_servo = true;
}


// ============================================================
// Placing mode - driven by UDP commands from field_vision.py
// ============================================================

/*
void runPlacingState() {
  if (needToAnnouncePickup) {
    udp.beginPacket(LAPTOP_IP, LAPTOP_PORT);
    udp.print("PICKED,");
    udp.print(color_id);
    udp.endPacket();
    needToAnnouncePickup = false;
    lastNavPacketTime = millis();  // don't immediately trip the timeout below
  }

  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) packetBuffer[len] = '\0';
    lastNavCommand = String(packetBuffer);
    lastNavPacketTime = millis();
  }

  // Safety: if the laptop goes quiet (WiFi hiccup, script crashed, etc.),
  // stop rather than keep blindly executing the last command forever.
  if (millis() - lastNavPacketTime > NAV_TIMEOUT_MS) {
    stopMotors();
    return;
  }

  if (lastNavCommand == "TURN_LEFT") {
    turnLeft();
  } else if (lastNavCommand == "TURN_RIGHT") {
    turnRight();
  } else if (lastNavCommand == "FORWARD") {
    forward();
  } else if (lastNavCommand == "STOP") {
    stopMotors();
  } else if (lastNavCommand == "ARRIVED") {
    stopMotors();
    Serial.println("Arrived at zone, releasing stone!");
    openArms();
    delay(2000);  // let the servos actually finish opening
    // move back so the arm don't sweep the stone out of area
    stopMotors();
    backward();
    delay(2000);
    stopMotors();

    lastNavCommand = "";
    color_id = -1;
    placing_mode = false;
    pick_up_mode = true;
    pickupState = SEARCHING;
  }
}
*/
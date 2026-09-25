#include <HUSKYLENS.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <InEngMotor.h>
#include <WiFi.h>
#include <WiFiUdp.h>

HUSKYLENS huskylens;

const char *ssid = "RedRobotINWZA";
const char *password = "GUJAPENBALAEW67"; // รหัสผ่านต้องมากกว่า 8 ตัวอักษร หรือปล่อยว่างหากไม่ต้องการตั้งรหัส

WiFiUDP udp;

const unsigned int LOCAL_UDP_PORT = 4210;
const unsigned int LAPTOP_PORT = 4211;

IPAddress laptopIP;
bool laptopKnown = false;

const int LEFT_SERVO_PIN  = 19;   
const int RIGHT_SERVO_PIN = 32;

const int LEFT_ARM_OPEN_ANGLE   = 0;
const int LEFT_ARM_CLOSE_ANGLE  = 99;
const int RIGHT_ARM_OPEN_ANGLE  = 180;
const int RIGHT_ARM_CLOSE_ANGLE = 81;

Servo leftArmServo;
Servo rightArmServo;

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

long minimum_box_size = 6800;  // TODO calibrate

// ---------------- Placing mode ----------------
String lastNavCommand = "";
unsigned long lastNavPacketTime = 0;
const unsigned long NAV_TIMEOUT_MS = 1000;  // stop driving if the laptop goes quiet mid-drive

// UDP can drop packets, so PICKED and PLACED are each resent on a timer
// until the laptop acknowledges them - a single fire-and-forget send is
// what let one lost packet freeze the whole run before.
const unsigned long RESEND_PERIOD_MS = 300;   // how often to repeat PICKED/PLACED until acked
const unsigned long PLACED_GIVEUP_MS = 3000;  // the stone is already physically released by the
                                               // time we send PLACED, so if no ack comes back in
                                               // time, just resume pickup instead of waiting forever

bool waitingForPickedAck = false;
unsigned long lastPickedSendTime = 0;

bool waitingForPlacedAck = false;
unsigned long lastPlacedSendTime = 0;
unsigned long placedWaitStart = 0;
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
  inengmotor.turnLeft(80, 180);
}

void turnRight() {
  inengmotor.turnRight(180, 80);
}

void spinLeft() {
  inengmotor.spinLeft(80, 80);
}

void spinRight() {
  inengmotor.spinRight(80, 80);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  leftArmServo.setPeriodHertz(50);
  rightArmServo.setPeriodHertz(50);
  leftArmServo.attach(LEFT_SERVO_PIN);
  rightArmServo.attach(RIGHT_SERVO_PIN);

  closeArms();
  delay(5000);
  openArms();
  delay(5000);

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

  inengmotor.begin();
  stopMotors();

  // ตั้งค่าโหมดให้เป็น Access Point
  WiFi.mode(WIFI_AP);

  // เริ่มต้นปล่อยสัญญาณ Wi-Fi
  bool result = WiFi.softAP(ssid, password);
  
  if (result) {
    Serial.println("SoftAP ตั้งค่าสำเร็จ!");
    Serial.print("IP Address ของ ESP32: ");
    Serial.println(WiFi.softAPIP()); // ค่าเริ่มต้นมักเป็น 192.168.4.1
  } else {
    Serial.println("SoftAP ตั้งค่าไม่สำเร็จ!");
  }

  udp.begin(LOCAL_UDP_PORT);

  Serial.print("UDP listening on port ");
  Serial.println(LOCAL_UDP_PORT);


  /* set this in InEngMotor.cpp
  void InEngMotor::begin() {
  // Arduino-ESP32 3.x LEDC API:
  // ledcAttachChannel(pin, frequency, resolution, channel)
  ledcAttachChannel(_motorAIn1, _pwmFrequency, _pwmResolution, 4);
  ledcAttachChannel(_motorAIn2, _pwmFrequency, _pwmResolution, 5);
  ledcAttachChannel(_motorBIn1, _pwmFrequency, _pwmResolution, 6);
  ledcAttachChannel(_motorBIn2, _pwmFrequency, _pwmResolution, 7);
  */
  
  // วิ่งชนหินเหมือน snooker
  //forward();
  //delay(5000);
  //stopMotors();
  //turnRight();
  //delay(2000);

}

void loop() {

  receiveUDP();
  checkNavigationTimeout();


  if (pick_up_mode) {
    readHuskyLens();
    int targetIdx = findLargestBlock();
    runPickupState(targetIdx);
  }

  if (placing_mode) {
    runPlacingState();
  }
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
    Serial.println("X: ");
    Serial.println(blocks[targetIdx].xCenter);
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
      pick_up_mode = false;
      placing_mode = true;
      pickupState = SEARCHING;  // reset, ready for the next pickup cycle later

      // Clear out anything left over from a previous cycle (e.g. a stale
      // "ARRIVED" from before), then start announcing the pickup -
      // runPlacingState() resends "PICKED,<id>" every loop until acked.
      lastNavCommand = "";
      waitingForPickedAck = true;
      lastPickedSendTime = 0;
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


void runPlacingState() {
  // Step 1: make sure the laptop actually knows we're holding a stone.
  // Keep repeating "PICKED,<id>" until it replies "ACK_PICKED" - no ack
  // means the packet may never have arrived.
  if (waitingForPickedAck) {
    if (millis() - lastPickedSendTime > RESEND_PERIOD_MS) {
      sendUDP("PICKED," + String(color_id));
      lastPickedSendTime = millis();
    }
    return;  // don't act on nav commands until the handshake is done
  }

  if (lastNavCommand == "TURN_LEFT") {
    turnLeft();
  } else if (lastNavCommand == "TURN_RIGHT") {
    turnRight();
  } else if (lastNavCommand == "FORWARD") {
    forward();
  } else if (lastNavCommand == "STOP") {
    stopMotors();
  } else if (lastNavCommand == "ARRIVED" && !waitingForPlacedAck) {
    stopMotors();
    Serial.println("Arrived at zone, releasing stone!");
    openArms();
    delay(2000);  // let the servos actually finish opening
    backward();   // move back so the arm doesn't sweep the stone out of the zone
    delay(2000);
    stopMotors();

    waitingForPlacedAck = true;
    placedWaitStart = millis();
    lastPlacedSendTime = 0;  // send the first "PLACED" immediately, below
  }

  // Step 2: same idea in reverse - keep telling the laptop the stone is
  // placed until it acknowledges, so a lost packet can't leave the laptop
  // stuck in "RELEASING" forever (which would silently break the *next*
  // pickup's navigation too).
  if (waitingForPlacedAck) {
    if (millis() - placedWaitStart > PLACED_GIVEUP_MS) {
      Serial.println("No ACK_PLACED - resuming pickup anyway (stone is already released).");
      finishPlacing();
    } else if (millis() - lastPlacedSendTime > RESEND_PERIOD_MS) {
      sendUDP("PLACED," + String(color_id));
      lastPlacedSendTime = millis();
    }
  }
}

void finishPlacing() {
  waitingForPlacedAck = false;
  lastNavCommand = "";
  color_id = -1;
  placing_mode = false;
  pick_up_mode = true;
  pickupState = SEARCHING;
}

void receiveUDP() {
  int packetSize = udp.parsePacket();

  if (packetSize <= 0) {
    return;

  char packet[64];

  int len = udp.read(packet, sizeof(packet) - 1);

  if (len <= 0) {
    return;
  }

  packet[len] = '\0';

  String command = String(packet);
  command.trim();

  // Remember the laptop's IP address
  laptopIP = udp.remoteIP();
  laptopKnown = true;

  Serial.print("Received UDP: ");
  Serial.println(command);

  if (command == "HELLO") {
    udp.beginPacket(laptopIP, LAPTOP_PORT);
    udp.print("HELLO_ACK");
    udp.endPacket();

    Serial.println("Sent HELLO_ACK");
  }

  else if (command == "ACK_PICKED") {
    waitingForPickedAck = false;
  }

  else if (command == "ACK_PLACED") {
    finishPlacing();
  }

  else {
    // Navigation command
    lastNavCommand = command;
    lastNavPacketTime = millis();
  }
}

void sendUDP(const String &message) {
  if (!laptopKnown) {
    return;
  }

  udp.beginPacket(laptopIP, LAPTOP_PORT);
  udp.print(message);
  udp.endPacket();

  Serial.print("Sent UDP: ");
  Serial.println(message);
}

void checkNavigationTimeout() {

  if (!placing_mode) {
    return;
  }

  if (millis() - lastNavPacketTime > NAV_TIMEOUT_MS) {
    lastNavCommand = "";
    stopMotors();
  }
}

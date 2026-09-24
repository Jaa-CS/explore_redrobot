#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>

// --- ตั้งค่า Wi-Fi ---
const char* ssid = "iQOO15";
const char* password = "thanwin2550";

WiFiUDP udp;
const unsigned int localPort = 4210; 

// --- พินขับมอเตอร์ DC (ล้อรถ) ---
const int leftPin1 = 26;
const int leftPin2 = 27;
const int rightPin1 = 17;
const int rightPin2 = 16;

// --- พิน Servo (ก้ามปู Gripper) ---
const int LEFT_SERVO_PIN  = 19;   
const int RIGHT_SERVO_PIN = 32;

// เปลี่ยนชื่อตัวแปร Servo ให้ไม่งงกับล้อรถ
Servo leftGripper;
Servo rightGripper;

// *ตั้งค่าองศาการหนีบและปล่อย*
const int LEFT_OPEN_ANGLE = 9;
const int LEFT_CLOSE_ANGLE = 60;
const int RIGHT_OPEN_ANGLE = 171;
const int RIGHT_CLOSE_ANGLE = 120;

void controlRobot(String cmd, int leftSpeed, int rightSpeed) {
  leftSpeed = constrain(leftSpeed, 0, 255);
  rightSpeed = constrain(rightSpeed, 0, 255);

  // --- เช็คคำสั่งสำหรับก้ามปู Gripper ---
  if (cmd == "G_ON") {
    Serial.println(">> Gripper: GRIP (G_ON)");
    leftGripper.write(LEFT_CLOSE_ANGLE);
    rightGripper.write(RIGHT_CLOSE_ANGLE);
    return; 
  } 
  else if (cmd == "G_OFF") {
    Serial.println(">> Gripper: RELEASE (G_OFF)");
    leftGripper.write(LEFT_OPEN_ANGLE);
    rightGripper.write(RIGHT_OPEN_ANGLE);
    return; 
  }

  // --- เช็คคำสั่งสำหรับล้อรถ ---
  if (cmd == "F") {
    analogWrite(leftPin1, leftSpeed);
    analogWrite(leftPin2, 0);     
    analogWrite(rightPin1, rightSpeed);
    analogWrite(rightPin2, 0);
  } 
  else if (cmd == "B") {
    analogWrite(leftPin1, 0);
    analogWrite(leftPin2, leftSpeed);
    analogWrite(rightPin1, 0);
    analogWrite(rightPin2, rightSpeed);
  } 
  else if (cmd == "L") { // เลี้ยวซ้าย
    analogWrite(leftPin1, 0);
    analogWrite(leftPin2, leftSpeed);
    analogWrite(rightPin1, rightSpeed);
    analogWrite(rightPin2, 0);
  } 
  else if (cmd == "R") { // เลี้ยวขวา
    analogWrite(leftPin1, leftSpeed);
    analogWrite(leftPin2, 0);
    analogWrite(rightPin1, 0);
    analogWrite(rightPin2, rightSpeed);
  } 
  else { // S หรืออื่นๆ สั่งหยุด
    analogWrite(leftPin1, 0);
    analogWrite(leftPin2, 0);
    analogWrite(rightPin1, 0);
    analogWrite(rightPin2, 0);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(leftPin1, OUTPUT);
  pinMode(leftPin2, OUTPUT);
  pinMode(rightPin1, OUTPUT);
  pinMode(rightPin2, OUTPUT);
  
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  leftGripper.setPeriodHertz(50);
  rightGripper.setPeriodHertz(50);
  
  leftGripper.attach(LEFT_SERVO_PIN, 500, 2400);
  rightGripper.attach(RIGHT_SERVO_PIN, 500, 2400);

  controlRobot("S", 0, 0);
  controlRobot("G_OFF", 0, 0); 

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  udp.begin(localPort);
}

void loop() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char incomingPacket[255];
    int len = udp.read(incomingPacket, 255);
    if (len > 0) {
      incomingPacket[len] = 0;
    }

    String request = String(incomingPacket);
    request.trim(); 

    int firstComma = request.indexOf(',');
    int secondComma = request.indexOf(',', firstComma + 1);

    if (firstComma != -1) {
      String cmd = request.substring(0, firstComma);
      cmd.trim(); 
      
      int leftSpeed = 0;
      int rightSpeed = 0;

      if (secondComma != -1) {
        leftSpeed = request.substring(firstComma + 1, secondComma).toInt();
        rightSpeed = request.substring(secondComma + 1).toInt();
      } else {
        int speed = request.substring(firstComma + 1).toInt();
        leftSpeed = speed;
        rightSpeed = speed;
      }
      
      Serial.print("CMD: [");
      Serial.print(cmd);
      Serial.print("] | Left: ");
      Serial.print(leftSpeed);
      Serial.print(" | Right: ");
      Serial.println(rightSpeed);
      
      controlRobot(cmd, leftSpeed, rightSpeed);
    }
  }
}
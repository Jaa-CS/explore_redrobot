#include <WiFi.h>
#include <WebServer.h>

// --- ตั้งค่า Wi-Fi SSID และ Password ของคุณ ---
const char* ssid = "YOUR_WIFI_SSID";         // เปลี่ยนเป็นชื่อ Wi-Fi
const char* password = "YOUR_WIFI_PASSWORD"; // เปลี่ยนเป็นรหัสผ่าน Wi-Fi

WebServer server(80);

// --- กำหนดพินมอเตอร์แบบ 2 พินต่อข้าง (รวม 4 พิน) ---


// ปรับ pin เองนะจะ จูบๆๆๆๆ
// ยังไม่ได้ทําของ gipper นะ

// มอเตอร์ฝั่งซ้าย (ต่อเข้ากับ 2 พินของ Driver เช่น IN1, IN2)
const int leftPin1 = 12;
const int leftPin2 = 14;

// มอเตอร์ฝั่งขวา (ต่อเข้ากับ 2 พินของ Driver เช่น IN3, IN4)
const int rightPin1 = 26;
const int rightPin2 = 25;

// ฟังก์ชันควบคุมมอเตอร์ (แบบ 2 พินต่อข้าง)
void moveRobot(String cmd, int speed) {
  speed = constrain(speed, 0, 255);

  if (cmd == "F") {
    // เดินหน้า: ซ้ายเดินหน้า, ขวาเดินหน้า
    analogWrite(leftPin1, speed);
    digitalWrite(leftPin2, LOW);
    analogWrite(rightPin1, speed);
    digitalWrite(rightPin2, LOW);
    Serial.println("Action: Forward");
  } 
  else if (cmd == "B") {
    // ถอยหลัง: ซ้ายถอย, ขวาถอย
    digitalWrite(leftPin1, LOW);
    analogWrite(leftPin2, speed);
    digitalWrite(rightPin1, LOW);
    analogWrite(rightPin2, speed);
    Serial.println("Action: Backward");
  } 
  else if (cmd == "L") {
    // เลี้ยวซ้าย: ซ้ายถอย, ขวาเดินหน้า
    digitalWrite(leftPin1, LOW);
    analogWrite(leftPin2, speed);
    analogWrite(rightPin1, speed);
    digitalWrite(rightPin2, LOW);
    Serial.println("Action: Left");
  } 
  else if (cmd == "R") {
    // เลี้ยวขวา: ซ้ายเดินหน้า, ขวาถอย
    analogWrite(leftPin1, speed);
    digitalWrite(leftPin2, LOW);
    digitalWrite(rightPin1, LOW);
    analogWrite(rightPin2, speed);
    Serial.println("Action: Right");
  } 
  else {
    // หยุด (Stop / S หรือคำสั่งอื่นๆ)
    digitalWrite(leftPin1, LOW);
    digitalWrite(leftPin2, LOW);
    digitalWrite(rightPin1, LOW);
    digitalWrite(rightPin2, LOW);
    Serial.println("Action: Stop");
  }
}

// ฟังก์ชันรับ HTTP Request จาก Python
void handleMove() {
  if (server.hasArg("cmd") && server.hasArg("speed")) {
    String cmd = server.arg("cmd");
    int speed = server.arg("speed").toInt();

    moveRobot(cmd, speed);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void setup() {
  Serial.begin(115200);

  // กำหนดโหมดพินทั้งหมดเป็น OUTPUT
  pinMode(leftPin1, OUTPUT);
  pinMode(leftPin2, OUTPUT);
  pinMode(rightPin1, OUTPUT);
  pinMode(rightPin2, OUTPUT);

  // เริ่มต้นด้วยการหยุดรถ
  moveRobot("S", 0);

  // เชื่อมต่อ Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); // นำ IP นี้ไปใส่ใน Python (ESP32_IP)

  // ตั้งค่า Web Server
  server.on("/move", handleMove);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
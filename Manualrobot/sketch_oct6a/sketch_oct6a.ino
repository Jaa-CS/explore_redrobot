#include <WiFi.h>
#include <WebServer.h>

// ตั้งค่า Wi-Fi ของคุณ
const char* ssid = "YOUR_WIFI_SSID";     // เปลี่ยนเป็นชื่อ Wi-Fi
const char* password = "YOUR_WIFI_PASSWORD"; // เปลี่ยนเป็นรหัสผ่าน Wi-Fi

WebServer server(80);

// กำหนดขา Pin สำหรับควบคุมมอเตอร์และตัวหนีบ (ปรับแก้ตามวงจรของคุณได้เลยครับ)
const int LED_PIN = 2; // ตัวอย่างใช้ไฟบนบอร์ด หรือเปลี่ยนเป็นพินขับมอเตอร์

void handleMove() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    
    // ตรวจสอบตัวอักษรคำสั่งที่ส่งมาจากเว็บ
    if (cmd == "F") {
      Serial.println("Action: Forward (เดินหน้า)");
      // ใส่โค้ดสั่งมอเตอร์เดินหน้าตรงนี้
    } 
    else if (cmd == "B") {
      Serial.println("Action: Backward (ถอยหลัง)");
      // ใส่โค้ดสั่งมอเตอร์ถอยหลังตรงนี้
    } 
    else if (cmd == "L") {
      Serial.println("Action: Left (เลี้ยวซ้าย)");
      // ใส่โค้ดสั่งมอเตอร์เลี้ยวซ้ายตรงนี้
    } 
    else if (cmd == "R") {
      Serial.println("Action: Right (เลี้ยวขวา)");
      // ใส่โค้ดสั่งมอเตอร์เลี้ยวขวาตรงนี้
    } 
    else if (cmd == "S") {
      Serial.println("Action: Stop (หยุด)");
      // ใส่โค้ดสั่งมอเตอร์หยุดตรงนี้
    } 
    else if (cmd == "O") {
      Serial.println("Action: Gripper OPEN (เปิดตัวหนีบ)");
      digitalWrite(LED_PIN, HIGH); // ตัวอย่างเปิดไฟ
      // ใส่โค้ดสั่งเซอร์โวเปิดตัวหนีบตรงนี้
    } 
    else if (cmd == "C") {
      Serial.println("Action: Gripper CLOSE (ปิดตัวหนีบ)");
      digitalWrite(LED_PIN, LOW); // ตัวอย่างปิดไฟ
      // ใส่โค้ดสั่งเซอร์โวปิดตัวหนีบตรงนี้
    }
    
    server.send(200, "text/plain", "OK: " + cmd);
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // เชื่อมต่อ Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); // ** สำคัญ: นำ IP นี้ไปใส่ในไฟล์ HTML ฝั่งคอมพิวเตอร์ **

  // กำหนดเส้นทาง URL สำหรับรับคำสั่ง
  server.on("/move", handleMove);
  server.begin();
}

void loop() {
  server.handleClient();
}
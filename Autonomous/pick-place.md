// NOTE: on newer Arduino-ESP32 cores (3.x) analogWrite() works directly on
// any GPIO, no extra setup needed. On older cores it doesn't exist - if this
// fails to compile, do this instead:
//   setup():  ledcAttach(MOTOR_LEFT_PWM, 5000, 8);  ledcAttach(MOTOR_RIGHT_PWM, 5000, 8);
//   in place of analogWrite(pin, val), use:  ledcWrite(pin, val);

// ---------------- WiFi / UDP ----------------
const char* WIFI_SSID     = "YOUR_WIFI_NAME";      // TODO
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";  // TODO

const unsigned int LOCAL_UDP_PORT = 4210;  // this ESP32 listens here for nav commands
const char* LAPTOP_IP   = "192.168.1.20";  // TODO: your laptop's IP (check with ipconfig/ifconfig)
const unsigned int LAPTOP_PORT = 4211;     // must match LISTEN_PORT in field_vision.py



**To calibrate**

// ---------------- Servo (gripper) ----------------
// Recommended ESP32 PWM-capable pins: 2,4,12-19,21-23,25-27,32-33
const int LEFT_SERVO_PIN  = 13;   // TODO match your wiring
const int RIGHT_SERVO_PIN = 14;

const int LEFT_ARM_OPEN_ANGLE   = 60;
const int LEFT_ARM_CLOSE_ANGLE  = 130;
const int RIGHT_ARM_OPEN_ANGLE  = 130;
const int RIGHT_ARM_CLOSE_ANGLE = 60;

// ---------------- Motor driver (placeholder - adjust to your driver) ----------------
const int MOTOR_LEFT_IN1  = 26;
const int MOTOR_LEFT_IN2  = 27;
const int MOTOR_RIGHT_IN1 = 32;
const int MOTOR_RIGHT_IN2 = 33;
const int MOTOR_LEFT_PWM  = 25;
const int MOTOR_RIGHT_PWM = 4;

const int DRIVE_SPEED = 150;  // 0-255, tune for your motors
const int TURN_SPEED  = 120;  // 0-255, tune for your motors

// ---------------- HuskyLens frame / target selection ----------------
const int FRAME_CENTER_MIN = 120;
const int FRAME_CENTER_MAX = 200;

// Max block that HuskyLens can detect
const int MAX_BLOCKS = 10;

// Minimum theshold for the PICKING state to proceed
long minimum_box_size = 3000;  // TODO calibrate

// turning the robot in searching state

void turnSearch() {
  turnRight();  // simple constant-direction search; could add a sweep pattern later
}

// Calibrate time of the arms to close fully enclose the stone in PICKING state

delay(400);

import threading
import time
import cv2
import mediapipe as mp
import requests
from mediapipe.tasks import python
from mediapipe.tasks.python import vision

# ตั้งค่า IP Address ของ ESP32 และความเร็วคงที่
ESP32_IP = "192.168.1.50"
FIXED_SPEED = 200  # กำหนดความเร็วคงที่ตรงนี้ (0 - 255)

# โหลดโมเดล Hand Landmarker
base_options = python.BaseOptions(model_asset_path="hand_landmarker.task")
options = vision.HandLandmarkerOptions(
    base_options=base_options,
    num_hands=1,
    min_hand_detection_confidence=0.6,
    min_hand_presence_confidence=0.6,
)
detector = vision.HandLandmarker.create_from_options(options)

# เปิดกล้องเว็บแคม
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

last_cmd = ""
last_speed = -1
last_sent_time = 0


# ฟังก์ชันส่ง Wi-Fi แบบเบื้องหลัง (Background Thread)
def send_to_esp32_async(cmd, speed):
  def send():
    try:
      url = f"http://{ESP32_IP}/move?cmd={cmd}&speed={speed}"
      requests.get(url, timeout=0.05)
    except:
      pass

  threading.Thread(target=send, daemon=True).start()


def get_finger_status(landmarks):
  tips = [4, 8, 12, 16, 20]
  pips = [2, 6, 10, 14, 18]
  fingers = []

  if landmarks[tips[0]].x < landmarks[pips[0]].x:
    fingers.append(True)
  else:
    fingers.append(False)

  for i in range(1, 5):
    if landmarks[tips[i]].y < landmarks[pips[i]].y:
      fingers.append(True)
    else:
      fingers.append(False)

  return fingers


def draw_hand_landmarks(image, landmark_list):
  h, w, _ = image.shape
  hand_connections = [
      (0, 1),
      (1, 2),
      (2, 3),
      (3, 4),
      (0, 5),
      (5, 6),
      (6, 7),
      (7, 8),
      (5, 9),
      (9, 10),
      (10, 11),
      (11, 12),
      (9, 13),
      (13, 14),
      (14, 15),
      (15, 16),
      (13, 17),
      (17, 18),
      (18, 19),
      (19, 20),
      (0, 17),
  ]

  points = []
  for lm in landmark_list:
    px, py = int(lm.x * w), int(lm.y * h)
    points.append((px, py))
    cv2.circle(image, (px, py), 4, (0, 0, 255), -1)

  for connection in hand_connections:
    pt1 = points[connection[0]]
    pt2 = points[connection[1]]
    cv2.line(image, pt1, pt2, (0, 255, 0), 2)


while cap.isOpened():
  success, frame = cap.read()
  if not success:
    break

  frame = cv2.flip(frame, 1)
  image_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
  mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=image_rgb)

  detection_result = detector.detect(mp_image)

  cmd = "S"
  speed = 0

  if detection_result.hand_landmarks:
    for hand_landmarks in detection_result.hand_landmarks:
      draw_hand_landmarks(frame, hand_landmarks)

      lm = hand_landmarks
      # ใช้ความเร็วคงที่ แทนการคำนวณตามตำแหน่งข้อมือ
      speed = FIXED_SPEED

      fingers = get_finger_status(lm)

      if fingers == [False, False, False, False, False]:
        cmd = "S"
      elif fingers == [True, False, False, False, False]:
        cmd = "F"
      elif fingers == [False, True, False, False, False]:
        cmd = "B"
      elif fingers == [True, False, False, False, True]:
        cmd = "L"
      elif fingers == [True, True, True, False, False]:
        cmd = "R"
      elif fingers == [False, True, True, False, False]:
        cmd = "on"
      elif fingers == [False, True, True, True, False]:
        cmd = "off"
      else:
        cmd = "S"
  else:
    cmd = "S"
    speed = 0

  if (
      cmd != last_cmd
      or abs(speed - last_speed) > 10
      or (time.time() - last_sent_time > 0.08)
  ):
    send_to_esp32_async(cmd, speed)
    last_cmd = cmd
    last_speed = speed
    last_sent_time = time.time()

  # แสดงข้อความคำสั่งและความเร็วบนหน้าจอ
  cv2.putText(
      frame,
      f"CMD: {cmd} | Speed: {speed}",
      (30, 50),
      cv2.FONT_HERSHEY_SIMPLEX,
      1,
      (0, 255, 0),
      2,
  )
  cv2.imshow("Fast MediaPipe Robot Control", frame)

  if cv2.waitKey(1) & 0xFF == ord("q"):
    break

cap.release()
cv2.destroyAllWindows()
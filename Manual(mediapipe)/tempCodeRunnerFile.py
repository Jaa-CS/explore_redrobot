import socket
import time
import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision

# --- ตั้งค่า IP Address ของ ESP32 และ Port ---
ESP32_IP = "10.164.108.67"  # เปลี่ยนเป็น IP ของ ESP32
ESP32_PORT = 4210
FIXED_SPEED = 120

# สร้าง Socket แบบ UDP
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# โหลดโมเดล Hand Landmarker
base_options = python.BaseOptions(model_asset_path="hand_landmarker.task")
options = vision.HandLandmarkerOptions(
    base_options=base_options,
    num_hands=1,
    min_hand_detection_confidence=0.6,
    min_hand_presence_confidence=0.6,
)
detector = vision.HandLandmarker.create_from_options(options)

cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

last_cmd = ""
last_speed = 0
last_sent_time = 0
SEND_INTERVAL = 0.375  # ส่งซ้ำทุกๆ 0.375 วินาที เพื่อป้องกันแพ็กเกจหลุด


def send_udp(cmd, speed):
    message = f"{cmd},{speed}"
    sock.sendto(message.encode("utf-8"), (ESP32_IP, ESP32_PORT))


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
            fingers = get_finger_status(hand_landmarks)

            if fingers == [False, False, False, False, False]:
                cmd = "S"
                speed = 0
            elif fingers == [True, False, False, False, False]:
                cmd = "F"
                speed = FIXED_SPEED
            elif fingers == [False, True, False, False, False]:
                cmd = "B"
                speed = FIXED_SPEED
            elif fingers == [False, False, False, False, True]:
                cmd = "L"
                speed = FIXED_SPEED
            elif fingers == [False, True, True, False, False]:
                cmd = "R"
                speed = FIXED_SPEED
            # --- เพิ่มท่าทางสำหรับ Gripper ---
            elif fingers == [True, True, True, True, True]:
                cmd = "G_OFF"  # แบมือ 5 นิ้ว = ปล่อย
                speed = 0
            elif fingers == [False, True, True, True, True]:
                cmd = "G_ON"   # ชู 4 นิ้ว (หดนิ้วโป้ง) = หนีบ
                speed = 0
            else:
                cmd = "S"
                speed = 0
    else:
        cmd = "S"
        speed = 0

    current_time = time.time()

    if cmd != last_cmd or speed != last_speed:
        send_udp(cmd, speed)
        last_cmd = cmd
        last_speed = speed
        last_sent_time = current_time
    elif current_time - last_sent_time >= SEND_INTERVAL:
        send_udp(cmd, speed)
        last_sent_time = current_time

    cv2.putText(
        frame,
        f"CMD: {cmd} | Speed: {speed}",
        (30, 50),
        cv2.FONT_HERSHEY_SIMPLEX,
        1,
        (0, 255, 0),
        2,
    )
    cv2.imshow("UDP Real-time Robot Control", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
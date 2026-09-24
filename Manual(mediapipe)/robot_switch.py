import os
import socket
import time
import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision

# --- ตั้งค่า IP Address ของ ESP32 และ Port ---
ESP32_IP = "10.164.108.67"
ESP32_PORT = 4210

# ==========================================
# --- 1. ตั้งค่าความเร็วในแต่ละโหมด (0-255) ---
# ==========================================
# [โหมดปกติ - NORMAL MODE]
NORMAL_FORWARD_L = 120
NORMAL_FORWARD_R = 120
NORMAL_TURN_L = 90
NORMAL_TURN_R = 90

# [โหมดช้า - SLOW MODE]
SLOW_FORWARD_L = 80
SLOW_FORWARD_R = 80
SLOW_TURN_L = 50  # หันช้าลงเพื่อความแม่นยำ
SLOW_TURN_R = 50

# ==========================================
# --- 2. ตัวแปรสำหรับสลับโหมด (State Variable) ---
# ==========================================
is_slow_mode = False  # False = Normal, True = Slow
mode_toggle_lock = (
    False  # ตัวล็อกป้องกันการสลับโหมดรัวๆ เวลาชูมือค้างไว้ (Edge Trigger)
)

# สร้าง Socket แบบ UDP
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# โหลดโมเดล Hand Landmarker
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "hand_landmarker.task")

base_options = python.BaseOptions(model_asset_path=MODEL_PATH)
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
last_left_speed = 0
last_right_speed = 0
last_sent_time = 0
SEND_INTERVAL = 0.375


def send_udp(cmd, left_speed, right_speed):
    message = f"{cmd},{left_speed},{right_speed}"
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
    l_spd = 0
    r_spd = 0

    # --- เลือกชุดความเร็วตามโหมดปัจจุบัน ---
    if is_slow_mode:
        fwd_l, fwd_r = SLOW_FORWARD_L, SLOW_FORWARD_R
        turn_l, turn_r = SLOW_TURN_L, SLOW_TURN_R
    else:
        fwd_l, fwd_r = NORMAL_FORWARD_L, NORMAL_FORWARD_R
        turn_l, turn_r = NORMAL_TURN_L, NORMAL_TURN_R

    if detection_result.hand_landmarks:
        for hand_landmarks in detection_result.hand_landmarks:
            draw_hand_landmarks(frame, hand_landmarks)
            fingers = get_finger_status(hand_landmarks)

            # --- ท่าทางสลับโหมด: นิ้วชี้ + นิ้วก้อย 🤘 [False, True, False, False, True] ---
            if fingers == [False, True, False, False, True]:
                if not mode_toggle_lock:
                    is_slow_mode = not is_slow_mode  # สลับโหมด (Normal <-> Slow)
                    mode_toggle_lock = True  # ล็อกไว้จนกว่าจะเปลี่ยนท่ามือ
                    print(
                        f">> Mode Switched! Current Mode: {'SLOW' if is_slow_mode else 'NORMAL'}"
                    )

                cmd = "S"  # ตอนสลับโหมดให้หุ่นหยุดนิ่งก่อน
                l_spd, r_spd = 0, 0

            else:
                mode_toggle_lock = (
                    False  # ปลดล็อกเมื่อเปลี่ยนไปทำท่าอื่นแล้ว
                )

                if fingers == [False, False, False, False, False]:
                    cmd = "S"
                    l_spd, r_spd = 0, 0
                elif fingers == [True, False, False, False, False]:
                    cmd = "F"
                    l_spd, r_spd = fwd_l, fwd_r
                elif fingers == [False, True, False, False, False]:
                    cmd = "B"
                    l_spd, r_spd = fwd_l, fwd_r
                elif fingers == [False, False, False, False, True]:
                    cmd = "L"
                    l_spd, r_spd = turn_l, turn_l
                elif fingers == [False, True, True, False, False]:
                    cmd = "R"
                    l_spd, r_spd = turn_r, turn_r
                elif fingers == [True, True, True, True, True]:
                    cmd = "G_OFF"  # ปล่อยก้ามปู
                    l_spd, r_spd = 0, 0
                elif fingers == [False, True, True, True, True]:
                    cmd = "G_ON"  # หนีบก้ามปู
                    l_spd, r_spd = 0, 0
                else:
                    cmd = "S"
                    l_spd, r_spd = 0, 0
    else:
        mode_toggle_lock = False
        cmd = "S"
        l_spd, r_spd = 0, 0

    current_time = time.time()

    if cmd != last_cmd or l_spd != last_left_speed or r_spd != last_right_speed:
        send_udp(cmd, l_spd, r_spd)
        last_cmd = cmd
        last_left_speed = l_spd
        last_right_speed = r_spd
        last_sent_time = current_time
    elif current_time - last_sent_time >= SEND_INTERVAL:
        send_udp(cmd, l_spd, r_spd)
        last_sent_time = current_time

    # --- แสดงผลโหมดบนหน้าจอ OpenCV ---
    mode_text = "MODE: SLOW" if is_slow_mode else "MODE: NORMAL"
    mode_color = (0, 165, 255) if is_slow_mode else (0, 255, 0)  # ส้ม / เขียว

    cv2.putText(
        frame,
        f"{mode_text} | CMD: {cmd} | L:{l_spd} R:{r_spd}",
        (20, 40),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        mode_color,
        2,
    )
    cv2.imshow("UDP Real-time Robot Control", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
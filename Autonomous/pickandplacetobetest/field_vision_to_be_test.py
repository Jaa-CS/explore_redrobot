
import cv2
import numpy as np
import socket
import math
import time


# ============================ CONFIG - fill these in ============================

ESP32_IP = "192.168.4.1"      # fixed softAP address
ESP32_PORT = 4210             # must match LOCAL_UDP_PORT in the .ino
LISTEN_PORT = 4211            # this script's own port; must match LAPTOP_PORT in the .ino

CAMERA_INDEX = 0              # which webcam (0 is usually the first/only one)
MARKER_ID = 5                 # the ArUco marker ID you print and stick on the robot
MARKER_DICT = cv2.aruco.DICT_4X4_50

FRAME_W, FRAME_H = 1920, 1080


# Step 1 of calibration: with the camera in its final mounted position, take one
# photo of the empty field and read off the pixel (x, y) of each of these 4
# corners in an image viewer. Order: top-left, top-right, bottom-right, bottom-left.
IMAGE_PTS = np.array([
    [176, 4],           # pixel coords of field's top-left corner
    [1908, 46],         # top-right
    [1867, 1055],       # bottom-right
    [180, 1010],        # bottom-left
], dtype=np.float32)

# Step 2: the real size of the field (any consistent unit - cm is convenient).
FIELD_W, FIELD_H = 208, 122   

FIELD_PTS = np.array([
    [0, 0], [FIELD_W, 0], [FIELD_W, FIELD_H], [0, FIELD_H],
], dtype=np.float32)

# Step 3: where each color zone's center is, in the same field units as above.
# Measure these once the field is set up.
COLOR_ZONES_PX = {
    1: (419, 373),     # ซ้ายบน   
    2: (407, 745),    # ซ้ายล่าง     
    3: (816, 174),    # กลางบน   
    4: (816, 901),     # กลางล่าง    
    5: (1175, 198),    # ขวาบน     
    6: (1158, 894),   # ขวาล่าง      
}

HEADING_TOLERANCE = math.radians(8)   # how close to "pointed at the target" counts as aligned
ARRIVAL_RADIUS = 8                    # field units - how close counts as "arrived"

INVERT_TURNS = False

HELLO_PERIOD = 1.0        # keep-alive ping; ESP32 answers HELLO_ACK
ARRIVED_RESEND = 0.3      # repeat ARRIVED until the ESP32 confirms PLACED
LINK_TIMEOUT = 3.0        # no reply for this long -> show "NO REPLY" on screen

H, _ = cv2.findHomography(IMAGE_PTS, FIELD_PTS)

aruco_dict = cv2.aruco.getPredefinedDictionary(MARKER_DICT)
aruco_params = cv2.aruco.DetectorParameters()
detector = cv2.aruco.ArucoDetector(aruco_dict, aruco_params)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", LISTEN_PORT))
sock.setblocking(False)


def pixel_to_field(px, py):
    """Turn a camera pixel coordinate into a real field coordinate."""
    pt = np.array([[[px, py]]], dtype=np.float32)
    out = cv2.perspectiveTransform(pt, H)
    return float(out[0][0][0]), float(out[0][0][1])

# ADDED: zones in field units, derived from the pixel positions above
COLOR_ZONES = {zid: pixel_to_field(*px) for zid, px in COLOR_ZONES_PX.items()}

def check_zones():
    """Print the converted zones and warn if any falls outside the field rectangle."""
    print("Zone centres in field units:")
    for zid, (x, y) in COLOR_ZONES.items():
        inside = 0 <= x <= FIELD_W and 0 <= y <= FIELD_H
        print(f"  zone {zid}: ({x:6.1f}, {y:6.1f})" + ("" if inside else "   <-- OUTSIDE FIELD, check calibration!"))

_warned_send = False


def send_command(cmd):
    """Send one text message to the ESP32 (never crashes if the WiFi is not joined yet)."""
    global _warned_send
    try:
        sock.sendto(cmd.encode(), (ESP32_IP, ESP32_PORT))
        _warned_send = False
    except OSError as e:
        if not _warned_send:
            print(f"Cannot send to ESP32 ({e}). Is the laptop connected to the ESP32's WiFi?")
            _warned_send = True


def poll_esp32():
    """Non-blocking: return a list with every text message the ESP32 has sent since last call."""
    msgs = []
    while True:
        try:
            data, _ = sock.recvfrom(64)
        except BlockingIOError:
            break
        except ConnectionResetError:
            # Windows quirk: raised once after we sent to a port nobody was listening on yet
            continue
        msgs.append(data.decode(errors="ignore").strip())
    return msgs
 
 
def main():
    cap = cv2.VideoCapture(CAMERA_INDEX)
    # ADDED: request the calibration resolution (MJPG is needed for 1080p on most USB webcams)
    # On Windows, if the picture is black or slow, try: cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)
 
    check_zones()
 
    # IDLE -> NAVIGATING (after PICKED) -> RELEASING (after arrival) -> IDLE (after PLACED)
    state = "IDLE"
    target = None          # (x, y) field coords of the current target zone
    target_id = None
    last_hello = 0.0
    last_arrived = 0.0
    last_reply_time = 0.0
    last_ack_text = ""
    size_checked = False
    size_ok = True
 
    cv2.namedWindow("Field camera", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Field camera", 1280, 720)
 
    while True:
        ok, frame = cap.read()
        if not ok:
            continue
        now = time.time()
 
        # ADDED: make sure the camera really delivers the resolution IMAGE_PTS was measured at
        if not size_checked:
            h_, w_ = frame.shape[:2]
            print(f"Camera frame size: {w_}x{h_}")
            if (w_, h_) != (FRAME_W, FRAME_H):
                size_ok = False
                print(f"WARNING: expected {FRAME_W}x{FRAME_H}. Navigation will be WRONG until the "
                      f"camera size matches the size IMAGE_PTS/COLOR_ZONES_PX were measured at.")
            size_checked = True
 
        # ---- keep-alive / link test -------------------------------------------------
        if now - last_hello >= HELLO_PERIOD:
            send_command("HELLO")
            last_hello = now
 
        # ---- everything the ESP32 told us since the last frame ------------------------
        for msg in poll_esp32():
            last_reply_time = now
            if msg.startswith("PICKED,"):
                try:
                    pid = int(msg.split(",")[1])
                except (ValueError, IndexError):
                    continue
                send_command("ACK_PICKED")      # tell the ESP32 to stop repeating PICKED
                if state == "IDLE":
                    if pid in COLOR_ZONES:
                        target_id = pid
                        target = COLOR_ZONES[pid]
                        state = "NAVIGATING"
                        print(f"Robot picked color {pid} -> heading to {target}")
                    else:
                        print(f"Robot reported unknown color id {pid} - ignoring")
            elif msg.startswith("ACK,"):
                last_ack_text = msg
            elif msg.startswith("PLACED,"):
                if state == "RELEASING":
                    print(f"SUCCESS: ESP32 confirmed the stone was placed ({msg})")
                    send_command("ACK_PLACED")  # let the ESP32 stop repeating PLACED
                    state = "IDLE"
                    target = None
                    target_id = None
            # "HELLO_ACK" needs no action beyond refreshing last_reply_time
 
        # ---- draw the zones so calibration errors are visible at a glance --------------
        for zid, (zx, zy) in COLOR_ZONES_PX.items():
            cv2.circle(frame, (int(zx), int(zy)), 18, (0, 255, 255), 2)
            cv2.putText(frame, str(zid), (int(zx) - 8, int(zy) + 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
 
        # ---- navigation --------------------------------------------------------------
        if state == "NAVIGATING":
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            corners, ids, _ = detector.detectMarkers(gray)
 
            if ids is not None and MARKER_ID in ids.flatten():
                idx = list(ids.flatten()).index(MARKER_ID)
                c = corners[idx][0]  # 4 corner points in pixel coords,
                                      # order: top-left, top-right, bottom-right, bottom-left
 
                cx_px, cy_px = c[:, 0].mean(), c[:, 1].mean()
                # midpoint of the top edge = "front" of the marker.
                # Mount the marker so this edge faces the robot's front.
                front_px = ((c[0][0] + c[1][0]) / 2, (c[0][1] + c[1][1]) / 2)
 
                robot_x, robot_y = pixel_to_field(cx_px, cy_px)
                front_x, front_y = pixel_to_field(*front_px)
                robot_heading = math.atan2(front_y - robot_y, front_x - robot_x)
 
                target_x, target_y = target
                desired_heading = math.atan2(target_y - robot_y, target_x - robot_x)
                distance = math.hypot(target_x - robot_x, target_y - robot_y)
 
                error = desired_heading - robot_heading
                error = math.atan2(math.sin(error), math.cos(error))  # wrap to [-pi, pi]
 
                if distance < ARRIVAL_RADIUS:
                    send_command("ARRIVED")
                    last_arrived = now
                    state = "RELEASING"
                    print("Arrived - asking ESP32 to release the stone (waiting for PLACED)")
                else:
                    # CHANGED: the image/field y-axis points DOWN, so a positive error means the
                    # target is clockwise of the robot's heading = to its RIGHT. The old code had
                    # these two swapped. If your robot still turns the wrong way, set INVERT_TURNS.
                    turn_pos = "TURN_LEFT" if INVERT_TURNS else "TURN_RIGHT"
                    turn_neg = "TURN_RIGHT" if INVERT_TURNS else "TURN_LEFT"
                    if error > HEADING_TOLERANCE:
                        send_command(turn_pos)
                    elif error < -HEADING_TOLERANCE:
                        send_command(turn_neg)
                    else:
                        send_command("FORWARD")
 
                cv2.aruco.drawDetectedMarkers(frame, corners, ids)
                cv2.putText(frame, f"dist={distance:.1f} err={math.degrees(error):.1f}deg",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            else:
                # marker not visible this frame - stop rather than drive blind
                send_command("STOP")
 
        elif state == "RELEASING":
            # UDP can lose packets: keep asking until the ESP32 answers "PLACED,<id>"
            if now - last_arrived >= ARRIVED_RESEND:
                send_command("ARRIVED")
                last_arrived = now
 
        # ---- status overlay ----------------------------------------------------------
        linked = (now - last_reply_time) < LINK_TIMEOUT
        cv2.putText(frame, "ESP32 link OK" if linked else "ESP32: NO REPLY",
                    (10, 65), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                    (0, 255, 0) if linked else (0, 0, 255), 2)
        cv2.putText(frame, f"state={state} target={target_id} last={last_ack_text}",
                    (10, 100), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
        if not size_ok:
            cv2.putText(frame, "WRONG CAMERA RESOLUTION", (10, 135),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 255), 2)
 
        cv2.imshow("Field camera", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
 
    send_command("STOP")
    cap.release()
    cv2.destroyAllWindows()
 
 
if __name__ == "__main__":
    main()

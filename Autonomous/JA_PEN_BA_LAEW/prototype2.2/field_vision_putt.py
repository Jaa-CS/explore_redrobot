# =============================================================================
# CHANGE MARKERS - search for these tags to see where each part came from
#   [F1]      taken from your FIRST file  (proportional steering version)
#   [F2]      taken from your SECOND file (display helpers / get_robot_pose version)
#   [NEW]     added or edited during the merge
#   [REMOVED] something that was dropped here (nothing to run, just a note)
#   (no tag)  identical in both files
# The code itself is identical to fieldVision.py - only comments differ.
# =============================================================================

import cv2
import numpy as np
import socket
import math
import time
import sys
import textwrap


# ============================ CONFIG - fill these in ============================

ESP32_IP = "192.168.4.1"      # fixed softAP address
ESP32_PORT = 4210             # must match LOCAL_UDP_PORT in the .ino
LISTEN_PORT = 4211            # this script's own port; must match LAPTOP_PORT in the .ino

CAMERA_INDEX = 0              # which webcam (0 is usually the first/only one)
MARKER_ID = 5                 # the ArUco marker ID you print and stick on the robot
MARKER_DICT = cv2.aruco.DICT_4X4_50

FRAME_W, FRAME_H = 1920, 1080   # [NEW] set to 1920x1080 as requested (both files already used this)


# Step 1 of calibration: with the camera in its final mounted position, take one
# photo of the empty field and read off the pixel (x, y) of each of these 4
# corners in an image viewer. Order: top-left, top-right, bottom-right, bottom-left.
# [NEW] comment: these must be measured on a 1920x1080 frame.
IMAGE_PTS = np.array([
    [136, 40],           # pixel coords of field's top-left corner
    [1916, 36],         # top-right
    [1912, 1057],       # bottom-right
    [136, 1045],        # bottom-left
], dtype=np.float32)

# Step 2: the real size of the field (any consistent unit - cm is convenient).
FIELD_W, FIELD_H = 208, 122

FIELD_PTS = np.array([
    [0, 0], [FIELD_W, 0], [FIELD_W, FIELD_H], [0, FIELD_H],
], dtype=np.float32)

# Step 3: where each color zone's center is (pixels, on the same 1920x1080 frame).
# [REMOVED] the old commented-out duplicate COLOR_ZONES_PX block that sat below this dict
COLOR_ZONES_PX = {
    1: (1239, 861),     # orange
    2: (849, 893),     # blue
    3: (426, 769),      # purple
    4: (446, 371),      # green
    5: (1219, 170),      # cyan
    6: (764, 202),      # red
}

# [NEW] comment only: HEADING_TOLERANCE is no longer used now that steering is proportional
HEADING_TOLERANCE = math.radians(8)   # not used by the proportional steering below; kept for reference
ARRIVAL_RADIUS = 8                    # field units - how close counts as "arrived"
ARM_OFFSET_CM = 15                    # distance from the marker's center to the gripper arms,
                                      # measured along the robot's forward heading

# change to True if robot turn away from target area
INVERT_TURNS = False

# ---- steering (proportional) ----                                   [F1] values are your latest tuned ones
PANEL_W = 640
BASE_SPEED = 180                # forward speed while curving - tune this                [F1]
MAX_STEER = 20                  # how much speed difference at max steering - tune this  [F1]
REALIGN_THRESHOLD_DEG = 40      # error bigger than this -> spin in place first          [F1]
STEER_GAIN = 1.0                # how aggressively steer scales with error - tune this   [F1]
MIN_WHEEL_SPEED = 160           # [NEW] was hardcoded as 160 inside the clamp in file 1
MAX_WHEEL_SPEED = 200           # [NEW] was hardcoded as 200 inside the clamp in file 1

# ---- display only: these change what is DRAWN, never how the robot drives ----   [F2] (defined but unused in file 1)
FIELD_MARGIN_CM = 10      # safe-area margin drawn inside the field edge
ZONE_RADIUS_CM = 8       # radius of the circle drawn around each colour placing area
HEADING_ARROW_CM = 10     # length of the red heading arrow
ZONE_STYLE = {            # zone id: (label, BGR colour) - ids match COLOR_ZONES_PX
    1: ("ORANGE", (0, 140, 255)),
    2: ("BLUE",   (255, 255, 0)),
    3: ("PURPLE", (200, 0, 160)),
    4: ("GREEN",  (0, 200, 0)),
    5: ("CYAN",   (155, 155, 0)),
    6: ("RED",    (0, 0, 255)),
}

HELLO_PERIOD = 1.0        # keep-alive ping; ESP32 answers HELLO_ACK
ARRIVED_RESEND = 0.3      # repeat ARRIVED until the ESP32 confirms PLACED
LINK_TIMEOUT = 3.0        # no reply for this long -> show "NO REPLY" on screen

H, _ = cv2.findHomography(IMAGE_PTS, FIELD_PTS)
H_INV = np.linalg.inv(H)   # [F2] goes the other way: field cm -> camera pixel (used for drawing)

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


# zones in field units, derived from the pixel positions above
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


# ============================ display helpers ============================
# [F2] this whole block (down to the end of draw_robot_overlay) comes from the second file.
# Everything in it only DRAWS on the video window.

FONT = cv2.FONT_HERSHEY_SIMPLEX


def field_to_pixel(x, y):
    """Inverse of pixel_to_field: a field coordinate (cm) -> the camera pixel it appears at."""
    pt = np.array([[[x, y]]], dtype=np.float32)
    out = cv2.perspectiveTransform(pt, H_INV)
    return int(round(out[0][0][0])), int(round(out[0][0][1]))


def draw_field_polygon(frame, points_cm, color, thickness=2):
    """Draw a closed shape given in field coordinates - it follows the camera's perspective."""
    pts = np.array([field_to_pixel(x, y) for x, y in points_cm], dtype=np.int32)
    cv2.polylines(frame, [pts], True, color, thickness, cv2.LINE_AA)


def draw_field_circle(frame, cx, cy, radius_cm, color, thickness=2, steps=48):
    """Draw a circle with a real radius (cm) around a field point."""
    ring = [(cx + radius_cm * math.cos(2 * math.pi * i / steps),
             cy + radius_cm * math.sin(2 * math.pi * i / steps)) for i in range(steps)]
    draw_field_polygon(frame, ring, color, thickness)


def put_text_centered(frame, text, center, color, scale=0.6, thickness=2):
    (w, _), _ = cv2.getTextSize(text, FONT, scale, thickness)
    x = min(max(5, center[0] - w // 2), frame.shape[1] - w - 5)
    y = min(max(20, center[1]), frame.shape[0] - 10)
    cv2.putText(frame, text, (x, y), FONT, scale, color, thickness, cv2.LINE_AA)


def get_robot_pose(frame):
    """[F2] Find the robot's ArUco marker. Returns its pose, or None if it is not visible.
    (In file 1 this marker-detection code lived inline inside main()'s NAVIGATING block.)"""
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    corners, ids, _ = detector.detectMarkers(gray)
    if ids is None or MARKER_ID not in ids.flatten():
        return None

    idx = list(ids.flatten()).index(MARKER_ID)
    c = corners[idx][0]  # 4 corner points in pixel coords,
                         # order: top-left, top-right, bottom-right, bottom-left

    cx_px, cy_px = c[:, 0].mean(), c[:, 1].mean()
    # midpoint of the top edge = "front" of the marker.
    # Mount the marker so this edge faces the robot's front.
    front_px = ((c[0][0] + c[1][0]) / 2, (c[0][1] + c[1][1]) / 2)

    robot_x, robot_y = pixel_to_field(cx_px, cy_px)
    front_x, front_y = pixel_to_field(*front_px)
    heading = math.atan2(front_y - robot_y, front_x - robot_x)

    # The stone sits between the arms, ARM_OFFSET_CM ahead of the marker along
    # the direction the robot faces - not at the marker's own position.
    arm_x = robot_x + ARM_OFFSET_CM * math.cos(heading)
    arm_y = robot_y + ARM_OFFSET_CM * math.sin(heading)

    return {"corners": corners, "ids": ids, "x": robot_x, "y": robot_y,
            "heading": heading, "arm_x": arm_x, "arm_y": arm_y}


def draw_field_overlay(frame, target_id):
    """Field edge (red), safe margin (yellow-green) and a circle around every colour zone."""
    m = FIELD_MARGIN_CM
    edge = [(0, 0), (FIELD_W, 0), (FIELD_W, FIELD_H), (0, FIELD_H)]
    safe = [(m, m), (FIELD_W - m, m), (FIELD_W - m, FIELD_H - m), (m, FIELD_H - m)]
    draw_field_polygon(frame, edge, (0, 0, 255), 2)
    draw_field_polygon(frame, safe, (0, 255, 170), 2)
    sx, sy = field_to_pixel(*safe[0])
    cv2.putText(frame, f"SAFE AREA (MARGIN {m}cm)", (sx + 10, sy + 25),
                FONT, 0.6, (0, 255, 170), 2, cv2.LINE_AA)

    for zid, (zx, zy) in COLOR_ZONES.items():
        name, color = ZONE_STYLE.get(zid, (f"ZONE {zid}", (255, 255, 255)))
        draw_field_circle(frame, zx, zy, ZONE_RADIUS_CM, color, 3)
        cv2.circle(frame, field_to_pixel(zx, zy), 5, color, -1, cv2.LINE_AA)
        put_text_centered(frame, f"{zid} {name}", field_to_pixel(zx, zy - ZONE_RADIUS_CM - 3), color)

        if zid == target_id:   # the zone the robot is currently heading to
            draw_field_circle(frame, zx, zy, ZONE_RADIUS_CM + 3, (0, 255, 255), 2)
            draw_field_circle(frame, zx, zy, ZONE_RADIUS_CM + 6, (0, 255, 255), 1)
            put_text_centered(frame, f"LOCK {name}",
                              field_to_pixel(zx, zy - ZONE_RADIUS_CM - 10), (0, 255, 255))


def draw_robot_overlay(frame, pose, target):
    """Red heading arrow, green line + circle at the arm end (TCP), yellow line to the target."""
    cv2.aruco.drawDetectedMarkers(frame, pose["corners"], pose["ids"])

    x, y, heading = pose["x"], pose["y"], pose["heading"]
    center = field_to_pixel(x, y)
    tcp = field_to_pixel(pose["arm_x"], pose["arm_y"])
    arrow_tip = field_to_pixel(x + HEADING_ARROW_CM * math.cos(heading),
                               y + HEADING_ARROW_CM * math.sin(heading))

    # yellow line: from the arm end to the zone it has to reach
    if target is not None:
        cv2.line(frame, tcp, field_to_pixel(*target), (0, 255, 255), 3, cv2.LINE_AA)

    # green line: robot centre -> where the arms end (where the stone gets placed)
    cv2.line(frame, center, tcp, (0, 255, 0), 2, cv2.LINE_AA)
    # circle around the arm end = the "arrived" area (ARRIVAL_RADIUS)
    draw_field_circle(frame, pose["arm_x"], pose["arm_y"], ARRIVAL_RADIUS, (0, 255, 0), 2)
    cv2.circle(frame, tcp, 5, (0, 255, 0), -1, cv2.LINE_AA)
    cv2.putText(frame, f"TCP [{ARM_OFFSET_CM}cm]", (tcp[0] + 15, tcp[1] - 15),
                FONT, 0.6, (0, 255, 0), 2, cv2.LINE_AA)

    # red arrow: which way the robot is heading
    cv2.arrowedLine(frame, center, arrow_tip, (0, 0, 255), 4, cv2.LINE_AA, tipLength=0.35)

# ---- terminal panel: shows what the python console prints, to the left of the video ----

class LogPanel:
    """Keeps the most recent console lines. A line that repeats is not added again - it
    moves to the bottom and its counter goes up, so per-frame prints don't flood the panel."""

    def __init__(self, max_entries=60):
        self.entries = []          # each entry is [text, repeat_count]
        self.max_entries = max_entries

    def add(self, text):
        text = text.rstrip().encode("ascii", "replace").decode()   # cv2 can only draw plain ASCII
        if not text.strip():
            return
        for i, entry in enumerate(self.entries):
            if entry[0] == text:
                entry[1] += 1
                self.entries.append(self.entries.pop(i))
                return
        self.entries.append([text, 1])
        if len(self.entries) > self.max_entries:
            self.entries.pop(0)


class TeeToPanel:
    """Replaces sys.stdout: everything printed still reaches the real terminal, and each
    finished line is also handed to the LogPanel."""

    def __init__(self, real_stdout, panel):
        self.real = real_stdout
        self.panel = panel
        self._partial = ""

    def write(self, text):
        self.real.write(text)
        self._partial += text
        while "\n" in self._partial:
            line, self._partial = self._partial.split("\n", 1)
            self.panel.add(line)
        return len(text)

    def flush(self):
        self.real.flush()

    def __getattr__(self, name):          # anything else (encoding, isatty...) -> real stdout
        return getattr(self.real, name)


def log_line_color(text):
    if any(word in text for word in ("WARNING", "Cannot", "OUTSIDE", "unknown", "failed")):
        return (60, 90, 255)       # red
    if "SUCCESS" in text or "Arrived" in text:
        return (0, 255, 0)         # green
    if text.startswith(("Sent", "Received")):
        return (170, 170, 170)     # grey: routine traffic
    return (255, 255, 255)


LOG_WRAP_CHARS = 52     # characters per line that fit in PANEL_W
LOG_LINE_H = 26         # pixels between lines


def render_log_panel(height, entries):
    """Draw the newest console lines (newest at the bottom) into a PANEL_W x height image."""
    panel = np.full((height, PANEL_W, 3), 25, np.uint8)
    cv2.putText(panel, "TERMINAL", (15, 35), FONT, 0.9, (0, 255, 255), 2, cv2.LINE_AA)
    cv2.line(panel, (0, 52), (PANEL_W, 52), (90, 90, 90), 1)

    lines = []                                            # (text, colour) after wrapping
    for text, count in entries:
        shown = text if count == 1 else f"{text}  (x{count})"
        color = log_line_color(text)
        for k, piece in enumerate(textwrap.wrap(shown, LOG_WRAP_CHARS) or [""]):
            lines.append(("    " + piece if k else piece, color))

    max_lines = (height - 80) // LOG_LINE_H
    y = 85
    for text, color in lines[-max_lines:]:                # keep only what fits
        cv2.putText(panel, text, (15, y), FONT, 0.6, color, 1, cv2.LINE_AA)
        y += LOG_LINE_H
    return panel

def main():
    cap = cv2.VideoCapture(CAMERA_INDEX)
    # request the calibration resolution (MJPG is needed for 1080p on most USB webcams)
    # On Windows, if the picture is black or slow, try: cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)

    log_panel = LogPanel()
    tee = TeeToPanel(sys.stdout, log_panel)
    sys.stdout = tee

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
    cv2.resizeWindow("Field camera", int((FRAME_W + PANEL_W) * 720 / FRAME_H), 720)

    while True:
        ok, frame = cap.read()
        if not ok:
            continue
        now = time.time()

        # make sure the camera really delivers the resolution IMAGE_PTS was measured at
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
            print("Sent HELLO command.")
            last_hello = now

        # ---- everything the ESP32 told us since the last frame ------------------------
        for msg in poll_esp32():
            last_reply_time = now
            if msg.startswith("PICKED,"):
                print("Received picked UDP from esp32")
                try:
                    pid = int(msg.split(",")[1])
                except (ValueError, IndexError):
                    continue
                send_command("ACK_PICKED")      # tell the ESP32 to stop repeating PICKED
                print("Sent ACK_PICKED command.")
                if state == "IDLE":
                    if pid in COLOR_ZONES:
                        target_id = pid
                        target = COLOR_ZONES[pid]
                        state = "NAVIGATING"
                        print(f"Robot picked color {pid} -> heading to ({target[0]:.1f}, {target[1]:.1f})")
                    else:
                        print(f"Robot reported unknown color id {pid} - ignoring")
            elif msg.startswith("ACK,"):
                last_ack_text = msg
            elif msg.startswith("PLACED,"):
                print("Received placed UDP from esp32")
                if state == "RELEASING":
                    print(f"SUCCESS: ESP32 confirmed the stone was placed ({msg})")
                    send_command("ACK_PLACED")  # let the ESP32 stop repeating PLACED
                    print("Sent ACK_PLACED command")
                    state = "IDLE"
                    target = None
                    target_id = None
            # "HELLO_ACK" needs no action beyond refreshing last_reply_time

        # ---- [F2] draw the field: edge, safe margin, and a circle around each colour zone ---
        # [REMOVED] file 1's plain yellow numbered circles drawn from COLOR_ZONES_PX
        draw_field_overlay(frame, target_id)

        # ---- [F2] find the robot every frame so its heading / arm end are always drawn -------
        pose = get_robot_pose(frame)
        if pose is not None:
            draw_robot_overlay(frame, pose, target)

        # ---- navigation --------------------------------------------------------------
        if state == "NAVIGATING":
            print("Navigating state")

            # [F2] uses pose from get_robot_pose()
            # [REMOVED] file 1's inline gray/detectMarkers/corner math and its inline drawDetectedMarkers
            if pose is not None:
                robot_heading = pose["heading"]
                arm_x, arm_y = pose["arm_x"], pose["arm_y"]

                target_x, target_y = target
                desired_heading = math.atan2(target_y - arm_y, target_x - arm_x)
                distance = math.hypot(target_x - arm_x, target_y - arm_y)

                error = desired_heading - robot_heading
                error = math.atan2(math.sin(error), math.cos(error))  # wrap to [-pi, pi]

                if distance < ARRIVAL_RADIUS:
                    send_command("ARRIVED")
                    print("Sent arrived command.")
                    last_arrived = now
                    state = "RELEASING"
                    print("Arrived - asking ESP32 to release the stone (waiting for PLACED)")
                else:
                    # [F1] proportional steering
                    # [REMOVED] file 2's old TURN_LEFT / TURN_RIGHT / FORWARD three-way decision
                    error_deg = math.degrees(error)

                    if abs(error_deg) > REALIGN_THRESHOLD_DEG:
                        # way off - spin in place first rather than a huge slow arc
                        spin_pos = "SPIN_LEFT" if INVERT_TURNS else "SPIN_RIGHT"
                        spin_neg = "SPIN_RIGHT" if INVERT_TURNS else "SPIN_LEFT"
                        command = spin_pos if error_deg > 0 else spin_neg
                        send_command(command)
                    else:
                        # smooth steering: always drive forward, wheel speeds differ by error
                        steer = max(-MAX_STEER, min(MAX_STEER, error_deg * STEER_GAIN))
                        if INVERT_TURNS:
                            steer = -steer
                        left = BASE_SPEED + steer
                        right = BASE_SPEED - steer
                        # [NEW] clamp now uses MIN_WHEEL_SPEED / MAX_WHEEL_SPEED (were literal 160 / 200)
                        left = max(MIN_WHEEL_SPEED, min(MAX_WHEEL_SPEED, int(left)))
                        right = max(MIN_WHEEL_SPEED, min(MAX_WHEEL_SPEED, int(right)))
                        send_command(f"DRIVE,{left},{right}")

                cv2.putText(frame, f"dist={distance:.1f} err={math.degrees(error):.1f}deg",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            else:
                # marker not visible this frame - stop rather than drive blind
                send_command("STOP")

        elif state == "RELEASING":
            print("Releasing state")
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

        
        panel = render_log_panel(frame.shape[0], log_panel.entries)
        cv2.imshow("Field camera", np.hstack([panel, frame]))   # terminal panel on the left
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    send_command("STOP")
    cap.release()
    cv2.destroyAllWindows()
    sys.stdout = tee.real


if __name__ == "__main__":
    main()

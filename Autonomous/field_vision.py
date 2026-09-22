
import cv2
import numpy as np
import socket
import math

# ============================ CONFIG - fill these in ============================

ESP32_IP = "192.168.1.50"     # printed by the ESP32 over Serial when it connects to WiFi
ESP32_PORT = 4210             # must match LOCAL_UDP_PORT in the .ino
LISTEN_PORT = 4211            # this script's own port; must match LAPTOP_PORT in the .ino

CAMERA_INDEX = 0              # which webcam (0 is usually the first/only one)
MARKER_ID = 7                 # the ArUco marker ID you print and stick on the robot
MARKER_DICT = cv2.aruco.DICT_4X4_50

# Step 1 of calibration: with the camera in its final mounted position, take one
# photo of the empty field and read off the pixel (x, y) of each of these 4
# corners in an image viewer. Order: top-left, top-right, bottom-right, bottom-left.
IMAGE_PTS = np.array([
    [0, 0],       # TODO: pixel coords of field's top-left corner
    [0, 0],       # TODO: top-right
    [0, 0],       # TODO: bottom-right
    [0, 0],       # TODO: bottom-left
], dtype=np.float32)

# Step 2: the real size of the field (any consistent unit - cm is convenient).
FIELD_W, FIELD_H = 200, 150   # TODO measure your actual field
FIELD_PTS = np.array([
    [0, 0], [FIELD_W, 0], [FIELD_W, FIELD_H], [0, FIELD_H],
], dtype=np.float32)

# Step 3: where each color zone's center is, in the same field units as above.
# Measure these once the field is set up.
COLOR_ZONES = {
    1: (20, 20),     # Orange   TODO
    2: (180, 20),    # Blue     TODO
    3: (20, 130),    # Purple   TODO
    4: (180, 130),   # Green    TODO
    5: (100, 20),    # Cyan     TODO
    6: (100, 130),   # Red      TODO
}

HEADING_TOLERANCE = math.radians(8)   # how close to "pointed at the target" counts as aligned
ARRIVAL_RADIUS = 8                    # field units - how close counts as "arrived"

# ==================================================================================

H, _ = cv2.findHomography(IMAGE_PTS, FIELD_PTS)

aruco_dict = cv2.aruco.getPredefinedDictionary(MARKER_DICT)
aruco_params = cv2.aruco.DetectorParameters()
detector = cv2.aruco.ArucoDetector(aruco_dict, aruco_params)

send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
listen_sock.bind(("", LISTEN_PORT))
listen_sock.setblocking(False)


def pixel_to_field(px, py):
    """Turn a camera pixel coordinate into a real field coordinate."""
    pt = np.array([[[px, py]]], dtype=np.float32)
    out = cv2.perspectiveTransform(pt, H)
    return float(out[0][0][0]), float(out[0][0][1])


def send_command(cmd):
    send_sock.sendto(cmd.encode(), (ESP32_IP, ESP32_PORT))


def check_for_pickup_message():
    """Non-blocking check: did the ESP32 just tell us it picked up a stone?"""
    try:
        data, _ = listen_sock.recvfrom(64)
        text = data.decode().strip()
        if text.startswith("PICKED,"):
            return int(text.split(",")[1])
    except BlockingIOError:
        pass
    except (ValueError, IndexError):
        pass
    return None


def main():
    cap = cv2.VideoCapture(CAMERA_INDEX)
    target = None  # (x, y) field coords of the current target zone, or None if idle

    while True:
        ok, frame = cap.read()
        if not ok:
            continue

        picked_id = check_for_pickup_message()
        if picked_id is not None and picked_id in COLOR_ZONES:
            target = COLOR_ZONES[picked_id]
            print(f"Robot picked color {picked_id} -> heading to {target}")

        if target is not None:
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
                    print("Arrived - releasing stone")
                    target = None
                elif error > HEADING_TOLERANCE:
                    send_command("TURN_LEFT")   # TODO: verify this matches your robot's
                elif error < -HEADING_TOLERANCE:  #       actual left/right - swap if backwards
                    send_command("TURN_RIGHT")
                else:
                    send_command("FORWARD")

                cv2.aruco.drawDetectedMarkers(frame, corners, ids)
                cv2.putText(frame, f"dist={distance:.1f} err={math.degrees(error):.1f}deg",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            else:
                # marker not visible this frame - stop rather than drive blind
                send_command("STOP")

        cv2.imshow("Field camera", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()

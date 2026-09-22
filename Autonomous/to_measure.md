<img width="1202" height="693" alt="Field+cord" src="https://github.com/user-attachments/assets/a120a48d-4600-4ff7-8f8a-329b912485f7" />"""
field_vision.py
----------------
Runs on the laptop/PC next to the field (NOT on the ESP32).

What it does, every frame:
  1. Grabs a frame from the overhead camera.
  2. Finds the ArUco marker mounted on top of the robot -> gives (x, y, heading).
  3. Converts that from pixel coordinates to real field coordinates using a
     homography computed once at startup.
  4. If the robot has told us (over UDP) which color it just picked up,
     figures out which way to turn / whether to drive forward to reach that
     color's zone, and sends a short text command to the ESP32.

Protocol (matches Pickup_and_Placing_code.ino):
  ESP32  -> laptop : "PICKED,<color_id>"   (sent once, right after pickup)
  laptop -> ESP32  : "TURN_LEFT" / "TURN_RIGHT" / "FORWARD" / "STOP" / "ARRIVED"

Install once:  pip install opencv-contrib-python numpy
"""

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

Pixel Coordinates from field camera 

Upper Left Corner:  176, 4
Upper Right Corner: 1908, 46
Lower Left Corner:  180, 1010
Lower Right Corner: 1867, 1055
Green:  419, 373
Purple: 407, 745
Red:    816, 174
Blue:   1175, 198
Cyan:   816, 901
Orange: 1158, 894

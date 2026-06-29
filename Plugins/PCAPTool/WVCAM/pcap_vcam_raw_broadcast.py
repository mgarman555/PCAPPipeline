# pcap_vcam_raw_broadcast.py
#
# Runs INSIDE WVCAM (Python tab). Instead of mapping the controller and driving
# MotionBuilder/Unreal (like vcam_default.py / vcam_sony.py), it broadcasts the RAW
# controller state of the real "vcam" device over UDP. The Unreal PCAP "VCam Operator"
# panel listens for these packets and does ALL the mapping itself (deadband, gain stack,
# button layout, transform). WVCAM becomes a silent translator.
#
# Axis/button names below match device_maps.py for the "vcam" device EXACTLY, so the
# Unreal side (FVCamControllerInput / PCAPVCamSubsystem::OnInputPacket) can consume them
# without guessing. Values are sent as RAW ABSOLUTE counts (deadband=0, normalize=False):
# Unreal applies its own start-relative baseline, 100-count deadband, and gain math, so the
# port matches WVCAM's vcam_default.py / vcam_sony.py rather than re-deriving constants here.
#
# ── SETUP (WVCAM, on the operator PC) ────────────────────────────────────────
#   1. WVCAM -> Python tab -> Load -> pick this file. Edit HOST/PORT below if needed.
#   2. Execute. In the device's mapping combo box, choose "PCAP Raw Broadcast".
#   3. Set HOST to the machine running Unreal (127.0.0.1 if it's the same PC); PORT must
#      match the VCam config's InputBroadcastPort (default 7401).
#
# ── TEST IT STANDALONE (no Unreal needed) ────────────────────────────────────
#   On the target machine, run this tiny listener and wiggle the sticks/wheels:
#       python -c "import socket,json; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); \
#                  s.bind(('0.0.0.0',7401)); \
#                  [print(json.loads(s.recvfrom(2048)[0])) for _ in iter(int,1)]"
#   You should see left_left_x / left_gain / right_right_y etc. change as you move the rig.

import re
import json
import socket

import vcam
from vcontrols.bindings import BindingBase

# ── Network target — edit for your setup ─────────────────────────────────────
HOST = "127.0.0.1"   # machine running Unreal (same PC as WVCAM = localhost)
PORT = 7401          # must match the Unreal VCam config's InputBroadcastPort

# Axes to broadcast — names + the "vcam" device (device_maps.py). Sent as raw absolute counts.
AXES = ["left_left_x", "left_left_y", "left_right_x", "left_right_y", "left_gain",
        "right_left_x", "right_left_y", "right_right_x", "right_right_y", "right_gain"]

# Buttons to broadcast — the full "vcam" button set (device_maps.py).
BUTTONS = ["left_x", "left_y", "left_a", "left_b", "left_up", "left_down", "left_left", "left_right",
           "right_x", "right_y", "right_a", "right_b", "right_up", "right_down", "right_left", "right_right"]


class VcamRawBroadcast(BindingBase):
    # Bind to the same physical device as vcam_default.py / vcam_sony.py.
    DEVICE_NAME = 'vcam'

    def __init__(self, vcamControllerID):
        try:
            deviceName = 'xbcam%d' % vcamControllerID
        except TypeError:
            # The GUI passes the full device name as a string.
            deviceName = vcamControllerID

        super(VcamRawBroadcast, self).__init__(deviceName)

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._seq = 0

    @classmethod
    def acceptsDevice(cls, deviceName):
        return bool(re.match(r"xbcam\d+", deviceName))

    @classmethod
    def getDisplayName(cls):
        return 'PCAP Raw Broadcast'

    def update(self):
        super(VcamRawBroadcast, self).update()

        packet = {"seq": self._seq}
        self._seq += 1

        # Raw absolute counts: deadband 0 + normalize False so Unreal sees the true value
        # and applies its own deadband / start-relative baseline / gain stack.
        for axis in AXES:
            packet[axis] = self.getAxisValue(axis, deadband=0, normalize=False)
        for name in BUTTONS:
            packet[name] = 1 if self.isButtonPressed(name) else 0

        try:
            self._sock.sendto(json.dumps(packet).encode("utf-8"), (HOST, PORT))
        except Exception as exc:
            print("[PCAP] raw broadcast send failed: %s" % exc)

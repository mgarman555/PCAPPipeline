# pcap_vcam_raw_broadcast.py
#
# Runs INSIDE WVCAM (Python tab), exactly like vcamv4_default.py — but instead of
# mapping the tombstone and driving MotionBuilder, it broadcasts the RAW controller
# state (every axis, encoder, and button) over UDP. The Unreal PCAP "VCam Operator"
# panel listens for these packets and does all the mapping itself.
#
# This makes WVCAM a silent translator: it only reads the USB tombstone and re-emits
# the raw values. All button layouts / gains / camera logic live in Unreal.
#
# ── SETUP (WVCAM, on the operator PC) ────────────────────────────────────────
#   1. WVCAM → Python tab → Load → pick this file. Edit HOST/PORT below if needed.
#   2. Execute. In the device's mapping combo box, choose "PCAP Raw Broadcast".
#   3. Set HOST to the machine running Unreal (127.0.0.1 if it's the same PC).
#
# ── TEST IT STANDALONE (no Unreal needed) ────────────────────────────────────
#   On the target machine, run this tiny listener in a terminal and wiggle the stick:
#       python -c "import socket,json; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); \
#                  s.bind(('0.0.0.0',7401)); \
#                  [print(json.loads(s.recvfrom(2048)[0])) for _ in iter(int,1)]"
#   If you see the numbers change as you move the controller, Option A works.

import re
import json
import socket
from functools import partial

import vcam
from vcontrols.bindings import BindingBase

# ── Network target — edit for your setup ─────────────────────────────────────
HOST = "127.0.0.1"   # machine running Unreal (same PC as WVCAM = localhost)
PORT = 7401          # must match the Unreal VCam panel's listen port

# Raw inputs to read every frame (names match the WVCAM device bindings).
AXES = ["left_joy_x", "left_joy_y", "right_joy_x", "right_joy_y", "left_enc", "right_enc"]
BUTTONS = ["left_trigger", "right_trigger",
           "left_up", "left_down", "left_left", "left_right",
           "right_up", "right_down", "right_left", "right_right",
           "left_a", "left_b", "right_a", "right_b"]


class VcamRawBroadcast(BindingBase):
    DEVICE_NAME = 'vcamv4'

    def __init__(self, vcamControllerID):
        try:
            deviceName = 'xbv4cam%d' % vcamControllerID
        except TypeError:
            # The GUI passes the full device name as a string.
            deviceName = vcamControllerID

        super(VcamRawBroadcast, self).__init__(deviceName)

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._buttons = {name: 0 for name in BUTTONS}
        self._seq = 0

    @classmethod
    def acceptsDevice(cls, deviceName):
        return bool(re.match(r"xbv4cam\d+", deviceName))

    @classmethod
    def getDisplayName(cls):
        return 'PCAP Raw Broadcast'

    # Button edges flip a stored flag; we broadcast the current flag each frame.
    def _press(self, name):
        self._buttons[name] = 1

    def _release(self, name):
        self._buttons[name] = 0

    def updateButtons(self):
        for name in BUTTONS:
            self.bindButton(name,
                            onPress=partial(self._press, name),
                            onRelease=partial(self._release, name))

    def update(self):
        super(VcamRawBroadcast, self).update()
        self.updateButtons()

        packet = {"seq": self._seq}
        self._seq += 1

        # Axes/encoders: deadband 0 so Unreal sees the true raw value and applies its own.
        for axis in AXES:
            packet[axis] = self.getAxisValue(axis, deadband=0.0)
        for name in BUTTONS:
            packet[name] = self._buttons[name]

        try:
            self._sock.sendto(json.dumps(packet).encode("utf-8"), (HOST, PORT))
        except Exception as exc:
            print("[PCAP] raw broadcast send failed: %s" % exc)

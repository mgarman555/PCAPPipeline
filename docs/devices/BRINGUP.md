# Hardware bring-up checklist (Phase 2 entry)

What happens when the webcam, Rode and OneHMC are connected. Madi's steps are marked **[Madi]**; the
rest is the agent running the probes below and filling the fact sheets. Every probe is read-only
toward the device (no settings changed, no OneHMC recording before firmware >= 0.2.0 is confirmed),
and writes to `Saved/DeviceProbes/<device>/` (gitignored).

Probes were dry-run on 2026-10-14 against the devices then present (MS2109 HDMI dongle, Focusrite
input): both completed and reproduced the device agents' measurements.

## 0. Before plugging anything in [Madi]

- [ ] Windows Settings > Privacy & security > Camera and Microphone: "Let desktop apps access" = On.
      (Checked 2026-10-14: both Allow. Re-check only if a probe reports black frames or silence.)
- [ ] Use USB 3 ports directly on the P620 (no hub) and **data** cables (Rode kits often ship charge-only USB-C).

## 1. Body webcam (~3 min)

1. **[Madi]** Plug the webcam into a rear USB 3 port. Point it at something lit (not a black wall).
2. Run:
   ```
   powershell -ExecutionPolicy Bypass -File Tools\probes\bodycam_probe.ps1
   ```
   It picks the first non-virtual camera; pass `-Device "<name>"` if the dongle is picked instead
   (it warns when the device is the MS2109).
3. Read `Saved\DeviceProbes\body-camera\summary-*.md`:
   - every offered mode, and **real** fps per candidate (empty padding frames excluded),
   - mean luma (black = lens cap / privacy / no light),
   - the recommended mode (4K60 if real, else 1080p60 over 4K30),
   - the one-owner recipe result: recording fps at CRF 18 plus UDP preview frames.
4. Agent updates `body-camera.md` §2/§3/§6 and adds PASS lines to `body-camera-verification.md`.

## 2. Rode receiver (~1 min)

1. **[Madi]** Plug the receiver in, power on and pair the transmitter. Note the model on the
   transmitter label (GO II / GO Gen 3 / PRO / ME).
2. Run (and **speak into the transmitter during the 10 s recording**):
   ```
   powershell -ExecutionPolicy Bypass -File Tools\probes\rode_probe.ps1
   ```
3. Summary gives: RODE PnP name (VID 19F7), Windows default format, dshow formats, test file rate /
   channels, level, and a verdict (OK / SILENT / LOW / CLIPPING).
4. If the model records on-board (GO II, GO Gen 3, PRO): **[Madi]** turn on-board recording on in
   RØDE Central as the safety backup (handoff §4.2(c)).
5. Agent updates `rode.md` §1–§3.

## 3. OneHMC (~10 min, needs Madi's answers first)

1. **[Madi]** Update to firmware 0.2.0, power on, and tell the agent:
   - the hostname (`soma-xxxx.local`) or IP,
   - whether the Soma joins the house network or makes its own Wi-Fi
     (**the P620 has no Wi-Fi adapter**: an own-network Soma is unreachable without a USB Wi-Fi
     adapter or Ethernet),
   - whether the Soma has an Ethernet port,
   - whether the storage is removable (card / drive).
2. **[Madi]** Confirm `http://soma-xxxx.local` opens in a browser on the P620.
3. Run:
   ```
   powershell -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Discover
   powershell -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Inspect -Target soma-xxxx.local
   ```
   Inspect saves the GUI's HTML/JS and lists endpoint candidates. The agent reads the JS and the
   browser's network requests to find firmware, mono/stereo, preview, record, list and download.
4. Only after the firmware reads **>= 0.2.0**: short timed mono and stereo test recordings, then
   `-Mode Download -Url <observed file URL>` to time the download. These give the §4.5 numbers
   (GB/min mono and stereo, MB/s), which decide the offload mode and your mono/stereo call (§11 Q1).
5. Agent updates `onehmc.md` and writes `onehmc-verification.md`.

## 4. Still open, independent of the hardware

- **CaptureRoot drive:** C: (~1.1 TB free) or G: (~1.66 TB free)? There is no D:.
- **Vicon DLL:** `ViconDataStreamSDK_CPP.dll` is missing, so the editor will not open with
  `LiveLinkViconDataStream` enabled. Drop the DLL next to the `.lib` in
  `Plugins/LiveLinkViconDataStream/Source/ThirdParty/ViconDataStreamSDK/`, or disable the plugin for
  this build.
- **Markerless Motion Capture plugin** from Fab (Madi's login).
- **Guideline images:** `fetch_guidelines.ps1` is blocked by Epic's bot check; fetch in a browser.
- Python 3.11, `gh`, `tshark` not installed (only needed later: pose preview, PRs, packet capture).

## 5. Gate to Phase 2

Phase 2 code for a device starts only when its fact sheet has the observed behavior and a PASS
line in `<device>-verification.md`, and Madi has signed off Phase 1.

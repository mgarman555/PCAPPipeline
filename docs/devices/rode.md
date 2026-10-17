# Device fact sheet: Rode wireless audio (receiver on the P620)

**Date of every observation below:** 2026-10-14 · **Host:** P620, Windows 11 Pro 10.0.26300, UE 5.8.1 (CL 56057345)
**Author:** audio device agent (observation only; no plugin code written)
**Spec:** `docs/handoffs/HOME_REBUILD_HANDOFF.md` §4.2(c), §8

**Status:**
- Hardware sections (1, 2, 3): **BLOCKED.** The Rode receiver is not connected to the P620, and Windows has no record of it ever being connected.
- Unreal section (4): **complete.** It comes from reading the UE 5.8 engine source.

---

## 0. What the owner must do to unblock the hardware checks

1. Plug the Rode **receiver** into a P620 USB port with a **data** USB-C cable. Charge-only cables are common with these kits and enumerate nothing. Turn the receiver and transmitter on and confirm they are paired (link light solid).
2. In Windows Settings > System > Sound > Input, confirm a new input appears. Expected name: something containing "RODE" / "Wireless", USB VID `0x19F7`.
3. Look at the label on the **transmitter**, or the model shown in RØDE Central, and tell the agent the exact model (Wireless GO II, Wireless GO (Gen 3), Wireless PRO, Wireless ME, ...).
4. Re-run this agent. It then runs §1–§3: model from VID/PID, formats, a 10 s 48 kHz test recording into `<repo>\Saved\DeviceProbes\rode\`, and a silence check.

---

## 1. Enumeration (hardware): BLOCKED, not present

| Check | Command | Result (2026-10-14) |
|---|---|---|
| Present PnP devices with VID_19F7 or a name matching rode/wireless | `Get-PnpDevice -PresentOnly \| ? { $_.InstanceId -match 'VID_19F7' -or $_.FriendlyName -match 'rode\|wireless' }` | **none** |
| All PnP devices, including non-present / previously seen | `Get-PnpDevice \| ? { ... same filter ... }` | **none.** No ghost entry, so it was likely never enumerated on this PC |
| USB enum registry | `HKLM:\SYSTEM\CurrentControlSet\Enum\USB` keys matching `19F7` | **none** |
| WMI | `Get-CimInstance Win32_PnPEntity \| ? { DeviceID -match 'VID_19F7' -or Name -match 'rode' }` | **none** |
| DirectShow audio inputs | `ffmpeg -hide_banner -list_devices true -f dshow -i dummy` (ffmpeg 9.0.1) | `Audio Technica Microphone (Realtek USB Audio Front)`, `Analogue 1 + 2 (Focusrite USB Audio)`, `Digital Audio Interface (USB Digital Audio)`, `Microphone (Steam Streaming Microphone)` |
| WASAPI capture endpoints, including unplugged (this is the list UE's WASAPI backend enumerates) | `HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\*` | Active (state=1): Audio Technica Mic (Realtek USB Audio Front), Analogue 1 + 2 (Focusrite), Digital Audio Interface (USB Digital Audio), Steam Streaming Microphone. Inactive history: Realtek Rear mic/line, Arctis 5 Chat, eMeet C960 webcam mic, Steam Streaming Speakers. **No Rode endpoint, current or historical.** |

Present USB VIDs for reference: 046D (Logitech Lightspeed), 058F, 05E3 hubs, 1235:8210 (Scarlett 2i2), 17AA:1046/104D (Lenovo/Realtek audio), 1A86 hub, 33AE (BEACN Mix Ctrl), 343C, 3554, 534D:2109 (MacroSilicon MS2109 HDMI capture; its audio is "USB Digital Audio"), BEEF. **No 19F7.**

Windows microphone privacy (checked so a later silent recording isn't misdiagnosed):

| Key | Value |
|---|---|
| `HKLM\...\CapabilityAccessManager\ConsentStore\microphone` `Value` | `Allow` |
| `HKCU\...\ConsentStore\microphone` `Value` | `Allow` |
| `HKCU\...\ConsentStore\microphone\NonPackaged` `Value` (desktop apps: UnrealEditor, ffmpeg) | `Allow` |

Microphone access for desktop apps is **on**. A silent recording from a present device is therefore not explained by privacy settings as of today.

## 2. Formats, test recording, silence check: BLOCKED

Not run because there is no device. Commands to run once it is present (`<name>` is the exact dshow name from §1):

```
ffmpeg -hide_banner -f dshow -list_options true -i audio="<name>"
ffmpeg -hide_banner -f dshow -sample_rate 48000 -i audio="<name>" -t 10 -c:a pcm_s24le "<repo>\Saved\DeviceProbes\rode\rode_10s_48k.wav"
ffprobe -hide_banner -show_streams "<repo>\Saved\DeviceProbes\rode\rode_10s_48k.wav"
ffmpeg -hide_banner -i "<repo>\Saved\DeviceProbes\rode\rode_10s_48k.wav" -af volumedetect,astats -f null -
```

Also record the endpoint's Windows default format (Sound control panel > device > Advanced, or MMDevices `{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0` property). **This is the rate UE records at before resampling** (see §4.3).

## 3. Model and on-board recording: UNKNOWN (model not identified)

The spec enum value is `EAudioSystem::RodeWirelessGo2` (`Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolTypes.h:45`). **Unverified:** the owner described the kit only as "square transmitter/receiver style", and several Rode models fit that description. Until the receiver enumerates, or the owner reads the label, the model is **undetermined**.

What Rode says about on-board (transmitter) recording, by model:

| Model | On-board recording in transmitter | Source |
|---|---|---|
| Wireless GO II | **Yes.** "On-board recording – over 40 hours of internal memory"; "each transmitter featuring on-board recording". Recordings are accessed and exported via RØDE Central. A secondary source puts it at 24-bit/48 kHz max, not 32-bit float. | https://rode.com/en/microphones/wireless/wireless-go-ii ; secondary: https://www.bonedo.de/artikel/rode-wireless-go-ii-test/ |
| Wireless GO (Gen 3) | **Yes**, 32-bit float, both transmitters. Files can be copied directly from the TX or exported via RØDE Central. Recording mode Manual or Always; the receiver's Ø button can start/stop it. | https://rode.com/en/user-guides/wireless-go-gen-3/on-board-recording-and-32-bit-float |
| Wireless PRO | **Yes.** "32GB internal memory per transmitter for over 40 hours of on-board recording time", "32-bit float on-board recording" | https://rode.com/en/microphones/wireless/wireless-pro |
| Wireless ME | **Not stated** on rode.com's product page. A retailer comparison says "No onboard recording" (secondary, unconfirmed by Rode). | https://rode.com/en/microphones/wireless/wireless-me ; secondary: retailer listings found via web search 2026-10-14 |
| Wireless GO (Gen 1, 2019) | Not checked this session. **Unverified.** | — |

Receiver over USB:
- Wireless GO II: secondary sources (Rode launch news, bonedo.de review) say the receiver's USB-C is a **24-bit / 48 kHz** audio interface. Rode's product page only says "USB-C and iOS digital output", with no rate.
- Wireless ME: retailer listings say 24-bit / 48 kHz USB-C.

**Not yet observed on this PC for any model.** In particular, it is unverified whether a two-transmitter GO II shows up as 1 or 2 input channels over USB, or which channel carries TX1. The Take Recorder channel number (§4.2) depends on this.

**Recommendation (pending the model):** if it is GO II, GO Gen 3, or PRO, enable on-board recording on the transmitter as the spec's safety backup (§4.2(c)). Offload the files with RØDE Central, or by direct copy on Gen 3, into the take folder.

---

## 4. Unreal Engine 5.8 side (complete; read from engine source, read-only)

Engine root: `C:\Program Files\Epic Games\UE_5.8\Engine`. All paths below are relative to it.

### 4.1 The Take Recorder source that records a microphone

| Item | Value | Source |
|---|---|---|
| Class | `UTakeRecorderMicrophoneAudioSource` (UI display "Microphone Audio Recorder", Category "Audio") | `Plugins/VirtualProduction/Takes/Source/TakeRecorderSources/Public/TakeRecorderMicrophoneAudioSource.h:77-79` |
| Parent | `UTakeRecorderMicrophoneAudioSourceSettings` (Abstract), which derives from `UTakeRecorderSource` | same file, :23-24 |
| Module | `TakeRecorderSources` (Runtime, PostDefault), plugin **Take Recorder** (`Takes.uplugin`) | `Plugins/VirtualProduction/Takes/Takes.uplugin` |
| Class path | **`/Script/TakeRecorderSources.TakeRecorderMicrophoneAudioSource`** | module + class name; same pattern PCAPTool already uses for `/Script/TakeRecorderSources.TakeRecorderActorSource` (`PCAPTakeRecorderSubsystem.cpp:341`) |
| Max mic sources per take | 8 (`FAudioInputDeviceProperty::MaxInputChannelCount`), enforced in `CanAddSource` | `TakesCore/Public/TakeRecorderSourceProperty.h:152`; `TakeRecorderMicrophoneAudioSource.cpp` `CanAddSource` |
| Editor only | Yes. In non-editor builds `StartRecording` logs "Microphone Audio Source is not currently supported in Cooked/Runtime builds". In `-game` with an audio thread it refuses to start (set `UseAudioThread=false`). | `TakeRecorderSources/Private/TakeRecorderMicrophoneAudioManager.cpp` `StartRecording` |

Per-source properties:

| Property | Notes |
|---|---|
| `AudioSourceName` (FText) | config |
| `AudioTrackName` (FText) | config, NamingTokens |
| `AudioAssetName` (FString) | config, tokens `{day}{month}{year}{hour}{minute}{second}{take}{slate}` |
| `AudioSubDirectory` (FString) | config |
| `AudioGain` (float dB) | Clamped 0..40 |
| `bReplaceRecordedAudio` (bool) | |
| `AudioChannel` (`FAudioInputDeviceChannelProperty` → `int32 AudioInputDeviceChannel`) | Source: `TakeRecorderMicrophoneAudioSource.h:41-108`, struct in `TakesCore/Public/TakeRecorderSourceProperty.h:175-185` |

### 4.2 How the input device and channel are chosen

- **Device is global, not per source.** The device lives on the CDO of `UTakeRecorderMicrophoneAudioManager`. That is `/Script/TakeRecorderSources.TakeRecorderMicrophoneAudioManager`, `config=EditorPerProjectUserSettings, PerObjectConfig`, display "Audio Input Device". Every mic source reaches it via `GetMutableDefault<UTakeRecorderMicrophoneAudioManager>()` (`TakeRecorderMicrophoneAudioSource.cpp:131-133`).
- The device is set by property `AudioInputDevice` (`FAudioInputDeviceProperty`, `TakesCore/Public/TakeRecorderSourceProperty.h:146-169`):
  - `bUseSystemDefaultAudioDevice` (config, default **true**)
  - `DeviceId` (config FString). This is the WASAPI endpoint id string; the recorder matches it exactly (`AudioRecordingManager.cpp:479-497`).
  - `AudioInputBufferSize` (config, default 1024, 256..8192)
  - `DeviceInfoArray` (transient: name, id, InputChannels, PreferredSampleRate, bIsDefaultDevice)
- **Recording always uses `DeviceId`.** `StartRecording` passes `AudioInputDevice.DeviceId` (`TakeRecorderMicrophoneAudioManager.cpp` `StartRecording`). `bUseSystemDefaultAudioDevice` only makes the details-panel customization write the default device's id into `DeviceId` (`TakeRecorderEditor/Private/Customization/TakeRecorderAudioSettingsCustomization.cpp:181, 296`).
  - Programmatic route: `SetAudioInputDevice(...)` (BlueprintCallable) with `bUseSystemDefaultAudioDevice=false` and the Rode's `DeviceId`. It saves config on the CDO and broadcasts `OnNotifySourcesOfDeviceChange`.
  - Device list: call `EnumerateAudioDevices(true)`, then read `GetAudioInputDevice().DeviceInfoArray`, and pick the entry whose `DeviceName` matches the Rode.
  - These are UFUNCTIONs and UPROPERTYs on a type in `TakesCore`/`TakeRecorderSources`, so they are reachable by reflection the same way `AddLiveLinkSource` resolves its class.
- **Channel is 1-based, per source.** `AudioChannel.AudioInputDeviceChannel`: 0 = none, 1..N = device channel. On `Initialize()`, each source auto-picks the first free channel (`DoAudioSetup`, `TakeRecorderMicrophoneAudioSource.cpp:88-117`). The recorder indexes `InputChannelNumber - 1` (`AudioRecordingManager.cpp:228, 311, 355`). A source with channel 0 yields no asset (`GetRecordedSoundWave` requires `> 0`).
- One `USoundWave` per source/channel, **mono** (`NumChannelsToSerialize = 1`, `AudioRecordingManager.cpp:325, 407`). That fits Epic's one-speaker-per-file rule.

### 4.3 Sample rate and bit depth of the recorded USoundWave

- **Capture:**
  - Stream opened at the device's `PreferredSampleRate`, which on Windows is the WASAPI shared-mode mix format `nSamplesPerSec` (`IAudioClient::GetMixFormat`; `Source/Runtime/AudioCaptureImplementations/Windows/AudioCaputureWasapi/Private/WasapiDeviceEnumeration.cpp:256, 354`).
  - Format: `PCM_16`, all device channels (`Plugins/Runtime/AudioCapture/Source/AudioCaptureEditor/Private/AudioRecordingManager.cpp:77-115`).
  - Shared mode: `AUDCLNT_SHAREMODE_SHARED` (`WasapiInputStream.cpp:53`).
- **Asset: always 44,100 Hz, 16-bit, mono.**
  - `const int32 WAVE_FILE_SAMPLERATE = 44100;` (`AudioCaptureEditor/Private/AudioRecordingManager.h:125`).
  - If the device rate is not 44100, `ProcessRecordedData` resamples with `SampleRateConvert`, a **linear-interpolation** converter with no anti-alias filter (`AudioRecordingManager.cpp:140-180, 242-252`).
  - The asset is written with `SetSampleRate(WAVE_FILE_SAMPLERATE)` (`:406`).
- Not configurable: it is a hard-coded constant in a Private header, with no setting or cvar.
- Windows capture backend: `[Audio] AudioCaptureModuleName=AudioCaptureWasapi` (`Config/Windows/BaseWindowsEngine.ini:3`).

### 4.4 Dependency on the AudioCapture plugin

**Yes.**
- `Takes.uplugin` lists plugin `"AudioCapture", "Enabled": true`.
- `TakeRecorderSources.Build.cs` (editor branch) depends on module `AudioCaptureEditor`.
- The manager creates its recorder via `FModuleManager::LoadModuleChecked<IAudioCaptureEditorModule>("AudioCaptureEditor")` (`TakeRecorderMicrophoneAudioManager.cpp:23-27`). Without it, it logs "Please check that the AudioCapture plugin is enabled".
- `AudioCaptureEditor` (UncookedOnly) lives in `Plugins/Runtime/AudioCapture/`. The interface is `Source/AudioCaptureEditor/Public/IAudioCaptureEditor.h`, and the types are in `AudioCaptureEditorTypes.h`.

### 4.5 Reading a live input level for the alarm

| Option | Header | Fit |
|---|---|---|
| **`Audio::FAudioCapture`** (module `AudioCaptureCore`, engine runtime) | `Source/Runtime/AudioCaptureCore/Public/AudioCaptureCore.h`; params/info in `Source/Runtime/AudioCaptureCore/Public/AudioCaptureDeviceInterface.h` | **Best fit.** `GetCaptureDevicesAvailable(TArray<FCaptureDeviceInfo>&)` gives `DeviceId`; pick the index whose `DeviceId` equals the Take Recorder manager's `DeviceId`. Then `OpenAudioCaptureStream(FAudioCaptureDeviceParams{DeviceIndex, ...}, FOnAudioCaptureFunction, NumFrames)` + `StartStream()`, and compute peak/RMS in the callback. The callback receives `const void*` in the requested `PCMAudioEncoding` (default `FLOATING_POINT_32`). `OpenAudioCaptureStream` is exported: the `UE_API` sits on the preceding line before `PRAGMA_ENABLE_DEPRECATION_WARNINGS` (`AudioCaptureCore.h:51-54`), and `AudioCaptureEditor` already calls it cross-module. WASAPI shared mode means a second stream on the same endpoint can coexist with the Take Recorder's stream. **Still to verify on hardware.** |
| `Audio::FEnvelopeFollower`, `FTruePeak`, `FMeanSquaredIIR` (module `SignalProcessing`) | `Source/Runtime/SignalProcessing/Public/DSP/EnvelopeFollower.h` (classes at :206, :257, :169) | DSP to turn the raw callback buffer into a smoothed level / true peak for the silence and clipping thresholds |
| `UAudioCaptureComponent` / `UAudioCapture` (module `AudioCapture`) | `Plugins/Runtime/AudioCapture/Source/AudioCapture/Public/AudioCaptureComponent.h`, `AudioCapture.h` | **Default device only.** Both open via `OpenDefaultStream()` / `OpenDefaultAudioStream()` (`AudioCaptureComponent.cpp:45,109`, `AudioCapture.cpp:22`). Only usable if the Rode is the Windows default input. |
| Audio Synesthesia `UMeterAnalyzer` (module `AudioSynesthesia`, plugin `Plugins/Runtime/AudioSynesthesia`) | `Plugins/Runtime/AudioSynesthesia/Source/AudioSynesthesia/Classes/Meter.h` (`UMeterSettings::ClippingThreshold`, `FMeterResults`, `OnLatestOverallMeterResults`); core in `AudioSynesthesiaCore/Public/MeterAnalyzer.h` | Analyzes an **audio bus / submix**, so the mic must first be routed into the audio engine. Today that means `UAudioCaptureComponent`, which is default-device only. Heavier, and the meter shows a live dB plus clipping out of the box. |
| `UAudioCaptureBlueprintLibrary::GetAvailableAudioInputDevices` | `Plugins/Runtime/AudioCapture/Source/AudioCapture/Public/AudioCaptureBlueprintLibrary.h:85-86` | Async device list (names/ids) for the Capture Setup "Input" dropdown |

Receiver-missing detection: re-enumerate (`EnumerateAudioDevices(true)` or `FAudioCapture::GetCaptureDevicesAvailable`) and check that the selected `DeviceId` is still listed. The manager has a private `IsAudioDeviceAvailable(DeviceId)` doing exactly that (`TakeRecorderMicrophoneAudioManager.cpp`). **Unverified:** whether Windows drops the endpoint when the transmitter (not the receiver) loses its link. That needs hardware.

---

## 5. Contradictions with the spec / things the architect must decide

1. **"Record at 48 kHz" (§4.2(c)) cannot be met by Take Recorder's microphone source.** UE 5.8 always writes the USoundWave at **44,100 Hz / 16-bit mono**. A 48 kHz device is resampled with linear interpolation (§4.3). That still clears Epic's 16 kHz floor, so it is compliant for MetaHuman Animator. If 48 kHz is a hard requirement, it has to come from another path, for example:
   - the transmitter's on-board files (24/48 on GO II, 32-bit float on Gen 3/PRO);
   - a parallel ffmpeg dshow recording of the receiver;
   - setting the Windows default format of the Rode endpoint to 44.1 kHz so UE does no resampling. The third option is lower quality than native, but avoids the linear SRC.
2. **Enum `RodeWirelessGo2` is unconfirmed.** The model has not been seen. The spec's own §7 already proposes a generic `UsbAudioInput`; this fact sheet supports that.
3. **Device choice is global.** The Take Recorder input device is one per-user-per-project setting on the manager CDO, not a per-source property. The Capture Setup "Input ▾" dropdown therefore writes a global, and every mic source in the take uses that one device. That is fine for one performer. Multiple Rode receivers at once would need a different approach.
4. **Mic recording is editor-only** (§4.1). Fine for this pipeline (PCAPTool runs in the editor), but note it for any `-game` use.

## 6. Unknowns (to resolve when the receiver is connected)

- Exact model, USB PID, firmware.
- dshow / WASAPI device name and endpoint `DeviceId`.
- Supported rates/bit depths, Windows default format, channel count over USB (1 vs 2 with two transmitters).
- Real 10 s recording: duration, rate, level (volumedetect/astats).
- Whether a transmitter link loss is visible to Windows (endpoint removed vs. silent stream).
- Whether a second `FAudioCapture` stream for metering coexists with the Take Recorder stream on this device (expected, since both are WASAPI shared).
- On-board recording: model-dependent (§3).

---

## 7. Owner update (Madi, 2026-10-15), not yet observed on the P620

- **Model: Rode Wireless GO II** (lav). This matches the existing `EAudioSystem::RodeWirelessGo2` value. Still to confirm from the USB name / VID 19F7 with `Tools/probes/rode_probe.ps1`.
- Madi: "the transmitter directly into the P620". Per retailer copies of Rode's spec, the GO II's **computer audio interface is the receiver** (USB-C, 24-bit / 48 kHz); the **transmitter's** USB-C is for charging and for exporting its on-board recordings in RØDE Central. To check when connected: which unit enumerates as an audio input.
  Sources: https://www.bhphotovideo.com/c/product/1622642-REG/rode_wigoii_wireless_go_ii_compact.html , https://www.bhphotovideo.com/c/product/803494831-USE/rode_wigo_ii_single_wireless_go_ii_single.html
- **On-board recording: yes** on GO II (24-bit / 48 kHz WAV, up to ~7 h uncompressed per transmitter, set up in RØDE Central). Exports carry markers where the TX/RX link dropped. Turn it on as the safety backup (handoff §4.2(c)); offload = export from RØDE Central into the take folder.

## 8. Observed 2026-10-16: the connected Rode unit is not an audio input

Plugged into the P620 by Madi ("audio is in now"). Read-only checks:

- **USB:** composite device `USB\VID_19F7&PID_001E` (Rode). Interfaces: `MI_00` HID (USB Input Device plus vendor-defined HID collections, the RØDE Central control channel) and `MI_01` USB Mass Storage, which shows as disk **`RODE RAMBUFFER-UDISK USB Device`** (3726 MB).
- **No audio interface.** `ffmpeg -list_devices true -f dshow` lists no Rode input; the audio inputs are the C920 mic, Realtek, Focusrite, "Digital Audio Interface (USB Digital Audio)" and Steam. `Get-PnpDevice` shows no Rode device of class MEDIA / AudioEndpoint.
- **The disk is not readable by Windows:** drive letter `E:` is assigned but has no file system (size 0, `Test-Path E:\` = False). On-board recordings come off through RØDE Central, not as files.
- **Conclusion:** this is the access the §7 note predicted for the **transmitter** (charging + RØDE Central export). It can be the on-board backup, but it cannot feed Take Recorder. For live audio in Unreal, the **receiver** must be plugged into the P620 over USB-C; it should enumerate as an audio input (to verify: run `Tools/probes/rode_probe.ps1` and speak during the 10 s recording).
- Not changed: no RØDE Central, no device settings touched.
- **Confirmed model and unit (same day, later):** the device's USB-reported name (`DEVPKEY_Device_BusReportedDeviceDesc`) is **`Wireless GO II TX`**: it is the GO II **transmitter**, connected since 18:55 and never re-plugged. After Madi connected the receiver with a second (data) USB-C cable, **no new USB device enumerated** (no PnP arrival, no new dshow audio input). The receiver is not yet visible to Windows.
- **Receiver connected (21:01):** enumerates as `USB\VID_19F7&PID_0014`, serial `FF20500528`, USB-reported name **`Wireless GO RX`** (not "GO II"). Interfaces: HID only (vendor-defined plus consumer control). **No audio interface**: no new dshow input, no MEDIA device. The name suggests a first-generation Wireless GO receiver paired with a GO II transmitter (unverified; the label on the unit would settle it). As far as I know, Rode added USB audio with the GO II, so a Gen 1 receiver would only give analog audio from its 3.5 mm output.
- At the same moment "Audio Technica Microphone (Realtek USB Audio Front)" reappeared in the input list (it had disappeared earlier). 5 s capture: 44.1 kHz, 2 ch, mean -55.5 dB, max -42.6 dB = **silent** (probe threshold -50 dB). Not yet known whether the receiver's 3.5 mm output is cabled to that front jack.
- **Probe with Madi talking into the transmitter (21:08,** `rode_probe.ps1 -Device "Audio Technica Microphone (Realtek USB Audio Front)"`): 48 kHz 2 ch 24-bit, 9.98 s, mean **-71.6 dB**, max **-59.9 dB** = **SILENT**. The receiver's 3.5 mm output reportedly goes into this jack, so no audio is reaching it. Not yet known whether the receiver itself shows the link and a moving level.
- **Wireless link works** (Madi: the receiver's meters move with speech). **Windows endpoint is not the cause:** read-only Core Audio check shows "Audio Technica Microphone (Realtek USB Audio Front)" at 100% (0.0 dB), not muted. So the signal is lost between the receiver's 3.5 mm output and the front jack. Likely a cable mismatch (the receiver outputs TRS; a front combo/headset jack expects TRRS) or the wrong jack. Alternative path: the receiver output into the Focusrite input (already a working 48 kHz USB interface on this PC).

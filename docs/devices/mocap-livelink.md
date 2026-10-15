# Mocap systems: what each needs to reach Unreal (Live Link)

**Date:** 2026-10-15 · **Status:** research only. None of these has been connected on the P620.
Madi: Cortex, Shogun Live and Giant are **placeholders** for now; Markerless UE is the weekend's body path.

The same facts are the defaults of each Mocap entry in **Project Settings > PCAP > PCAP Tool > Capture
Systems > Mocap Systems > Live Link**, and show under the Mocap dropdown in the Stage Database.
Edit them there when a system is actually tested, and tick **Verified**.

| System | How data reaches Unreal | Plugin needed | Connection | Verified |
|---|---|---|---|---|
| Markerless UE | No Live Link. Recorded mono video -> MetaHuman Animator markerless solve after the take | MetaHuman Animator Markerless Motion Capture (Fab, experimental, Windows, UE 5.8+), MetaHuman Animator, Capture Manager Editor, Live Link Hub | n/a | no |
| Shogun Live 1.13 / 1.19 | Vicon DataStream as a Live Link source | Vicon Live Link plugin (`LiveLinkViconDataStream`, in this repo; its SDK DLL is missing on the P620) | Vicon Server Name (IP or computer name of the Shogun PC), port **801** (default) or **804** (Shogun "Always Live" / low-latency) | no |
| Cortex | **Unverified.** No public Cortex Unreal Live Link plugin found. Likely: Cortex -> MotionBuilder -> MotionBuilder Live Link, or Cortex's VRPN server | Ask Motion Analysis | unknown | no |
| Giant | **Unverified.** No public Giant Unreal plugin found. Likely: Giant solve -> MotionBuilder -> MotionBuilder Live Link | MotionBuilder Live Link (Epic) if routed through MotionBuilder | unknown | no |

## Shogun Live: details from Vicon's plugin source (this repo) and docs

Source settings (from `Plugins/LiveLinkViconDataStream/.../LiveLinkViconDataStreamBlueprint.h` and
`LiveLinkViconDataStreamSourceSettings.h`): `ServerName` (default `localhost`), `PortNumber` (`801`),
`SubjectFilter`, `bIsRetimed` (false), `bUsePreFetch` (false), `bIsScaled` (true), `bLogOutput` (false),
`Offset` (0.0), plus `EnableLightweight`, `StreamMarkerData`, `StreamUnlabeledMarkerData`, `ShowAllVideoCamera`.
The stage asset already stores the address (`UStageConfigAsset::DataStreamHost`, default `localhost:801`).

**Version catch:** Vicon lists its UE 5.8 plugin (1.13) as compatible with **Shogun Live 1.14+**.
Shogun Live **1.13** may not connect from UE 5.8. Auto-discovery on UE 5.7+ uses Windows DNS-SD and also
needs Shogun Live 1.14+; manual connection by IP is always available. Shogun Live 1.19 was not covered in
the pages found; check Vicon's release notes before relying on it.

## Sources

- Vicon, Install and set up Live Link (UE5 plugin 1.11): https://help.vicon.com/space/Unreal5Plugin111/337740484/Install+and+set+up+Live+Link
- Vicon, Live Link plugin components overview (1.12): https://vicon-help.atlassian.net/wiki/spaces/Unreal5Plugin112/pages/545850930/Vicon+Live+Link+plugin+components+overview
- Vicon, Install and set up the LiveLink plugin (1.6): https://help.vicon.com/space/Unreal5Plugin16/14713547/Install+and+set+up+the+LiveLink+plugin
- Motion Analysis, Cortex: https://www.motionanalysis.com/software/cortex-software/
- Epic, Live Link: stream MotionBuilder to Unreal: https://dev.epicgames.com/documentation/unreal-engine/live-link-stream-motionbuilder-to-unreal-engine
- Epic, Animation from mono video capture (markerless): https://dev.epicgames.com/documentation/metahuman/metahuman-animation-from-mono-video-capture-in-unreal-engine

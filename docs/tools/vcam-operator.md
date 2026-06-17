# VCam Operator

A virtual camera for the volume — a replacement for WVCAM. Drive a UE cine camera live from a hardware controller.

**Open:** Window ▸ Tools ▸ PCAP Tools ▸ **VCam Operator**.

## How input gets in

Controller input arrives over **UDP**: a WVCAM raw-broadcast `.py` script forwards the controller's raw broadcast → UDP → the engine's `FVCamInputLayer`, which feeds the VCam. Pick the VCam config to use (from the [VCam Database](databases.md)).

> Rig-specific scale constants and platforming are still being tuned on-rig.
>
> Design: [`specs/2026-06-12-vcam-panel-design.md`](../specs/2026-06-12-vcam-panel-design.md) · input layer: [`specs/2026-06-12-vcam-input-layer-design.md`](../specs/2026-06-12-vcam-input-layer-design.md)

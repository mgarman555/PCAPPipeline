# PCAPTool host-side tests

Standalone unit tests for the **pure** VCam logic — `FVCamInputLayer` (WVCAM controller
mapping) and `FPCAPVCamProcessor` (transform/zoom math) — that run with plain `clang++`,
**no Unreal Engine required**. They exist because the CI/dev container has no engine, but the
WVCAM port is exactly the kind of numeric code worth pinning down with fast, runnable tests.

## Run

```bash
Plugins/PCAPTool/HostTests/run.sh      # builds + runs; exit 0 = all green
```

## How it works

`stubs/CoreMinimal.h` reimplements just the slice of the UE math API the two translation
units use — `FVector`, `FRotator`, `FQuat`, `FTransform`, `FMath`, plus no-op reflection
macros — with **UE's exact conventions** (`FQuat A*B` = apply B then A; `FTransform A*B` =
apply A then B; UE's `FRotator`↔`FQuat` formulas). `run.sh` then compiles the *real*
`VCamInputLayer.cpp` / `VCamProcessor.cpp` (and `VCamConfig.h`) against those stubs and links
`test_main.cpp`. So the tests exercise the shipping logic, not a copy.

These mirror the in-editor automation tests (`Source/PCAPTool/Private/Tests/PCAPVCam*Tests.cpp`)
so the same behavior is checked both standalone and inside UE 5.8.

## Scope / caveats

Only the engine-independent logic is covered. The UDP listener, Slate panel, Live Link read,
and actor driving need a real editor. And the deepest WVCAM transform internals (motion
integration clock, hold/zeroSpace re-solve) live in WVCAM's compiled native module, which is
not available — `FPCAPVCamProcessor` reconstructs them and final feel must be tuned on the rig.

This folder is **not** part of the UE build (no `.Build.cs`); UBT never sees it.

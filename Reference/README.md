# Reference/ — local engine source (not committed)

This folder is for **read-only engine plugin source** kept locally so the
PCAPTool ↔ Mocap Manager bridge (`UPCAPMocapBridge`) can be matched against the
real UE 5.8 API instead of from documentation.

Everything under `Reference/` is git-ignored **except this README** — engine
source is Epic-licensed and must never be committed.

## What to drop here

Copy these two plugin `Source/` trees from your UE 5.8 install:

| Plugin | Copy from (typical Windows path) | Into |
|---|---|---|
| Performance Capture Core | `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Animation\PerformanceCaptureCore\Source\` | `Reference/PerformanceCaptureCore/Source/` |
| Performance Capture Workflow | `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\…\PerformanceCaptureWorkflow\Source\` | `Reference/PerformanceCaptureWorkflow/Source/` |

> The Workflow plugin's exact parent folder varies by install — if it isn't
> under `Animation\`, search the engine's `Plugins\` tree for
> `PerformanceCaptureWorkflow.uplugin` and copy the `Source/` next to it.

Resulting layout:

```
Reference/
  README.md                         (tracked)
  PerformanceCaptureCore/Source/     (ignored)
  PerformanceCaptureWorkflow/Source/ (ignored)
```

## The headers that matter

If copying whole trees is awkward, these specific headers are enough to verify
the bridge:

- `PerformanceCaptureCore` → `Public/CapturePerformer.h`, `Public/PerformerComponent.h`
- `PerformanceCaptureWorkflow*` → `…/PCapPropComponent.h`

Identifiers to confirm: `ACapturePerformer::{SetLiveLinkSubject, SetMocapMesh,
SetEvaluateLiveLinkData}`, `UPCapPropComponent::{SetLiveLinkSubject,
SetControlledComponent, SetEvaluateLiveLinkData}`, the include paths, and the
exact module names used in `PCAPTool.Build.cs`.

Google Drive works too — upload the same files and they can be read from there.

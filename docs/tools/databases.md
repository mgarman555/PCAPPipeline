# Databases

The permanent **libraries** — everything ever created, kept as DataAssets. They're pure data (no Live Link) and expose the shared `SPCAPRosterCard`, which the [Call Sheet](call-sheet.md) reuses when it calls things out.

**Open:** Window ▸ Tools ▸ Databases ▸ **Actor · Prop · Stage · VCam · Production**.

| Library | One entry = | Stored at |
|---|---|---|
| Actor | `UActorRosterEntry` (ActorID + name + headshot) | `/Game/PCAPTool/Databases/Actors` |
| Prop | `UPropRosterEntry` (PropID + mesh) | `…/Props` |
| Stage | `UStageConfigAsset` (stage reference mesh + capture setup) | `…/Stages` |
| VCam | `UPCAPVCamConfig` | `…/VCams` |
| Production | `FProduction` struct **inside** the master DB | `…/MasterPCAPDatabase` |

Each library is a **card grid** — click a card to open its detail panel. Create, rename, and delete from the library's toolbar. New entries also appear instantly in the Call Sheet's "+ call" pickers.

> You can create library entries straight from the Call Sheet (the "+ new" fields). The Databases are where you manage and edit them in depth.

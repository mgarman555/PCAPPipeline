# Operator Console

Run the day's takes. Navigate the shot list the [Call Sheet](call-sheet.md) prepped, and drive **RECORD / STOP** through the Take Recorder backend.

**Open:** Window ▸ Tools ▸ PCAP Tools ▸ **Operator Console**.

## Workflow

1. Prep the day in the Call Sheet (production / day / stage / called subjects / shots).
2. In the Console, step through the day's **shots** (sorted; prioritizes the call sheet).
3. Hit **RECORD** to start a take and **STOP** to end it — recording goes through `PCAPTakeRecorderSubsystem`, which writes takes under `/Game/PCAPTool/Productions/`.

Takes are labeled and tracked in the data model (`FTake`), so you can see what's been shot and what's queued.

> Design: [`specs/2026-06-09-pcap-phase2-take-recorder-design.md`](../specs/2026-06-09-pcap-phase2-take-recorder-design.md) · comfort/integration: [`specs/2026-06-12-operator-console-comfort-integration.md`](../specs/2026-06-12-operator-console-comfort-integration.md)

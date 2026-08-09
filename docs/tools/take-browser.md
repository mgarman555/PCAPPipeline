# Take Browser

Post-take management. Find any take in the database, read what was actually recorded on it, label it, and track it through processing. It's the other half of the [Operator Console](operator-console.md) — the Console runs the takes, the Browser is where they go afterwards.

**Open:** Window ▸ Tools ▸ PCAP Tools ▸ **Take Browser**.

Layout: **filters** (day / session / shot / label + search) → **queue bar** → **take list** on the left → **take detail** on the right. Pick a take on the left; everything else is on the right.

## Find a take

Filter by **day**, **session**, **shot** and **label**, plus a search box over take and shot IDs. Every picker has an "(all)" entry, so you can widen back out without resetting the panel. Day IDs repeat across productions, so the day picker sets the production too and shows both (`DA · Day_001`).

The count beside the search box reads *"12 of 340 takes"* — filtered against total, so you always know how much you're not looking at.

## Labels

Four labels, and they decide what happens to the take next:

| Label | Colour | Means |
|---|---|---|
| **Captured** | gray | The default — every take is born here. Not processed. |
| **Best** | gold | The keeper. Enters the processing queue. |
| **Alt** | green | Worth keeping too. Enters the processing queue. |
| **Burn** | red | Archived raw. **Never** processed. |

Label from the take detail — click a chip. This is the only place in the tool that sets a take label, so nothing reaches the queue until you've been through here. Labelling also republishes the take to the Mocap Manager's Review tab so the two copies don't drift; if that fails it's logged and your label still stands.

Re-labelling a take you already queued is allowed, and called out when it matters: mark a queued take **Burn** or **Captured** and you get a toast saying it will no longer be processed, the detail pane says so, and the queue bar counts how many takes are in that state. Nothing you recorded is touched either way.

## The manifest

The detail pane is the take's full record — when it was recorded, its recorded sequence, notes, and the manifest:

- **Performers** — each subject with their ActorID resolved to a real name from the [Actor Database](databases.md), the character they drove, and badges for **body** / **face** / **audio** (hover audio for the channel list).
- **Props** — each prop resolved to its display name, marked tracked or untracked.

One caveat the panel states in place, and you should read literally: those stream badges are **what was armed at record time**, not what stayed connected through the take. It is not a health report.

Fields nothing writes yet read as `—` with a note saying so, rather than a misleading zero — take duration is the main one.

## Processing

Five steps per take. Where each one actually happens:

| Step | Where it happens |
|---|---|
| Body Solve Cleanup | **Shogun Post / Motive** — external |
| HMC Solve | **MetaHuman Animator** or the head-cam vendor's tool — external |
| Body Retarget | In-engine in principle; no automation wired |
| Audio Sync / Trim | In-engine in principle; no automation wired |
| Merge to Sequencer | In-engine in principle; no automation wired |

**The editor performs none of them.** Body solve and HMC solve are not Unreal operations and never will be. The other three could be engine work eventually, but nothing runs them today. So every step is something you do elsewhere and **mark** here, and each step row says where it happens.

Which steps a take needs is computed from its own manifest when you queue it: no face stream, no HMC solve. A step that doesn't apply says so, instead of sitting there looking outstanding.

Per step you get **Mark started**, **Mark complete**, **Reset**, and a field to record a problem — the message is required, and it's where *"failed for actor B, actor A is fine"* has to live, since a step covers the whole take. Anything unavailable is disabled and its tooltip says why. **Reset** puts a step back in the queue along with anything downstream that depended on it, because a result built on a step you just reopened is no longer true.

**Remove from queue** takes a take out of the worklist entirely and clears its progress. The recorded take is untouched.

## "Process All Queued"

The end-of-day batch. It sweeps the **whole database**, not just what your filters are showing.

Before you click, the bar spells out exactly what will happen — how many takes, and which steps they carry (*"Will queue 6 take(s) — Body Solve Cleanup ×6 · HMC Solve ×2 · Body Retarget ×6 · Merge to Sequencer ×6"*). If there's nothing to do the button is disabled and says why, rather than clicking through to a silent no-op.

It **admits takes to the worklist and runs nothing.** After it, each take's steps are outstanding work for you, in Shogun, in MetaHuman Animator, and in the editor. Press it twice and the second press is a no-op — takes already queued are left exactly as they are, including any step plan you adjusted by hand.

> New tab — after a fresh Windows build you must **restart the editor** before it appears under Window ▸ Tools, because tabs register at module startup.
>
> Design: [`specs/2026-08-09-take-browser-and-processing-queue-design.md`](../specs/2026-08-09-take-browser-and-processing-queue-design.md)

# Call Sheet

The shoot-day **prep** tool: declare the production, day, and stage, call out the actors / props / vcam you'll use, and build the shot list. It's day-level — *running* the shots happens in the [Operator Console](operator-console.md).

**Open:** Window ▸ Tools ▸ PCAP Tools ▸ **Call Sheet**.

It's one scrollable page: **header** (Production / Day / Stage + readiness) → **stage setup** → **called Actors / Props / VCam** → **Shots**.

## The "+" creates *or* calls — everywhere

Every "+" is a create-or-call. Type a name, press ↵:

- **Header "+" next to Production / Day** — creates it in the master DB and makes it active.
- **Header "+" next to Stage** — creates the stage config if new, or **pulls the existing one** (e.g. `Osborne_LAEast` / `Osborne_LAWest`) and assigns it to the day. This is how you "call" a stage.
- **In Called Actors / Props / VCam**, the **"+ call"** picker has a **"+ new"** field at the top — it creates the library entry *and* calls it to the day in one step. Existing entries are toggled on via the searchable checklist; called ones show as chips (✕ to un-call).

Anything created here lands in the matching [Database](databases.md) and persists.

## Shots

The **Shots** section is the day's slate list. Add shots by slot, or **Import CSV** to bring in a slate (and **Export CSV** to round-trip). Shots are built here in prep and run in the Operator Console.

## Readiness

The header shows a **Ready / Not ready** summary — what still needs calling out before the day can shoot. You can also **Spawn volume visualizer** from the header to drop a [Volume Visualizer](volume-visualizer.md) for the day's stage.

> Design: [`specs/2026-06-16-callsheet-singlepage-design.md`](../specs/2026-06-16-callsheet-singlepage-design.md)

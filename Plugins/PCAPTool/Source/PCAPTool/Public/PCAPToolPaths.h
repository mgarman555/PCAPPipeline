#pragma once

#include "CoreMinimal.h"

// ── PCAP tool content area — single source of truth ─────────────────────────
// Everything the toolset builds lives under one root, organised into branches so
// it is always findable in the proper place. Current AND future tools/databases
// must derive their paths from here — never hardcode a "/Game/..." path.
//
// NOTE: UE forbids spaces in asset/package paths (INVALID_LONGPACKAGE_CHARACTERS),
// so FOLDER names are space-free PascalCase. Spaces live only in display text
// (e.g. the "PCAP Tools" Window menu group).
//
//   Disk root:   Content/PCAPTool/
//   Asset root:  /Game/PCAPTool/
//
//   /Game/PCAPTool/
//   ├── Databases/        ← the database setup (rosters + master DB)
//   ├── PipelineTools/    ← per-tool asset areas (ShootSetup, RealtimeOperator, HMC, VCam, …)
//   └── Productions/      ← recorded takes (shoot output)
//
// To relocate the entire area, change Root() — everything follows.
namespace PCAPPaths
{
    inline FString Root()           { return TEXT("/Game/PCAPTool"); }

    // Top-level branches.
    inline FString Databases()      { return Root() + TEXT("/Databases"); }
    inline FString PipelineTools()  { return Root() + TEXT("/PipelineTools"); }
    inline FString Productions()    { return Root() + TEXT("/Productions"); }

    // Database sub-areas.
    inline FString ActorsDir()      { return Databases() + TEXT("/Actors"); }
    inline FString PropsDir()       { return Databases() + TEXT("/Props"); }
    inline FString StagesDir()      { return Databases() + TEXT("/Stages"); }
    inline FString MasterDatabase() { return Databases() + TEXT("/MasterPCAPDatabase"); }

    // Per-tool asset area, created on demand as a tool needs to write assets.
    // e.g. ToolDir(TEXT("RealtimeOperator")) → /Game/PCAPTool/PipelineTools/RealtimeOperator
    inline FString ToolDir(const TCHAR* ToolName) { return PipelineTools() + TEXT("/") + ToolName; }
}

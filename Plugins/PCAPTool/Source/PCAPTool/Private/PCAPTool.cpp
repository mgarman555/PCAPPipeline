#include "PCAPToolModule.h"
#include "SPCAPToolPanel.h"
#include "SPCAPActorDatabasePanel.h"
#include "SPCAPPropDatabasePanel.h"
#include "SPCAPStageDatabasePanel.h"
#include "SPCAPOperatorConsole.h"
#include "SPCAPCallSheetPanel.h"
#include "SPCAPVCamPanel.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "PCAPTool"

const FName FPCAPToolModule::HMCTabName       = TEXT("PCAPTool_HMCMonitor");
const FName FPCAPToolModule::ConsoleTabName   = TEXT("PCAPTool_Console");
const FName FPCAPToolModule::CallSheetTabName = TEXT("PCAPTool_CallSheet");
const FName FPCAPToolModule::VCamTabName      = TEXT("PCAPTool_VCam");
const FName FPCAPToolModule::ActorDBTabName   = TEXT("PCAPTool_ActorDB");
const FName FPCAPToolModule::PropDBTabName    = TEXT("PCAPTool_PropDB");
const FName FPCAPToolModule::StageDBTabName   = TEXT("PCAPTool_StageDB");

void FPCAPToolModule::StartupModule()
{
    // Two sibling groups under Window > Tools: the operator tools, and the database setup.
    PCAPMenuGroup = WorkspaceMenu::GetMenuStructure().GetToolsCategory()->AddGroup(
        LOCTEXT("PCAPToolsGroup", "PCAP Tools"),
        LOCTEXT("PCAPToolsGroupTooltip", "Performance capture tools"),
        FSlateIcon());

    DatabasesMenuGroup = WorkspaceMenu::GetMenuStructure().GetToolsCategory()->AddGroup(
        LOCTEXT("PCAPDatabasesGroup", "Databases"),
        LOCTEXT("PCAPDatabasesGroupTooltip", "PCAP databases — actors, props, stages"),
        FSlateIcon());

    // ── PCAP Tools (prep → run → monitor) ──────────────────────────────────
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        CallSheetTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnCallSheetTab))
        .SetDisplayName(LOCTEXT("CallSheetTabTitle", "Call Sheet"))
        .SetTooltipText(LOCTEXT("CallSheetTabTooltip", "PCAP Tool — shoot-day prep (project, stage, day, actors, props)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ConsoleTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnConsoleTab))
        .SetDisplayName(LOCTEXT("ConsoleTabTitle", "Operator Console"))
        .SetTooltipText(LOCTEXT("ConsoleTabTooltip", "PCAP Tool — navigate shots + run takes (solo operator)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        HMCTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnHMCTab))
        .SetDisplayName(LOCTEXT("HMCTabTitle", "HMC Monitor"))
        .SetTooltipText(LOCTEXT("HMCTabTooltip", "PCAP Tool — HMC device monitor"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        VCamTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnVCamTab))
        .SetDisplayName(LOCTEXT("VCamTabTitle", "VCam Operator"))
        .SetTooltipText(LOCTEXT("VCamTabTooltip", "PCAP Tool — virtual camera operator (WVCAM replacement)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    // ── Databases (the separate data setup) ────────────────────────────────
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ActorDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnActorDBTab))
        .SetDisplayName(LOCTEXT("ActorDBTabTitle", "Actor Database"))
        .SetTooltipText(LOCTEXT("ActorDBTabTooltip", "PCAP — the permanent talent library"))
        .SetGroup(DatabasesMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        PropDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnPropDBTab))
        .SetDisplayName(LOCTEXT("PropDBTabTitle", "Prop Database"))
        .SetTooltipText(LOCTEXT("PropDBTabTooltip", "PCAP — the prop library (with mesh previews)"))
        .SetGroup(DatabasesMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        StageDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnStageDBTab))
        .SetDisplayName(LOCTEXT("StageDBTabTitle", "Stage Database"))
        .SetTooltipText(LOCTEXT("StageDBTabTooltip", "PCAP — stages (location + what to record)"))
        .SetGroup(DatabasesMenuGroup.ToSharedRef());
}

void FPCAPToolModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CallSheetTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ConsoleTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HMCTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(VCamTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ActorDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PropDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StageDBTabName);
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnHMCTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPToolPanel) ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnConsoleTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPOperatorConsole) ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnCallSheetTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPCallSheetPanel) ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnVCamTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPVCamPanel) ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnActorDBTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPActorDatabasePanel) ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnPropDBTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPPropDatabasePanel) ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnStageDBTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPStageDatabasePanel) ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCAPToolModule, PCAPTool)

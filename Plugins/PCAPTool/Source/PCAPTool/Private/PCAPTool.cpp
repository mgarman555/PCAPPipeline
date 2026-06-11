#include "PCAPToolModule.h"
#include "SPCAPToolPanel.h"
#include "SPCAPDatabasePanel.h"
#include "SPCAPActorDatabasePanel.h"
#include "SPCAPPropDatabasePanel.h"
#include "SPCAPStageDatabasePanel.h"
#include "SPCAPOperatorConsole.h"
#include "SPCAPCallSheetPanel.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "PCAPTool"

const FName FPCAPToolModule::HMCTabName      = TEXT("PCAPTool_HMCMonitor");
const FName FPCAPToolModule::DatabaseTabName = TEXT("PCAPTool_Database");
const FName FPCAPToolModule::ActorDBTabName  = TEXT("PCAPTool_ActorDB");
const FName FPCAPToolModule::PropDBTabName   = TEXT("PCAPTool_PropDB");
const FName FPCAPToolModule::StageDBTabName  = TEXT("PCAPTool_StageDB");
const FName FPCAPToolModule::ConsoleTabName  = TEXT("PCAPTool_Console");
const FName FPCAPToolModule::CallSheetTabName = TEXT("PCAPTool_CallSheet");

void FPCAPToolModule::StartupModule()
{
    // Group every PCAP tab under a "PCAP Tools" submenu within Window > Tools.
    PCAPMenuGroup = WorkspaceMenu::GetMenuStructure().GetToolsCategory()->AddGroup(
        LOCTEXT("PCAPToolsGroup", "PCAP Tools"),
        LOCTEXT("PCAPToolsGroupTooltip", "Performance capture tools"),
        FSlateIcon());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        HMCTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnHMCTab))
        .SetDisplayName(LOCTEXT("HMCTabTitle", "HMC Monitor"))
        .SetTooltipText(LOCTEXT("HMCTabTooltip", "PCAP Tool — HMC device monitor"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        DatabaseTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnDatabaseTab))
        .SetDisplayName(LOCTEXT("DatabaseTabTitle", "Mocap Database"))
        .SetTooltipText(LOCTEXT("DatabaseTabTooltip", "PCAP Tool — browse the mocap database (read-only)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ActorDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnActorDBTab))
        .SetDisplayName(LOCTEXT("ActorDBTabTitle", "Actor Database"))
        .SetTooltipText(LOCTEXT("ActorDBTabTooltip", "PCAP Tool — the permanent talent library"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        PropDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnPropDBTab))
        .SetDisplayName(LOCTEXT("PropDBTabTitle", "Prop Database"))
        .SetTooltipText(LOCTEXT("PropDBTabTooltip", "PCAP Tool — the prop library (with mesh previews)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        StageDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnStageDBTab))
        .SetDisplayName(LOCTEXT("StageDBTabTitle", "Stage Database"))
        .SetTooltipText(LOCTEXT("StageDBTabTooltip", "PCAP Tool — stages (location + what to record)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ConsoleTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnConsoleTab))
        .SetDisplayName(LOCTEXT("ConsoleTabTitle", "Operator Console"))
        .SetTooltipText(LOCTEXT("ConsoleTabTooltip", "PCAP Tool — navigate shots + run takes (solo operator)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        CallSheetTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnCallSheetTab))
        .SetDisplayName(LOCTEXT("CallSheetTabTitle", "Call Sheet"))
        .SetTooltipText(LOCTEXT("CallSheetTabTooltip", "PCAP Tool — shoot-day prep (project, stage, day, actors, props)"))
        .SetGroup(PCAPMenuGroup.ToSharedRef());
}

void FPCAPToolModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HMCTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DatabaseTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ActorDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PropDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StageDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ConsoleTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CallSheetTabName);
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnHMCTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPToolPanel)
        ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnDatabaseTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPDatabasePanel)
        ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnActorDBTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPActorDatabasePanel)
        ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnPropDBTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPPropDatabasePanel)
        ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnStageDBTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPStageDatabasePanel)
        ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnConsoleTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPOperatorConsole)
        ];
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnCallSheetTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPCallSheetPanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCAPToolModule, PCAPTool)

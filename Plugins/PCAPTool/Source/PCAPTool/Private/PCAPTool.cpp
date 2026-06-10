#include "PCAPToolModule.h"
#include "SPCAPToolPanel.h"
#include "SPCAPDatabasePanel.h"
#include "SPCAPActorDatabasePanel.h"
#include "SPCAPPropDatabasePanel.h"
#include "SPCAPStageDatabasePanel.h"
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

void FPCAPToolModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        HMCTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnHMCTab))
        .SetDisplayName(LOCTEXT("HMCTabTitle", "HMC Monitor"))
        .SetTooltipText(LOCTEXT("HMCTabTooltip", "PCAP Tool — HMC device monitor"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        DatabaseTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnDatabaseTab))
        .SetDisplayName(LOCTEXT("DatabaseTabTitle", "Mocap Database"))
        .SetTooltipText(LOCTEXT("DatabaseTabTooltip", "PCAP Tool — browse the mocap database (read-only)"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ActorDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnActorDBTab))
        .SetDisplayName(LOCTEXT("ActorDBTabTitle", "Actor Database"))
        .SetTooltipText(LOCTEXT("ActorDBTabTooltip", "PCAP Tool — the permanent talent library"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        PropDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnPropDBTab))
        .SetDisplayName(LOCTEXT("PropDBTabTitle", "Prop Database"))
        .SetTooltipText(LOCTEXT("PropDBTabTooltip", "PCAP Tool — the prop library (with mesh previews)"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        StageDBTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnStageDBTab))
        .SetDisplayName(LOCTEXT("StageDBTabTitle", "Stage Database"))
        .SetTooltipText(LOCTEXT("StageDBTabTooltip", "PCAP Tool — stages (location + what to record)"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void FPCAPToolModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HMCTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DatabaseTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ActorDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PropDBTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StageDBTabName);
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCAPToolModule, PCAPTool)

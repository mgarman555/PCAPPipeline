#include "PCAPToolModule.h"
#include "SPCAPToolPanel.h"
#include "SPCAPDatabasePanel.h"
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
}

void FPCAPToolModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HMCTabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DatabaseTabName);
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCAPToolModule, PCAPTool)

#include "PCAPToolModule.h"
#include "SPCAPToolPanel.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "PCAPTool"

const FName FPCAPToolModule::HMCTabName = TEXT("PCAPTool_HMCMonitor");

void FPCAPToolModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        HMCTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnHMCTab))
        .SetDisplayName(LOCTEXT("HMCTabTitle", "HMC Monitor"))
        .SetTooltipText(LOCTEXT("HMCTabTooltip", "PCAP Tool — HMC device monitor"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void FPCAPToolModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HMCTabName);
}

TSharedRef<SDockTab> FPCAPToolModule::SpawnHMCTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPCAPToolPanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCAPToolModule, PCAPTool)

#include "PCAPToolModule.h"
#include "SPCAPToolPanel.h"
#include "SPCAPDatabasePanel.h"
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

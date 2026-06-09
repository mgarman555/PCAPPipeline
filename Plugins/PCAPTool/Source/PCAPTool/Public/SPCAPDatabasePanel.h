#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class UMocapDatabase;
class STableViewBase;
class ITableRow;

// One node in the database browse tree. Heterogeneous — Kind drives styling.
struct FPCAPDBNode
{
    enum class EKind : uint8
    {
        Section,      // "Roster", "Actors", "Productions" … (header rows)
        Production, Day, Session, Shot, Take,
        Actor, Prop, StageConfig,
        Info          // placeholder / empty-state message
    };

    EKind Kind = EKind::Info;
    FString Label;    // primary text (left)
    FString Detail;   // secondary text (right-aligned: counts, dates, ids)

    TArray<TSharedPtr<FPCAPDBNode>> Children;
};

/**
 * Plain, read-focused browser for the project's UMocapDatabase — "where do things exist".
 * Productions → Days → Sessions → Shots → Takes, plus the Roster (Actors / Props /
 * Stage Configs). No capture controls — that lives in the Realtime Operator.
 * Registered as the "Mocap Database" nomad tab.
 */
class PCAPTOOL_API SPCAPDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<STreeView<TSharedPtr<FPCAPDBNode>>> TreeView;
    TArray<TSharedPtr<FPCAPDBNode>> RootNodes;

    // (Re)reads the database asset and rebuilds RootNodes, then refreshes the tree.
    void RebuildTree();

    TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FPCAPDBNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
    void OnGetChildren(TSharedPtr<FPCAPDBNode> Node, TArray<TSharedPtr<FPCAPDBNode>>& OutChildren);
    void ExpandAll(const TArray<TSharedPtr<FPCAPDBNode>>& Nodes, bool bExpand);

    FReply OnRefreshClicked();
    FText  HeaderStatusText() const;

    // Palette (matches the existing PCAP Slate panels).
    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);  // #4AE080
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);  // #7A8A80 secondary
    const FLinearColor ColText3 = FLinearColor(0.290f, 0.345f, 0.314f);  // #4A5850 tertiary
};

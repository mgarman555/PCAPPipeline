#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

class UPCAPToolSubsystem;

class PCAPTOOL_API SPCAPToolPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPToolPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SWidgetSwitcher> ModeSwitcher;

    FReply OnSetupClicked();
    FReply OnPreviewClicked();

    int32 ActiveMode = 0; // 0 = Setup, 1 = Preview

    const FLinearColor BgPanel  = FLinearColor(0.102f, 0.102f, 0.102f);
    const FLinearColor BgHeader = FLinearColor(0.067f, 0.067f, 0.067f);
    const FLinearColor ColGreen = FLinearColor(0.137f, 0.467f, 0.220f);
    const FLinearColor ColGray  = FLinearColor(0.200f, 0.200f, 0.200f);
};

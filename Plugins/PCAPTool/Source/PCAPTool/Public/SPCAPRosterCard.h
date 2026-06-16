#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"   // FOnCheckStateChanged

// The one shared library card. Every database grid and every Call Sheet callout
// composes this — thumbnail (optional) + title + subtitle, clickable, with an
// optional "called" toggle. Caller supplies the thumbnail widget (or none → a dot).
class PCAPTOOL_API SPCAPRosterCard : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPRosterCard)
        : _bShowCalled(false)
        , _bCalled(false)
        , _Accent(FLinearColor(0.290f, 0.878f, 0.502f))
        {}
        SLATE_ARGUMENT(FText, Title)
        SLATE_ARGUMENT(FText, Subtitle)
        SLATE_ARGUMENT(TSharedPtr<SWidget>, Thumbnail)   // optional; null → accent dot
        SLATE_ARGUMENT(bool, bShowCalled)
        SLATE_ARGUMENT(bool, bCalled)
        SLATE_ARGUMENT(FLinearColor, Accent)
        SLATE_EVENT(FSimpleDelegate, OnClicked)
        SLATE_EVENT(FOnCheckStateChanged, OnCalledChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FSimpleDelegate OnClicked;
};

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// A dropdown over a list of strings (the Capture Systems lists, productions, helmets).
// Current/Options are read when the button paints / opens, so the list is always live.
// An empty Current shows Placeholder. EmptyEntryLabel, when set, adds a first entry that
// picks "" (e.g. "(none)" / "(any project)").
namespace PCAPStringPick
{
    // "OneHMC (stereo)", "OneHMC (mono/stereo)", or just the name when no mode is set.
    inline FString FacialModeLabel(const FString& Facial, bool bMono, bool bStereo)
    {
        const TCHAR* Mode = (bMono && bStereo) ? TEXT("mono/stereo") : bMono ? TEXT("mono") : bStereo ? TEXT("stereo") : TEXT("");
        return *Mode ? FString::Printf(TEXT("%s (%s)"), *Facial, Mode) : Facial;
    }

    inline TSharedRef<SWidget> Make(
        TFunction<FString()> Current,
        TFunction<TArray<FString>()> Options,
        TFunction<void(const FString&)> OnPick,
        const FText& Placeholder = NSLOCTEXT("PCAPStringPick", "Pick", "(pick)"),
        const FText& EmptyEntryLabel = FText::GetEmpty())
    {
        return SNew(SComboButton)
            .ButtonContent()
            [
                SNew(STextBlock).Text_Lambda([Current, Placeholder]()
                {
                    const FString C = Current ? Current() : FString();
                    return C.IsEmpty() ? Placeholder : FText::FromString(C);
                })
            ]
            .OnGetMenuContent_Lambda([Options, OnPick, EmptyEntryLabel]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(/*bCloseAfterSelection*/ true, nullptr);
                if (!EmptyEntryLabel.IsEmpty())
                    MB.AddMenuEntry(EmptyEntryLabel, FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([OnPick]() { OnPick(FString()); })));
                const TArray<FString> Opts = Options ? Options() : TArray<FString>();
                for (const FString& O : Opts)
                {
                    if (O.IsEmpty()) continue;
                    MB.AddMenuEntry(FText::FromString(O), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([OnPick, O]() { OnPick(O); })));
                }
                if (Opts.Num() == 0 && EmptyEntryLabel.IsEmpty())
                    MB.AddMenuEntry(NSLOCTEXT("PCAPStringPick", "NoOptions", "(none set: Project Settings > PCAP Tool)"),
                        FText::GetEmpty(), FSlateIcon(), FUIAction());
                return MB.MakeWidget();
            });
    }
}

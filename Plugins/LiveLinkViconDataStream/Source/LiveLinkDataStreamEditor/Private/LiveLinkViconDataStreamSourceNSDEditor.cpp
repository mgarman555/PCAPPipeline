// Copyright (c) 2021 Vicon Motion Systems Ltd. All Rights Reserved.

#include "LiveLinkViconDataStreamSourceNSDEditor.h"

#include "LiveLinkViconDataStreamNSDBrowserPanel.h"

#include "LiveLinkViconDataStreamSourcePropEditor.h"
#include "Widgets/Input/SButton.h"

#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "LiveLinkViconDataStreamSourceNSDEditor"

SLiveLinkViconDataStreamSourceNSDEditor::~SLiveLinkViconDataStreamSourceNSDEditor()
{
}

void SLiveLinkViconDataStreamSourceNSDEditor::Construct(const FArguments& Args, asio::io_context& IoContext)
{
  OnPropertiesSelected = Args._OnPropertiesSelected;
  ChildSlot
    [SNew(SBox)
       .WidthOverride(300)
       .Padding(2.0)
         [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(2.0)[SAssignNew(Browser, SLiveLinkViconDataStreamNSDBrowserPanel, IoContext)] + SVerticalBox::Slot().AutoHeight().Padding(2.0)[SAssignNew(ConnectionProperties, SLiveLinkViconDataStreamSourcePropEditor)] + SVerticalBox::Slot().AutoHeight().Padding(2.0f)[SAssignNew(CreateButton, SButton).Text(LOCTEXT("Create", "Create")).IsEnabled(false).OnClicked(this, &SLiveLinkViconDataStreamSourceNSDEditor::CreateSource)]]];

  Browser->OnListSelectionChanged.BindSP(this, &SLiveLinkViconDataStreamSourceNSDEditor::OnBrowserSelectionChanged);
}

FReply SLiveLinkViconDataStreamSourceNSDEditor::CreateSource() const
{
  ViconStreamProperties Properties;
  Properties.m_NSDProperties = Browser->GetSelectedService();
  Properties.m_SubjectFilter = ConnectionProperties->GetSubjectFilter();
  Properties.m_bRetimed = ConnectionProperties->GetIsRetimed();
  Properties.m_bLogOutput = ConnectionProperties->GetLogOutput();
  Properties.m_bUsePrefetch = ConnectionProperties->GetUsePrefetch();
  Properties.m_RetimeOffset = ConnectionProperties->GetOffset();

  Properties.m_bScaled = true;
  OnPropertiesSelected.ExecuteIfBound(Properties);
  return FReply::Handled();
}

void SLiveLinkViconDataStreamSourceNSDEditor::OnBrowserSelectionChanged(TSharedPtr<FListedService> Service, ESelectInfo::Type Type)
{
  CreateButton->SetEnabled(Service.IsValid());
}

#undef LOCTEXT_NAMESPACE

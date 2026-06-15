// Copyright (c) 2024 Vicon Motion Systems Ltd. All Rights Reserved.
#pragma once

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SCompoundWidget.h"


class SLiveLinkViconDataStreamSourcePropEditor : public SCompoundWidget
{
  SLATE_BEGIN_ARGS(SLiveLinkViconDataStreamSourcePropEditor) {}
  SLATE_END_ARGS()

  ~SLiveLinkViconDataStreamSourcePropEditor();

  void Construct(const FArguments& Args);

  FText GetSubjectFilter() const;
  bool GetIsRetimed() const;
  float GetOffset() const;
  bool GetLogOutput() const;
  bool GetUsePrefetch() const;

private:
  void OnOffsetEntryBoxChanged(float NewValue);
  TOptional<float> OnGetOffsetEntryBoxValue() const;

  TSharedPtr<SEditableTextBox> SubjectFilter;
  TSharedPtr<SCheckBox> IsStreamYUp;
  TSharedPtr<SCheckBox> IsRetimed;
  TSharedPtr<SCheckBox> LogOutput;
  TSharedPtr<SCheckBox> UsePreFetch;
  TOptional<float> Offset;
};

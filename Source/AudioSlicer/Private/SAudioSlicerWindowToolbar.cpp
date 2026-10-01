// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWindow.h"
#include "AudioSlicerSettings.h"
#include "SAudioSlicerWaveform.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Sound/SoundWave.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

namespace AudioSlicerWindow
{
	static FString FormatTime(float Seconds)
	{
		const double Clamped = FMath::Max(double(Seconds), 0.0);
		const int32 Minutes = FMath::FloorToInt(Clamped / 60.0);
		return FString::Printf(TEXT("%d:%05.2f"), Minutes, Clamped - Minutes * 60.0);
	}
}

TSharedRef<SWidget> SAudioSlicerWindow::BuildToolbar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SoundLabel", "Sound"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(320.f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(USoundWave::StaticClass())
				.ObjectPath_Lambda([this]()
				{
					return Session->Sound ? Session->Sound->GetPathName() : FString();
				})
				.OnObjectChanged(this, &SAudioSlicerWindow::OnSoundPicked)
				.AllowClear(false)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.DisplayThumbnail(false)
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.f, 2.f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsFocusable(false)
			.ToolTipText(LOCTEXT("PlayTooltip", "Play or pause (Space)"))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked_Lambda([this]()
			{
				TogglePlayback();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image_Lambda([this]()
				{
					return FAppStyle::GetBrush(bIsPlaying ? "Animation.Pause" : "Animation.Forward");
				})
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsFocusable(false)
			.ToolTipText(LOCTEXT("StopTooltip", "Stop and jump back to where playback started"))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked_Lambda([this]()
			{
				StopPlayback();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Animation.Stop"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.f, 0.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
			.Text_Lambda([this]()
			{
				using namespace AudioSlicerWindow;
				return FText::FromString(FormatTime(PlayheadTime) + TEXT(" / ") + FormatTime(GetDuration()));
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.f, 2.f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("DetectSlices", "Detect Slices"))
			.ToolTipText(LOCTEXT("DetectSlicesTooltip", "Split the sound at its silent parts. Threshold and timings are under Options."))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked(this, &SAudioSlicerWindow::OnDetectSlices)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ZoomToFit", "Fit"))
			.ToolTipText(LOCTEXT("ZoomToFitTooltip", "Show the whole sound (F)"))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked_Lambda([this]()
			{
				Waveform->ZoomToFit();
				return FReply::Handled();
			})
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNullWidget::NullWidget
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("OptionsTooltip", "Naming, fades and silence detection"))
			.OnGetMenuContent(this, &SAudioSlicerWindow::MakeOptionsMenu)
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Settings"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Options", "Options"))
				]
			]
		];
}

TSharedRef<SWidget> SAudioSlicerWindow::MakeOptionsMenu()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	TSharedRef<IDetailsView> DetailsView = PropertyEditor.CreateDetailView(Args);
	DetailsView->SetObject(GetMutableDefault<UAudioSlicerSettings>());
	DetailsView->OnFinishedChangingProperties().AddLambda([](const FPropertyChangedEvent&)
	{
		GetMutableDefault<UAudioSlicerSettings>()->SaveConfig();
	});

	return SNew(SBox)
		.WidthOverride(440.f)
		.MaxDesiredHeight(560.f)
		[
			DetailsView
		];
}

#undef LOCTEXT_NAMESPACE

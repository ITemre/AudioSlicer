// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWindow.h"
#include "AudioSlicerWindowUtils.h"
#include "AudioSlicerCore.h"
#include "AudioSlicerSettings.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Sound/SoundWave.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

TSharedRef<SWidget> SAudioSlicerWindow::BuildExportBar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BaseNameLabel", "Base Name"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(200.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(BaseName); })
				.HintText(LOCTEXT("BaseNameHint", "e.g. VO_Hero_Greeting"))
				.ToolTipText(LOCTEXT("BaseNameTooltip", "Slices without a name of their own are named after this, see the naming pattern under Options."))
				.OnTextChanged_Lambda([this](const FText& NewText) { BaseName = NewText.ToString().TrimStartAndEnd(); })
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(12.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FolderLabel", "Folder"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(280.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(OutputFolder); })
				.HintText(LOCTEXT("FolderHint", "/Game/Audio"))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					OutputFolder = NewText.ToString().TrimStartAndEnd();
					OutputFolder.RemoveFromEnd(TEXT("/"));
				})
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 0.f)
		[
			SAssignNew(FolderPickerButton, SComboButton)
			.ToolTipText(LOCTEXT("PickFolderTooltip", "Pick a folder"))
			.HasDownArrow(false)
			.OnGetMenuContent(this, &SAudioSlicerWindow::MakeFolderPicker)
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.FolderClosed"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(12.f, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &SAudioSlicerWindow::GetExportPreview)
			.ColorAndOpacity_Lambda([this]()
			{
				return CanExport() || Session->Slices.Num() == 0 ? FSlateColor::UseSubduedForeground() : FSlateColor(FLinearColor(1.f, 0.6f, 0.3f));
			})
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.IsEnabled_Lambda([this]() { return CanExport(); })
			.OnClicked(this, &SAudioSlicerWindow::OnExport)
			.Text_Lambda([this]()
			{
				return FText::Format(LOCTEXT("ExportButton", "Export {0} {0}|plural(one=Slice,other=Slices)"), Session->Slices.Num());
			})
		];
}

TSharedRef<SWidget> SAudioSlicerWindow::MakeFolderPicker()
{
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FPathPickerConfig Config;
	Config.DefaultPath = OutputFolder;
	Config.OnPathSelected = FOnPathSelected::CreateSP(this, &SAudioSlicerWindow::OnFolderPicked);

	return SNew(SBox)
		.WidthOverride(300.f)
		.HeightOverride(400.f)
		[
			ContentBrowser.Get().CreatePathPicker(Config)
		];
}

void SAudioSlicerWindow::OnFolderPicked(const FString& Path)
{
	OutputFolder = Path;
	if (FolderPickerButton.IsValid())
	{
		FolderPickerButton->SetIsOpen(false);
	}
}

FReply SAudioSlicerWindow::OnExport()
{
	FText Problem;
	if (!CanExport(&Problem))
	{
		AudioSlicerWindow::Notify(Problem, SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	// Resolve the names here so the checks below see exactly what will be written
	TArray<FAudioSliceRange> Slices = Session->Slices;
	TSet<FName> UsedNames;
	int32 NumExisting = 0;

	for (int32 Index = 0; Index < Slices.Num(); ++Index)
	{
		Slices[Index].Name = AudioSlicer::GetSliceAssetName(Slices[Index], BaseName, Index + 1);

		if (UsedNames.Contains(FName(*Slices[Index].Name)))
		{
			AudioSlicerWindow::Notify(
				FText::Format(LOCTEXT("DuplicateName", "Two slices would both be called {0}. Rename one of them."), FText::FromString(Slices[Index].Name)),
				SNotificationItem::CS_Fail);
			SelectSlice(Index);
			return FReply::Handled();
		}
		UsedNames.Add(FName(*Slices[Index].Name));

		if (AudioSlicer::FindExistingAsset(OutputFolder, Slices[Index].Name).IsValid())
		{
			++NumExisting;
		}
	}

	bool bOverwrite = false;
	if (NumExisting > 0)
	{
		const FText Question = FText::Format(
			LOCTEXT("OverwritePrompt", "{0} of these assets already exist in {1}.\n\nYes: overwrite them. Anything that uses them keeps working.\nNo: keep them and give the new slices unique names."),
			NumExisting, FText::FromString(OutputFolder));

		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNoCancel, Question);
		if (Answer == EAppReturnType::Cancel)
		{
			return FReply::Handled();
		}
		bOverwrite = Answer == EAppReturnType::Yes;
	}

	PausePlayback();

	FAudioSliceExportOptions Options = GetDefault<UAudioSlicerSettings>()->MakeExportOptions(OutputFolder);
	Options.bOverwriteExisting = bOverwrite;

	const TArray<USoundWave*> Exported = AudioSlicer::ExportSlices(*PCM, Session->Sound, BaseName, Slices, Options);

	if (Exported.Num() == 0)
	{
		AudioSlicerWindow::Notify(LOCTEXT("ExportFailed", "Nothing was exported. The Output Log has the details."), SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	TArray<TWeakObjectPtr<UObject>> ExportedAssets;
	for (USoundWave* Wave : Exported)
	{
		ExportedAssets.Add(Wave);
	}

	FNotificationInfo Info(Exported.Num() == Slices.Num()
		? FText::Format(LOCTEXT("ExportDone", "Exported {0} {0}|plural(one=slice,other=slices) to {1}"), Exported.Num(), FText::FromString(OutputFolder))
		: FText::Format(LOCTEXT("ExportPartial", "Exported {0} of {1} slices. The Output Log says what went wrong with the rest."), Exported.Num(), Slices.Num()));
	Info.ExpireDuration = 8.f;
	Info.HyperlinkText = LOCTEXT("ShowInContentBrowser", "Show in Content Browser");
	Info.Hyperlink = FSimpleDelegate::CreateLambda([ExportedAssets]()
	{
		TArray<UObject*> Assets;
		for (const TWeakObjectPtr<UObject>& Asset : ExportedAssets)
		{
			if (Asset.IsValid())
			{
				Assets.Add(Asset.Get());
			}
		}

		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().SyncBrowserToAssets(Assets);
	});

	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(Exported.Num() == Slices.Num() ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	return FReply::Handled();
}

bool SAudioSlicerWindow::CanExport(FText* OutReason) const
{
	auto Fail = [OutReason](const FText& Reason)
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};

	if (!Session->Sound || !PCM.IsValid())
	{
		return Fail(LOCTEXT("NoSound", "Pick a sound first."));
	}
	if (Session->Slices.Num() == 0)
	{
		return Fail(LOCTEXT("NoSlices", "Add a slice first."));
	}
	if (BaseName.IsEmpty() && Session->Slices.ContainsByPredicate([](const FAudioSliceRange& Slice) { return Slice.Name.TrimStartAndEnd().IsEmpty(); }))
	{
		return Fail(LOCTEXT("NoBaseName", "Enter a base name, or give every slice a name."));
	}
	if (!AudioSlicer::IsValidContentFolder(OutputFolder))
	{
		return Fail(LOCTEXT("BadFolder", "The folder has to be a content folder, like /Game/Audio."));
	}
	return true;
}

FText SAudioSlicerWindow::GetExportPreview() const
{
	FText Problem;
	if (!CanExport(&Problem))
	{
		return Session->Slices.Num() > 0 ? Problem : FText::GetEmpty();
	}

	const int32 NumSlices = Session->Slices.Num();
	const FText First = FText::FromString(AudioSlicer::GetSliceAssetName(Session->Slices[0], BaseName, 1));
	const FText Folder = FText::FromString(OutputFolder);

	if (NumSlices == 1)
	{
		return FText::Format(LOCTEXT("PreviewSingle", "{0} in {1}"), First, Folder);
	}

	const FText Last = FText::FromString(AudioSlicer::GetSliceAssetName(Session->Slices.Last(), BaseName, NumSlices));
	return FText::Format(LOCTEXT("PreviewRange", "{0} to {1} in {2}"), First, Last, Folder);
}

#undef LOCTEXT_NAMESPACE

// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWindow.h"
#include "AudioSlicerWindowUtils.h"
#include "AudioSlicerCore.h"
#include "AudioSlicerSettings.h"
#include "SAudioSlicerWaveform.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "Sound/SoundWave.h"
#include "Subsystems/ImportSubsystem.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

namespace AudioSlicerWindow
{
	static FString GetDefaultFolder(const USoundWave* Sound)
	{
		if (!Sound)
		{
			return FString();
		}

		FString Folder = FPackageName::GetLongPackagePath(Sound->GetPackage()->GetName());

		FString Subfolder = GetDefault<UAudioSlicerSettings>()->DefaultSubfolder.TrimStartAndEnd();
		Subfolder.RemoveFromStart(TEXT("/"));
		Subfolder.RemoveFromEnd(TEXT("/"));
		if (!Subfolder.IsEmpty())
		{
			Folder /= Subfolder;
		}
		return Folder;
	}
}

void SAudioSlicerWindow::Construct(const FArguments& InArgs)
{
	Session.Reset(NewObject<UAudioSlicerSession>(GetTransientPackage(), NAME_None, RF_Transactional));

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 6.f, 6.f, 4.f)
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(6.f, 0.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)

			+ SSplitter::Slot()
			.Value(0.62f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(Waveform, SAudioSlicerWaveform)
					.Session(Session.Get())
					.PlayheadTime_Lambda([this]() { return PlayheadTime; })
					.SelectedSlice_Lambda([this]() { return SelectedSlice; })
					.OnSeek(this, &SAudioSlicerWindow::OnSeek)
					.OnSliceSelected_Lambda([this](int32 SliceIndex) { SelectSlice(SliceIndex); })
					.OnSliceDoubleClicked_Lambda([this](int32 SliceIndex) { PlaySlice(SliceIndex); })
					.OnSlicesChanged_Lambda([this]() { OnSessionChanged(); })
					.OnViewChanged_Lambda([this]() { UpdateScrollBar(); })
					.OnContextMenu(this, &SAudioSlicerWindow::MakeContextMenu)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ScrollBar, SScrollBar)
					.Orientation(Orient_Horizontal)
					.AlwaysShowScrollbar(true)
					.OnUserScrolled(this, &SAudioSlicerWindow::OnUserScrolled)
				]
			]

			+ SSplitter::Slot()
			.Value(0.38f)
			[
				BuildSliceList()
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 4.f, 6.f, 6.f)
		[
			BuildExportBar()
		]
	];

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ReimportHandle = ImportSubsystem->OnAssetReimport.AddSP(this, &SAudioSlicerWindow::OnAssetReimported);
		}
	}

	OnSessionChanged();
}

SAudioSlicerWindow::~SAudioSlicerWindow()
{
	PausePlayback();

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetReimport.Remove(ReimportHandle);
		}
	}
}

void SAudioSlicerWindow::SetSound(USoundWave* Sound)
{
	if (Sound == Session->Sound)
	{
		return;
	}

	{
		// Part of the undo history, so picking the wrong sound by accident doesn't cost the slices
		FScopedTransaction Transaction(LOCTEXT("ChangeSound", "Change Sound"));
		Session->Modify();
		Session->Sound = Sound;
		Session->Slices.Reset();
	}

	SelectedSlice = INDEX_NONE;
	OnSessionChanged();
}

FReply SAudioSlicerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bNoModifiers = !InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown();

	if (InKeyEvent.IsControlDown() && GEditor)
	{
		if (Key == EKeys::Z && !InKeyEvent.IsShiftDown())
		{
			GEditor->UndoTransaction();
			return FReply::Handled();
		}
		if (Key == EKeys::Y || (Key == EKeys::Z && InKeyEvent.IsShiftDown()))
		{
			GEditor->RedoTransaction();
			return FReply::Handled();
		}
	}

	if (!bNoModifiers)
	{
		return FReply::Unhandled();
	}

	if (Key == EKeys::SpaceBar)
	{
		TogglePlayback();
		return FReply::Handled();
	}
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		DeleteSlice(SelectedSlice);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		Waveform->CancelDrag();
		return FReply::Handled();
	}
	if (Key == EKeys::F)
	{
		if (Session->Slices.IsValidIndex(SelectedSlice))
		{
			ZoomToSlice(SelectedSlice);
		}
		else
		{
			Waveform->ZoomToFit();
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAudioSlicerWindow::PostUndo(bool bSuccess)
{
	OnSessionChanged();
}

void SAudioSlicerWindow::PostRedo(bool bSuccess)
{
	OnSessionChanged();
}

void SAudioSlicerWindow::OnSessionChanged()
{
	if (Session->Sound != LoadedSound.Get())
	{
		ReloadAudio(true);
	}

	// Items are only indices. Reusing them keeps the rows alive, so a text box that is
	// being edited doesn't vanish because some other slice changed.
	const int32 NumSlices = Session->Slices.Num();
	while (SliceItems.Num() < NumSlices)
	{
		SliceItems.Add(MakeShared<int32>(SliceItems.Num()));
	}
	SliceItems.SetNum(NumSlices);

	if (!Session->Slices.IsValidIndex(SelectedSlice))
	{
		SelectedSlice = INDEX_NONE;
	}

	if (SliceList.IsValid())
	{
		SliceList->RequestListRefresh();
		if (SelectedSlice != INDEX_NONE)
		{
			SliceList->SetSelection(SliceItems[SelectedSlice], ESelectInfo::Direct);
		}
		else
		{
			SliceList->ClearSelection();
		}
	}

	if (Waveform.IsValid())
	{
		Waveform->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SAudioSlicerWindow::ReloadAudio(bool bResetNaming)
{
	PausePlayback();

	USoundWave* Sound = Session->Sound;
	LoadedSound = Sound;
	PCM.Reset();

	if (Sound)
	{
		TSharedPtr<FAudioSlicerPCM> NewPCM = MakeShared<FAudioSlicerPCM>();
		if (NewPCM->Load(Sound))
		{
			PCM = NewPCM;
		}
		else
		{
			AudioSlicerWindow::Notify(
				FText::Format(LOCTEXT("LoadFailed", "Couldn't read the audio of {0}. Only imported Sound Waves can be sliced."), FText::FromString(Sound->GetName())),
				SNotificationItem::CS_Fail);
		}
	}

	if (Waveform.IsValid())
	{
		Waveform->SetAudio(PCM);
	}

	PlayheadTime = 0.f;
	PlayFrom = 0.f;

	if (bResetNaming)
	{
		BaseName = Sound ? Sound->GetName() : FString();
		OutputFolder = AudioSlicerWindow::GetDefaultFolder(Sound);
	}

	UpdateScrollBar();
}

void SAudioSlicerWindow::OnAssetReimported(UObject* Object)
{
	// The slices stay, only the audio under them changes
	if (Object && Object == LoadedSound.Get())
	{
		ReloadAudio(false);
	}
}

void SAudioSlicerWindow::OnSoundPicked(const FAssetData& AssetData)
{
	SetSound(Cast<USoundWave>(AssetData.GetAsset()));
}

FReply SAudioSlicerWindow::OnDetectSlices()
{
	if (!PCM.IsValid())
	{
		return FReply::Handled();
	}

	const FAudioSilenceDetectionSettings& Detection = GetDefault<UAudioSlicerSettings>()->Detection;
	TArray<FAudioSliceRange> Found = PCM->DetectSoundRegions(Detection);

	if (Found.Num() == 0)
	{
		AudioSlicerWindow::Notify(
			FText::Format(LOCTEXT("NothingDetected", "Nothing in this sound is louder than {0} dB. Try a lower threshold under Options."), FText::AsNumber(Detection.ThresholdDb)),
			SNotificationItem::CS_None);
		return FReply::Handled();
	}

	if (Session->Slices.Num() > 0)
	{
		const FText Question = FText::Format(
			LOCTEXT("ReplaceSlices", "Replace the {0} existing {0}|plural(one=slice,other=slices) with {1} detected ones?\n\nCtrl+Z brings them back."),
			Session->Slices.Num(), Found.Num());

		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
	}

	FScopedTransaction Transaction(LOCTEXT("DetectSlicesTransaction", "Detect Slices"));
	Session->Modify();
	Session->Slices = MoveTemp(Found);

	SelectedSlice = INDEX_NONE;
	OnSessionChanged();

	return FReply::Handled();
}

void SAudioSlicerWindow::UpdateScrollBar()
{
	if (!ScrollBar.IsValid() || !Waveform.IsValid())
	{
		return;
	}

	const float Total = Waveform->GetTotalDuration();
	if (Total <= 0.f)
	{
		ScrollBar->SetState(0.f, 1.f);
		return;
	}

	ScrollBar->SetState(Waveform->GetViewStart() / Total, Waveform->GetViewDuration() / Total);
}

void SAudioSlicerWindow::OnUserScrolled(float ScrollOffset)
{
	Waveform->SetViewStart(ScrollOffset * Waveform->GetTotalDuration());
}

#undef LOCTEXT_NAMESPACE

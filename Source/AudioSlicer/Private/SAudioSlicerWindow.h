// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "AudioSlicerSession.h"
#include "EditorUndoClient.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SAudioSlicerWaveform;
class SComboButton;
class SScrollBar;
class UAudioComponent;
class USoundWave;
struct FAssetData;
struct FAudioSlicerPCM;

/**
 * Coordinates the editing session and child widgets.
 * Implementation is grouped by responsibility: Window (lifecycle/session),
 * Toolbar (controls/options), Slices (list/editing), Playback and Export.
 * These files share this class so callbacks and undo keep one session owner.
 */
class SAudioSlicerWindow : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SAudioSlicerWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAudioSlicerWindow() override;

	void SetSound(USoundWave* Sound);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// Used by the rows of the slice list
	const TArray<FAudioSliceRange>& GetSlices() const;
	FString GetAutoName(int32 SliceIndex) const;
	float GetDuration() const;
	void RenameSlice(int32 SliceIndex, const FString& NewName);
	void SetSliceTime(int32 SliceIndex, bool bStart, float Time);
	void DeleteSlice(int32 SliceIndex);
	void PlaySlice(int32 SliceIndex);

private:
	using FSliceItem = TSharedPtr<int32>;

	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildSliceList();
	TSharedRef<SWidget> BuildExportBar();
	TSharedRef<SWidget> MakeOptionsMenu();
	TSharedRef<SWidget> MakeFolderPicker();
	TSharedPtr<SWidget> MakeContextMenu(int32 SliceIndex, float Time);
	TSharedRef<class ITableRow> MakeSliceRow(FSliceItem Item, const TSharedRef<class STableViewBase>& OwnerTable);

	/** Brings UI and loaded audio in line with the session, after edits and after undo. */
	void OnSessionChanged();
	void ReloadAudio(bool bResetNaming);
	void OnAssetReimported(UObject* Object);
	void OnSoundPicked(const FAssetData& AssetData);
	void OnFolderPicked(const FString& Path);

	void SelectSlice(int32 SliceIndex, bool bFromList = false);
	void OnListSelectionChanged(FSliceItem Item, ESelectInfo::Type SelectInfo);
	void SplitSliceAt(int32 SliceIndex, float Time);
	void ZoomToSlice(int32 SliceIndex);

	void OnSeek(float Time);
	void PlayFromTime(float Time);
	void TogglePlayback();
	void StartPlayback(float From, float Until);
	void PausePlayback();
	void StopPlayback();
	EActiveTimerReturnType TickPlayback(double CurrentTime, float DeltaTime);

	FReply OnDetectSlices();
	FReply OnExport();
	bool CanExport(FText* OutReason = nullptr) const;
	FText GetExportPreview() const;

	void UpdateScrollBar();
	void OnUserScrolled(float ScrollOffset);

	TStrongObjectPtr<UAudioSlicerSession> Session;
	TSharedPtr<FAudioSlicerPCM> PCM;
	TWeakObjectPtr<USoundWave> LoadedSound;

	FString BaseName;
	FString OutputFolder;
	int32 SelectedSlice = INDEX_NONE;

	float PlayheadTime = 0.f;
	float PlayFrom = 0.f;
	float PlayUntil = 0.f;
	double PlayStartedAt = 0.0;
	bool bIsPlaying = false;
	TWeakObjectPtr<UAudioComponent> PreviewComponent;
	TSharedPtr<FActiveTimerHandle> PlaybackTimer;

	TSharedPtr<SAudioSlicerWaveform> Waveform;
	TSharedPtr<SScrollBar> ScrollBar;
	TSharedPtr<SComboButton> FolderPickerButton;
	TSharedPtr<SListView<FSliceItem>> SliceList;
	TArray<FSliceItem> SliceItems;

	FDelegateHandle ReimportHandle;
};

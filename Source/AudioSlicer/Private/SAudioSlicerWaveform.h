// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "AudioSlicerLibrary.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SLeafWidget.h"

class FScopedTransaction;
class UAudioSlicerSession;
struct FAudioSlicerPCM;

DECLARE_DELEGATE_OneParam(FOnAudioSlicerTime, float /*Time*/);
DECLARE_DELEGATE_OneParam(FOnAudioSlicerSlice, int32 /*SliceIndex*/);
DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<SWidget>, FOnAudioSlicerContextMenu, int32 /*SliceIndex*/, float /*Time*/);

/** Waveform with a time ruler. Slices are drawn on top and can be created, resized and moved with the mouse. */
class SAudioSlicerWaveform : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAudioSlicerWaveform) {}
		SLATE_ARGUMENT(UAudioSlicerSession*, Session)
		SLATE_ATTRIBUTE(float, PlayheadTime)
		SLATE_ATTRIBUTE(int32, SelectedSlice)
		SLATE_EVENT(FOnAudioSlicerTime, OnSeek)
		SLATE_EVENT(FOnAudioSlicerSlice, OnSliceSelected)
		SLATE_EVENT(FOnAudioSlicerSlice, OnSliceDoubleClicked)
		SLATE_EVENT(FSimpleDelegate, OnSlicesChanged)
		SLATE_EVENT(FSimpleDelegate, OnViewChanged)
		SLATE_EVENT(FOnAudioSlicerContextMenu, OnContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAudioSlicerWaveform() override;

	void SetAudio(TSharedPtr<const FAudioSlicerPCM> InPCM);

	float GetTotalDuration() const;
	float GetViewStart() const { return ViewStart; }
	float GetViewDuration() const { return ViewDuration; }

	void SetViewStart(float Time);
	void ZoomToFit();
	void ZoomToRange(float Start, float End);

	/** Scrolls so Time is on screen. Does nothing if it already is. */
	void ScrollIntoView(float Time);

	/** Aborts a drag in progress and puts the slice back where it was. */
	void CancelDrag();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	enum class EDragMode : uint8
	{
		None,
		Pending,	// button is down, but the mouse hasn't moved far enough to tell what the user wants
		Create,
		ResizeStart,
		ResizeEnd,
		Move,
		Pan
	};

	struct FChannelPeaks
	{
		TArray<int16> Min;
		TArray<int16> Max;
	};

	void BuildPeaks();
	void GetMinMax(int32 Channel, int32 FirstFrame, int32 EndFrame, float& OutMin, float& OutMax) const;

	float TimeToX(float Time, float Width) const;
	float XToTime(float X, float Width) const;
	float LocalXToClampedTime(const FGeometry& Geometry, const FVector2D& ScreenPosition) const;

	/** Slice whose start or end is within grabbing distance of X. bOutIsStart says which edge. */
	int32 HitTestEdge(float X, float Width, bool& bOutIsStart) const;

	void ClampView();
	void NotifyViewChanged();
	void BeginEdit(const FText& Description);
	void FinishEdit();

	void PaintRuler(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2f& Size) const;
	void PaintChannels(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2f& Size) const;
	void PaintSlices(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2f& Size) const;

	TWeakObjectPtr<UAudioSlicerSession> Session;
	TSharedPtr<const FAudioSlicerPCM> PCM;
	TArray<FChannelPeaks> Peaks;

	TAttribute<float> PlayheadTime;
	TAttribute<int32> SelectedSlice;
	FOnAudioSlicerTime OnSeek;
	FOnAudioSlicerSlice OnSliceSelected;
	FOnAudioSlicerSlice OnSliceDoubleClicked;
	FSimpleDelegate OnSlicesChanged;
	FSimpleDelegate OnViewChanged;
	FOnAudioSlicerContextMenu OnContextMenu;

	float ViewStart = 0.f;
	float ViewDuration = 1.f;

	EDragMode DragMode = EDragMode::None;
	bool bPendingOnEdge = false;
	bool bPendingEdgeIsStart = false;
	float DragStartX = 0.f;
	float DragStartTime = 0.f;
	float DragViewStart = 0.f;
	float DragGrabOffset = 0.f;
	int32 DragSlice = INDEX_NONE;
	FAudioSliceRange DragSliceBackup;
	float CreateStart = 0.f;
	float CreateEnd = 0.f;

	TUniquePtr<FScopedTransaction> Transaction;
};

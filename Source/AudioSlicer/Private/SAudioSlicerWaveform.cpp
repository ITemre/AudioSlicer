// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWaveform.h"
#include "AudioSlicerCore.h"
#include "AudioSlicerSession.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/MenuStack.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

namespace AudioSlicerWaveform
{
	constexpr float RulerHeight = 22.f;
	constexpr float LabelHeight = 18.f;
	constexpr float EdgeGrabDistance = 6.f;
	constexpr float DragThreshold = 4.f;
	constexpr float MinSliceLength = 0.01f;
	constexpr float MinViewDuration = 0.01f;
	constexpr int32 FramesPerPeak = 256;

	const FLinearColor Background(0.014f, 0.014f, 0.017f);
	const FLinearColor RulerBackground(0.03f, 0.03f, 0.036f);
	const FLinearColor GridLine(1.f, 1.f, 1.f, 0.05f);
	const FLinearColor RulerText(0.55f, 0.55f, 0.6f);
	const FLinearColor Wave(0.36f, 0.72f, 0.95f);
	const FLinearColor CenterLine(1.f, 1.f, 1.f, 0.08f);
	const FLinearColor Playhead(1.f, 0.85f, 0.35f);

	// Picked to stay apart from each other and from the waveform blue
	const FLinearColor SliceColors[] =
	{
		FLinearColor(0.95f, 0.45f, 0.35f),
		FLinearColor(0.45f, 0.85f, 0.45f),
		FLinearColor(0.85f, 0.55f, 0.95f),
		FLinearColor(0.95f, 0.75f, 0.30f),
		FLinearColor(0.35f, 0.85f, 0.80f),
		FLinearColor(0.95f, 0.50f, 0.70f),
	};

	static const FLinearColor& GetSliceColor(int32 Index)
	{
		return SliceColors[Index % UE_ARRAY_COUNT(SliceColors)];
	}

	static FString FormatRulerTime(double Time, double Step)
	{
		const int32 Minutes = FMath::FloorToInt(Time / 60.0);
		const double Seconds = Time - Minutes * 60.0;

		if (Step < 0.01)
		{
			return FString::Printf(TEXT("%d:%06.3f"), Minutes, Seconds);
		}
		if (Step < 1.0)
		{
			return FString::Printf(TEXT("%d:%05.2f"), Minutes, Seconds);
		}
		return FString::Printf(TEXT("%d:%02d"), Minutes, FMath::RoundToInt(Seconds));
	}
}

void SAudioSlicerWaveform::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	PlayheadTime = InArgs._PlayheadTime;
	SelectedSlice = InArgs._SelectedSlice;
	OnSeek = InArgs._OnSeek;
	OnSliceSelected = InArgs._OnSliceSelected;
	OnSliceDoubleClicked = InArgs._OnSliceDoubleClicked;
	OnSlicesChanged = InArgs._OnSlicesChanged;
	OnViewChanged = InArgs._OnViewChanged;
	OnContextMenu = InArgs._OnContextMenu;
}

SAudioSlicerWaveform::~SAudioSlicerWaveform() = default;

void SAudioSlicerWaveform::SetAudio(TSharedPtr<const FAudioSlicerPCM> InPCM)
{
	CancelDrag();
	PCM = InPCM;
	BuildPeaks();
	ZoomToFit();
}

float SAudioSlicerWaveform::GetTotalDuration() const
{
	return PCM.IsValid() ? PCM->GetDuration() : 0.f;
}

void SAudioSlicerWaveform::SetViewStart(float Time)
{
	ViewStart = Time;
	ClampView();
	NotifyViewChanged();
}

void SAudioSlicerWaveform::ZoomToFit()
{
	ViewStart = 0.f;
	ViewDuration = FMath::Max(GetTotalDuration(), AudioSlicerWaveform::MinViewDuration);
	ClampView();
	NotifyViewChanged();
}

void SAudioSlicerWaveform::ZoomToRange(float Start, float End)
{
	// A bit of air on both sides so the edges can still be grabbed
	const float Margin = (End - Start) * 0.1f;
	ViewStart = Start - Margin;
	ViewDuration = (End - Start) + Margin * 2.f;
	ClampView();
	NotifyViewChanged();
}

void SAudioSlicerWaveform::ScrollIntoView(float Time)
{
	if (Time >= ViewStart && Time <= ViewStart + ViewDuration)
	{
		return;
	}

	// Page forward the way DAWs do instead of keeping the playhead centered
	ViewStart = Time - ViewDuration * 0.05f;
	ClampView();
	NotifyViewChanged();
}

void SAudioSlicerWaveform::CancelDrag()
{
	UAudioSlicerSession* SessionPtr = Session.Get();
	if (Transaction.IsValid() && SessionPtr && SessionPtr->Slices.IsValidIndex(DragSlice))
	{
		SessionPtr->Slices[DragSlice] = DragSliceBackup;
		Transaction->Cancel();
	}
	Transaction.Reset();

	DragMode = EDragMode::None;
	DragSlice = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAudioSlicerWaveform::BuildPeaks()
{
	Peaks.Reset();
	if (!PCM.IsValid() || !PCM->IsValid())
	{
		return;
	}

	using namespace AudioSlicerWaveform;

	const int32 NumChannels = PCM->NumChannels;
	const int32 NumFrames = PCM->GetNumFrames();
	const int32 NumPeaks = FMath::DivideAndRoundUp(NumFrames, FramesPerPeak);

	Peaks.SetNum(NumChannels);
	for (FChannelPeaks& ChannelPeaks : Peaks)
	{
		ChannelPeaks.Min.SetNumUninitialized(NumPeaks);
		ChannelPeaks.Max.SetNumUninitialized(NumPeaks);
	}

	for (int32 PeakIndex = 0; PeakIndex < NumPeaks; ++PeakIndex)
	{
		const int32 FirstFrame = PeakIndex * FramesPerPeak;
		const int32 EndFrame = FMath::Min(FirstFrame + FramesPerPeak, NumFrames);

		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			int16 Min = MAX_int16;
			int16 Max = MIN_int16;
			for (int32 Frame = FirstFrame; Frame < EndFrame; ++Frame)
			{
				const int16 Sample = PCM->Samples[Frame * NumChannels + Channel];
				Min = FMath::Min(Min, Sample);
				Max = FMath::Max(Max, Sample);
			}
			Peaks[Channel].Min[PeakIndex] = Min;
			Peaks[Channel].Max[PeakIndex] = Max;
		}
	}
}

void SAudioSlicerWaveform::GetMinMax(int32 Channel, int32 FirstFrame, int32 EndFrame, float& OutMin, float& OutMax) const
{
	using namespace AudioSlicerWaveform;

	const int32 NumFrames = PCM->GetNumFrames();
	FirstFrame = FMath::Clamp(FirstFrame, 0, NumFrames - 1);
	EndFrame = FMath::Clamp(EndFrame, FirstFrame + 1, NumFrames);

	int32 Min = MAX_int16;
	int32 Max = MIN_int16;

	if (EndFrame - FirstFrame >= FramesPerPeak * 2)
	{
		// Zoomed out: the precomputed peaks are exact enough and a lot cheaper
		const int32 LastPeak = FMath::Min((EndFrame - 1) / FramesPerPeak, Peaks[Channel].Min.Num() - 1);
		for (int32 PeakIndex = FirstFrame / FramesPerPeak; PeakIndex <= LastPeak; ++PeakIndex)
		{
			Min = FMath::Min<int32>(Min, Peaks[Channel].Min[PeakIndex]);
			Max = FMath::Max<int32>(Max, Peaks[Channel].Max[PeakIndex]);
		}
	}
	else
	{
		const int32 NumChannels = PCM->NumChannels;
		for (int32 Frame = FirstFrame; Frame < EndFrame; ++Frame)
		{
			const int32 Sample = PCM->Samples[Frame * NumChannels + Channel];
			Min = FMath::Min(Min, Sample);
			Max = FMath::Max(Max, Sample);
		}
	}

	OutMin = float(Min) / 32768.f;
	OutMax = float(Max) / 32768.f;
}

float SAudioSlicerWaveform::TimeToX(float Time, float Width) const
{
	return (Time - ViewStart) / ViewDuration * Width;
}

float SAudioSlicerWaveform::XToTime(float X, float Width) const
{
	return Width > 0.f ? ViewStart + X / Width * ViewDuration : ViewStart;
}

float SAudioSlicerWaveform::LocalXToClampedTime(const FGeometry& Geometry, const FVector2D& ScreenPosition) const
{
	const float LocalX = float(Geometry.AbsoluteToLocal(ScreenPosition).X);
	return FMath::Clamp(XToTime(LocalX, float(Geometry.GetLocalSize().X)), 0.f, GetTotalDuration());
}

int32 SAudioSlicerWaveform::HitTestEdge(float X, float Width, bool& bOutIsStart) const
{
	const UAudioSlicerSession* SessionPtr = Session.Get();
	if (!SessionPtr)
	{
		return INDEX_NONE;
	}

	int32 BestSlice = INDEX_NONE;
	float BestDistance = AudioSlicerWaveform::EdgeGrabDistance;

	for (int32 Index = 0; Index < SessionPtr->Slices.Num(); ++Index)
	{
		const FAudioSliceRange& Slice = SessionPtr->Slices[Index];

		const float StartDistance = FMath::Abs(TimeToX(Slice.StartTime, Width) - X);
		if (StartDistance <= BestDistance)
		{
			BestSlice = Index;
			BestDistance = StartDistance;
			bOutIsStart = true;
		}

		// "<" rather than "<=": where one slice ends and the next begins, the start of the later one wins,
		// which is almost always the edge people mean to drag.
		const float EndDistance = FMath::Abs(TimeToX(Slice.EndTime, Width) - X);
		if (EndDistance < BestDistance)
		{
			BestSlice = Index;
			BestDistance = EndDistance;
			bOutIsStart = false;
		}
	}

	return BestSlice;
}

void SAudioSlicerWaveform::ClampView()
{
	const float Total = GetTotalDuration();
	if (Total <= 0.f)
	{
		ViewStart = 0.f;
		ViewDuration = 1.f;
		return;
	}

	ViewDuration = FMath::Clamp(ViewDuration, FMath::Min(AudioSlicerWaveform::MinViewDuration, Total), Total);
	ViewStart = FMath::Clamp(ViewStart, 0.f, Total - ViewDuration);
}

void SAudioSlicerWaveform::NotifyViewChanged()
{
	OnViewChanged.ExecuteIfBound();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAudioSlicerWaveform::BeginEdit(const FText& Description)
{
	Transaction = MakeUnique<FScopedTransaction>(Description);
	if (UAudioSlicerSession* SessionPtr = Session.Get())
	{
		SessionPtr->Modify();
	}
}

void SAudioSlicerWaveform::FinishEdit()
{
	int32 NewIndex = DragSlice;
	if (UAudioSlicerSession* SessionPtr = Session.Get())
	{
		NewIndex = SessionPtr->SortSlices(DragSlice);
	}
	Transaction.Reset();

	OnSlicesChanged.ExecuteIfBound();
	OnSliceSelected.ExecuteIfBound(NewIndex);
}

FVector2D SAudioSlicerWaveform::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(600.f, 240.f);
}

int32 SAudioSlicerWaveform::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace AudioSlicerWaveform;

	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush(TEXT("WhiteBrush"));

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, Background);

	if (!PCM.IsValid() || !PCM->IsValid() || Size.X <= 1.f)
	{
		const FText Hint = LOCTEXT("EmptyHint", "Pick a Sound Wave above, or right-click one in the Content Browser and choose Open in Audio Slicer.");
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
		const FVector2f TextSize = FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Hint, Font));

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform((Size - TextSize) * 0.5f)),
			Hint, Font, ESlateDrawEffect::None, RulerText);
		return LayerId + 1;
	}

	PaintRuler(AllottedGeometry, OutDrawElements, LayerId + 1, Size);
	PaintSlices(AllottedGeometry, OutDrawElements, LayerId + 2, Size);
	PaintChannels(AllottedGeometry, OutDrawElements, LayerId + 4, Size);

	const int32 TopLayer = LayerId + 6;

	if (DragMode == EDragMode::Create)
	{
		const float X0 = TimeToX(FMath::Min(CreateStart, CreateEnd), Size.X);
		const float X1 = TimeToX(FMath::Max(CreateStart, CreateEnd), Size.X);
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(FMath::Max(X1 - X0, 1.f), Size.Y - RulerHeight), FSlateLayoutTransform(FVector2f(X0, RulerHeight))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.12f));
	}

	const float PlayX = TimeToX(PlayheadTime.Get(), Size.X);
	if (PlayX >= 0.f && PlayX <= Size.X)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, TopLayer, AllottedGeometry.ToPaintGeometry(),
			TArray<FVector2f>{ FVector2f(PlayX, 0.f), FVector2f(PlayX, Size.Y) },
			ESlateDrawEffect::None, Playhead, true, 1.5f);

		// Small handle in the ruler so the playhead is easy to spot when zoomed out
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(9.f, 7.f), FSlateLayoutTransform(FVector2f(PlayX - 4.5f, 0.f))),
			WhiteBrush, ESlateDrawEffect::None, Playhead);
	}

	return TopLayer;
}

void SAudioSlicerWaveform::PaintRuler(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2f& Size) const
{
	using namespace AudioSlicerWaveform;

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		Geometry.ToPaintGeometry(FVector2f(Size.X, RulerHeight), FSlateLayoutTransform()),
		FAppStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, RulerBackground);

	// Smallest step that leaves enough room between two labels
	static const double Steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 };
	const double PixelsPerSecond = double(Size.X) / double(ViewDuration);
	double Step = Steps[UE_ARRAY_COUNT(Steps) - 1];
	for (const double Candidate : Steps)
	{
		if (Candidate * PixelsPerSecond >= 90.0)
		{
			Step = Candidate;
			break;
		}
	}

	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const int64 FirstTick = int64(FMath::FloorToDouble(double(ViewStart) / Step));
	const int64 LastTick = int64(FMath::CeilToDouble(double(ViewStart + ViewDuration) / Step));

	for (int64 Tick = FirstTick; Tick <= LastTick; ++Tick)
	{
		const double Time = double(Tick) * Step;
		const float X = TimeToX(float(Time), Size.X);

		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(),
			TArray<FVector2f>{ FVector2f(X, RulerHeight - 6.f), FVector2f(X, Size.Y) },
			ESlateDrawEffect::None, GridLine, false, 1.f);

		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(FVector2f(80.f, 14.f), FSlateLayoutTransform(FVector2f(X + 3.f, 3.f))),
			FormatRulerTime(Time, Step), Font, ESlateDrawEffect::None, RulerText);

		// One unlabeled tick in between
		const float HalfX = TimeToX(float(Time + Step * 0.5), Size.X);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(),
			TArray<FVector2f>{ FVector2f(HalfX, RulerHeight - 3.f), FVector2f(HalfX, RulerHeight) },
			ESlateDrawEffect::None, GridLine, false, 1.f);
	}
}

void SAudioSlicerWaveform::PaintChannels(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2f& Size) const
{
	using namespace AudioSlicerWaveform;

	const int32 NumChannels = PCM->NumChannels;
	const float LaneHeight = (Size.Y - RulerHeight) / float(NumChannels);
	const float SampleRate = float(PCM->SampleRate);
	const int32 Columns = FMath::CeilToInt(Size.X);
	const float FramesPerPixel = ViewDuration * SampleRate / Size.X;

	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		const float Center = RulerHeight + LaneHeight * (float(Channel) + 0.5f);
		const float Scale = LaneHeight * 0.45f;

		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(),
			TArray<FVector2f>{ FVector2f(0.f, Center), FVector2f(Size.X, Center) },
			ESlateDrawEffect::None, CenterLine, false, 1.f);

		TArray<FVector2f> Points;

		if (FramesPerPixel < 2.f)
		{
			// Zoomed in far enough to see single samples, so connect them instead of drawing columns
			const int32 FirstFrame = FMath::Max(0, FMath::FloorToInt(ViewStart * SampleRate));
			const int32 EndFrame = FMath::Min(PCM->GetNumFrames(), FMath::CeilToInt((ViewStart + ViewDuration) * SampleRate) + 1);
			Points.Reserve(EndFrame - FirstFrame);

			for (int32 Frame = FirstFrame; Frame < EndFrame; ++Frame)
			{
				const float Sample = float(PCM->Samples[Frame * NumChannels + Channel]) / 32768.f;
				Points.Emplace(TimeToX(float(Frame) / SampleRate, Size.X), Center - Sample * Scale);
			}
		}
		else
		{
			// One vertical min/max stroke per pixel column, all in a single zigzag line
			Points.Reserve(Columns * 2);

			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const int32 FirstFrame = FMath::FloorToInt(XToTime(float(Column), Size.X) * SampleRate);
				const int32 EndFrame = FMath::FloorToInt(XToTime(float(Column + 1), Size.X) * SampleRate);
				if (FirstFrame >= PCM->GetNumFrames())
				{
					break;
				}

				float Min = 0.f;
				float Max = 0.f;
				GetMinMax(Channel, FirstFrame, EndFrame, Min, Max);

				const float Top = Center - Max * Scale;
				const float Bottom = FMath::Max(Center - Min * Scale, Top + 1.f);
				const float X = float(Column) + 0.5f;

				Points.Emplace(X, Top);
				Points.Emplace(X, Bottom);
			}
		}

		if (Points.Num() > 1)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Wave, true, 1.f);
		}
	}
}

void SAudioSlicerWaveform::PaintSlices(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2f& Size) const
{
	using namespace AudioSlicerWaveform;

	const UAudioSlicerSession* SessionPtr = Session.Get();
	if (!SessionPtr)
	{
		return;
	}

	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush(TEXT("WhiteBrush"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const int32 Selected = SelectedSlice.Get(INDEX_NONE);
	const float Top = RulerHeight;
	const float Height = Size.Y - RulerHeight;

	for (int32 Index = 0; Index < SessionPtr->Slices.Num(); ++Index)
	{
		const FAudioSliceRange& Slice = SessionPtr->Slices[Index];
		const float X0 = TimeToX(Slice.StartTime, Size.X);
		const float X1 = TimeToX(Slice.EndTime, Size.X);
		if (X1 < 0.f || X0 > Size.X)
		{
			continue;
		}

		const bool bSelected = Index == Selected;
		const FLinearColor& Color = GetSliceColor(Index);
		const float Width = FMath::Max(X1 - X0, 1.f);

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(FVector2f(X0, Top))),
			WhiteBrush, ESlateDrawEffect::None, Color.CopyWithNewOpacity(bSelected ? 0.22f : 0.1f));

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			Geometry.ToPaintGeometry(FVector2f(Width, LabelHeight), FSlateLayoutTransform(FVector2f(X0, Top))),
			WhiteBrush, ESlateDrawEffect::None, Color.CopyWithNewOpacity(bSelected ? 0.75f : 0.4f));

		const float EdgeThickness = bSelected ? 2.f : 1.f;
		for (const float EdgeX : { X0, X1 })
		{
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(),
				TArray<FVector2f>{ FVector2f(EdgeX, Top), FVector2f(EdgeX, Size.Y) },
				ESlateDrawEffect::None, Color.CopyWithNewOpacity(0.9f), false, EdgeThickness);
		}

		// Label is clipped to the slice, otherwise short slices spill their names over the neighbours
		FString Label = FString::Printf(TEXT("%02d"), Index + 1);
		if (!Slice.Name.IsEmpty())
		{
			Label += TEXT("  ") + Slice.Name;
		}

		const FGeometry LabelGeometry = Geometry.MakeChild(FVector2f(Width, LabelHeight), FSlateLayoutTransform(FVector2f(X0, Top)));
		OutDrawElements.PushClip(FSlateClippingZone(LabelGeometry));
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			Geometry.ToPaintGeometry(FVector2f(FMath::Max(Width - 6.f, 1.f), LabelHeight), FSlateLayoutTransform(FVector2f(X0 + 4.f, Top + 2.f))),
			Label, Font, ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.02f));
		OutDrawElements.PopClip();
	}
}

FReply SAudioSlicerWaveform::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!PCM.IsValid() || !Session.IsValid() || DragMode != EDragMode::None)
	{
		return FReply::Unhandled();
	}

	const FKey Button = MouseEvent.GetEffectingButton();
	const float LocalX = float(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
	const float Width = float(MyGeometry.GetLocalSize().X);

	DragStartX = LocalX;
	DragStartTime = LocalXToClampedTime(MyGeometry, MouseEvent.GetScreenSpacePosition());

	if (Button == EKeys::MiddleMouseButton)
	{
		DragMode = EDragMode::Pan;
		DragViewStart = ViewStart;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (Button == EKeys::RightMouseButton)
	{
		// The menu opens on release, like everywhere else in the editor
		return FReply::Handled();
	}

	if (Button != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	DragMode = EDragMode::Pending;
	DragSlice = HitTestEdge(LocalX, Width, bPendingEdgeIsStart);
	bPendingOnEdge = DragSlice != INDEX_NONE;

	if (!bPendingOnEdge)
	{
		DragSlice = Session->FindSliceAt(DragStartTime);
	}

	if (Session->Slices.IsValidIndex(DragSlice))
	{
		DragSliceBackup = Session->Slices[DragSlice];
		DragGrabOffset = DragStartTime - DragSliceBackup.StartTime;
	}

	return FReply::Handled()
		.CaptureMouse(SharedThis(this))
		.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SAudioSlicerWaveform::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (DragMode == EDragMode::None || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	UAudioSlicerSession* SessionPtr = Session.Get();
	if (!SessionPtr)
	{
		return FReply::Unhandled();
	}

	using namespace AudioSlicerWaveform;

	const float LocalX = float(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
	const float Width = float(MyGeometry.GetLocalSize().X);
	const float Time = LocalXToClampedTime(MyGeometry, MouseEvent.GetScreenSpacePosition());

	if (DragMode == EDragMode::Pending)
	{
		if (FMath::Abs(LocalX - DragStartX) < DragThreshold)
		{
			return FReply::Handled();
		}

		if (bPendingOnEdge)
		{
			DragMode = bPendingEdgeIsStart ? EDragMode::ResizeStart : EDragMode::ResizeEnd;
			BeginEdit(LOCTEXT("ResizeSlice", "Resize Slice"));
		}
		else if (SessionPtr->Slices.IsValidIndex(DragSlice))
		{
			DragMode = EDragMode::Move;
			BeginEdit(LOCTEXT("MoveSlice", "Move Slice"));
		}
		else
		{
			DragMode = EDragMode::Create;
			CreateStart = DragStartTime;
		}
	}

	switch (DragMode)
	{
	case EDragMode::Pan:
		ViewStart = DragViewStart - (LocalX - DragStartX) / Width * ViewDuration;
		ClampView();
		NotifyViewChanged();
		break;

	case EDragMode::Create:
		CreateEnd = Time;
		break;

	case EDragMode::ResizeStart:
		if (SessionPtr->Slices.IsValidIndex(DragSlice))
		{
			FAudioSliceRange& Slice = SessionPtr->Slices[DragSlice];
			Slice.StartTime = FMath::Clamp(Time, 0.f, FMath::Max(0.f, Slice.EndTime - MinSliceLength));
		}
		break;

	case EDragMode::ResizeEnd:
		if (SessionPtr->Slices.IsValidIndex(DragSlice))
		{
			FAudioSliceRange& Slice = SessionPtr->Slices[DragSlice];
			Slice.EndTime = FMath::Clamp(Time, FMath::Min(GetTotalDuration(), Slice.StartTime + MinSliceLength), GetTotalDuration());
		}
		break;

	case EDragMode::Move:
		if (SessionPtr->Slices.IsValidIndex(DragSlice))
		{
			FAudioSliceRange& Slice = SessionPtr->Slices[DragSlice];
			const float Length = DragSliceBackup.GetDuration();
			Slice.StartTime = FMath::Clamp(Time - DragGrabOffset, 0.f, FMath::Max(GetTotalDuration() - Length, 0.f));
			Slice.EndTime = Slice.StartTime + Length;
		}
		break;

	default:
		break;
	}

	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SAudioSlicerWaveform::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();

	if (Button == EKeys::RightMouseButton && DragMode == EDragMode::None)
	{
		if (PCM.IsValid() && Session.IsValid() && OnContextMenu.IsBound())
		{
			const float Time = LocalXToClampedTime(MyGeometry, MouseEvent.GetScreenSpacePosition());
			const int32 SliceIndex = Session->FindSliceAt(Time);
			if (SliceIndex != INDEX_NONE)
			{
				OnSliceSelected.ExecuteIfBound(SliceIndex);
			}

			TSharedPtr<SWidget> MenuContent = OnContextMenu.Execute(SliceIndex, Time);
			if (MenuContent.IsValid())
			{
				const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
				FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuContent.ToSharedRef(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
			}
		}
		return FReply::Handled();
	}

	const bool bEndsPan = DragMode == EDragMode::Pan && Button == EKeys::MiddleMouseButton;
	const bool bEndsLeftDrag = DragMode != EDragMode::Pan && DragMode != EDragMode::None && Button == EKeys::LeftMouseButton;
	if (!bEndsPan && !bEndsLeftDrag)
	{
		return FReply::Unhandled();
	}

	using namespace AudioSlicerWaveform;

	UAudioSlicerSession* SessionPtr = Session.Get();

	switch (DragMode)
	{
	case EDragMode::Pending:
		// Plain click: move the playhead, select what's under it
		OnSeek.ExecuteIfBound(DragStartTime);
		OnSliceSelected.ExecuteIfBound(DragSlice);
		break;

	case EDragMode::Create:
		if (SessionPtr && FMath::Abs(CreateEnd - CreateStart) >= MinSliceLength)
		{
			BeginEdit(LOCTEXT("AddSlice", "Add Slice"));

			FAudioSliceRange& NewSlice = SessionPtr->Slices.AddDefaulted_GetRef();
			NewSlice.StartTime = FMath::Min(CreateStart, CreateEnd);
			NewSlice.EndTime = FMath::Max(CreateStart, CreateEnd);

			DragSlice = SessionPtr->Slices.Num() - 1;
			FinishEdit();
		}
		break;

	case EDragMode::ResizeStart:
	case EDragMode::ResizeEnd:
	case EDragMode::Move:
		FinishEdit();
		break;

	default:
		break;
	}

	DragMode = EDragMode::None;
	DragSlice = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SAudioSlicerWaveform::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !PCM.IsValid() || !Session.IsValid())
	{
		return FReply::Unhandled();
	}

	const int32 SliceIndex = Session->FindSliceAt(LocalXToClampedTime(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	if (SliceIndex == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	OnSliceDoubleClicked.ExecuteIfBound(SliceIndex);
	return FReply::Handled();
}

FReply SAudioSlicerWaveform::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!PCM.IsValid())
	{
		return FReply::Unhandled();
	}

	const float Delta = MouseEvent.GetWheelDelta();

	if (MouseEvent.IsShiftDown())
	{
		ViewStart -= Delta * ViewDuration * 0.1f;
	}
	else
	{
		// Zoom around the mouse so the spot under the cursor stays put
		const float LocalX = float(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
		const float Width = float(MyGeometry.GetLocalSize().X);
		const float Anchor = XToTime(LocalX, Width);

		ViewDuration *= Delta > 0.f ? 0.8f : 1.25f;
		ClampView();
		ViewStart = Anchor - LocalX / Width * ViewDuration;
	}

	ClampView();
	NotifyViewChanged();
	return FReply::Handled();
}

FCursorReply SAudioSlicerWaveform::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	switch (DragMode)
	{
	case EDragMode::Pan:
	case EDragMode::Move:
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	case EDragMode::ResizeStart:
	case EDragMode::ResizeEnd:
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	default:
		break;
	}

	if (!PCM.IsValid())
	{
		return FCursorReply::Unhandled();
	}

	const float LocalX = float(MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition()).X);
	bool bIsStart = false;
	if (HitTestEdge(LocalX, float(MyGeometry.GetLocalSize().X), bIsStart) != INDEX_NONE)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	if (Session.IsValid() && Session->FindSliceAt(LocalXToClampedTime(MyGeometry, CursorEvent.GetScreenSpacePosition())) != INDEX_NONE)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return FCursorReply::Cursor(EMouseCursor::Default);
}

void SAudioSlicerWaveform::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Alt-tab or a modal window mid-drag: keep what was done so far
	if (Transaction.IsValid())
	{
		FinishEdit();
	}
	DragMode = EDragMode::None;
	DragSlice = INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE

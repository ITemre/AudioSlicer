// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWindow.h"
#include "AudioSlicerCore.h"
#include "SAudioSlicerWaveform.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Sound/SoundWave.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

void SAudioSlicerWindow::PlaySlice(int32 SliceIndex)
{
	if (Session->Slices.IsValidIndex(SliceIndex))
	{
		const FAudioSliceRange& Slice = Session->Slices[SliceIndex];
		StartPlayback(Slice.StartTime, Slice.EndTime);
	}
}

void SAudioSlicerWindow::OnSeek(float Time)
{
	if (bIsPlaying)
	{
		StartPlayback(Time, GetDuration());
	}
	else
	{
		PlayheadTime = Time;
		PlayFrom = Time;
	}
}

void SAudioSlicerWindow::PlayFromTime(float Time)
{
	StartPlayback(Time, GetDuration());
}

void SAudioSlicerWindow::TogglePlayback()
{
	if (bIsPlaying)
	{
		PausePlayback();
		return;
	}

	// At the very end, start over instead of playing nothing
	const float From = PlayheadTime >= GetDuration() - 0.001f ? 0.f : PlayheadTime;
	StartPlayback(From, GetDuration());
}

void SAudioSlicerWindow::StartPlayback(float From, float Until)
{
	USoundWave* Sound = Session->Sound;
	if (!Sound || !PCM.IsValid() || !GEditor)
	{
		return;
	}

	PausePlayback();

	// The editor's preview component is the same one the Content Browser uses, so starting
	// a preview there stops ours and the other way round, which is what you'd expect.
	UAudioComponent* Component = GEditor->ResetPreviewAudioComponent(Sound);
	if (!Component)
	{
		return;
	}

	Component->Play(From);
	PreviewComponent = Component;

	bIsPlaying = true;
	PlayFrom = From;
	PlayUntil = Until;
	PlayheadTime = From;
	PlayStartedAt = FPlatformTime::Seconds();

	if (!PlaybackTimer.IsValid())
	{
		PlaybackTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAudioSlicerWindow::TickPlayback));
	}
}

void SAudioSlicerWindow::PausePlayback()
{
	if (!bIsPlaying)
	{
		return;
	}

	if (UAudioComponent* Component = PreviewComponent.Get())
	{
		Component->Stop();
	}
	bIsPlaying = false;
}

void SAudioSlicerWindow::StopPlayback()
{
	if (bIsPlaying)
	{
		PausePlayback();
		PlayheadTime = PlayFrom;
	}
	else
	{
		PlayheadTime = 0.f;
		PlayFrom = 0.f;
	}
}

EActiveTimerReturnType SAudioSlicerWindow::TickPlayback(double CurrentTime, float DeltaTime)
{
	if (!bIsPlaying)
	{
		PlaybackTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	PlayheadTime = PlayFrom + float(FPlatformTime::Seconds() - PlayStartedAt);

	const UAudioComponent* Component = PreviewComponent.Get();
	if (PlayheadTime >= PlayUntil || !Component || !Component->IsPlaying())
	{
		PausePlayback();
		PlayheadTime = PlayFrom;
		PlaybackTimer.Reset();
		Waveform->Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Stop;
	}

	Waveform->ScrollIntoView(PlayheadTime);
	Waveform->Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE

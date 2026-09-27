// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class USoundWave;
struct FAudioSliceRange;
struct FAudioSliceExportOptions;
struct FAudioSilenceDetectionSettings;

DECLARE_LOG_CATEGORY_EXTERN(LogAudioSlicer, Log, All);

/**
 * Decoded audio of a Sound Wave as interleaved 16-bit samples.
 * The importer converts everything to 16-bit PCM on the way in, so this is exactly what the asset holds.
 */
struct FAudioSlicerPCM
{
	TArray<int16> Samples;
	int32 SampleRate = 0;
	int32 NumChannels = 0;

	bool Load(const USoundWave* SoundWave);

	bool IsValid() const { return SampleRate > 0 && NumChannels > 0 && Samples.Num() >= NumChannels; }
	int32 GetNumFrames() const { return NumChannels > 0 ? Samples.Num() / NumChannels : 0; }
	float GetDuration() const { return SampleRate > 0 ? float(GetNumFrames()) / float(SampleRate) : 0.f; }

	int32 TimeToFrame(float Seconds) const;
	float FrameToTime(int32 Frame) const;
	int32 MsToFrames(float Milliseconds) const;

	/** Closest frame to Frame where the signal changes sign. Gives up after SearchRadius frames in either direction. */
	int32 FindZeroCrossing(int32 Frame, int32 SearchRadius) const;

	/** Everything louder than the threshold, merged and padded as the settings say. */
	TArray<FAudioSliceRange> DetectSoundRegions(const FAudioSilenceDetectionSettings& Settings) const;
};

namespace AudioSlicer
{
	/** Sanitized asset name for a slice, falling back to the naming pattern when the slice has no name. */
	FString GetSliceAssetName(const FAudioSliceRange& Slice, const FString& BaseName, int32 Index);

	/** True if Path is a folder assets can be created in, like /Game/Audio. */
	bool IsValidContentFolder(const FString& Path);

	FAssetData FindExistingAsset(const FString& FolderPath, const FString& AssetName);

	TArray<USoundWave*> ExportSlices(const FAudioSlicerPCM& PCM, const USoundWave* Source, const FString& BaseName, const TArray<FAudioSliceRange>& Slices, const FAudioSliceExportOptions& Options);
}

// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AudioSlicerLibrary.generated.h"

class USoundWave;

/** One part of a sound, in seconds from the start of the source. */
USTRUCT(BlueprintType)
struct AUDIOSLICER_API FAudioSliceRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "0", Units = "s"))
	float StartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "0", Units = "s"))
	float EndTime = 0.f;

	/** Asset name of the slice. Leave it empty to use the naming pattern from the settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer")
	FString Name;

	float GetDuration() const { return EndTime - StartTime; }
};

USTRUCT(BlueprintType)
struct AUDIOSLICER_API FAudioSliceExportOptions
{
	GENERATED_BODY()

	/** Content folder for the new assets, e.g. /Game/Audio/Dialogue. Empty means the folder of the source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ContentDir))
	FString DestinationPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "0", Units = "ms"))
	float FadeInMs = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "0", Units = "ms"))
	float FadeOutMs = 5.f;

	/** Move each cut to the closest zero crossing (up to 5 ms away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer")
	bool bSnapToZeroCrossing = true;

	/** Take sound class, submix, attenuation, concurrency and compression over from the source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer")
	bool bCopySourceSettings = true;

	/**
	 * If a sound with the same name exists, update it in place (references to it stay intact).
	 * Otherwise the new slice gets a unique name instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer")
	bool bOverwriteExisting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer")
	bool bSaveAssets = false;
};

USTRUCT(BlueprintType)
struct AUDIOSLICER_API FAudioSilenceDetectionSettings
{
	GENERATED_BODY()

	/** Anything quieter than this counts as silence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (DisplayName = "Threshold (dB)", ClampMin = "-96", ClampMax = "0"))
	float ThresholdDb = -40.f;

	/** Pauses shorter than this stay inside a slice, so a breath between two words doesn't split them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "10", Units = "ms"))
	float MinSilenceMs = 300.f;

	/** Drops clicks and other short noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "0", Units = "ms"))
	float MinSliceMs = 150.f;

	/** Extra room added before and after each slice so soft onsets and tails aren't cut off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Slicer", meta = (ClampMin = "0", Units = "ms"))
	float PaddingMs = 40.f;
};

/**
 * The slicing itself, usable without the window, e.g. from an Editor Utility Blueprint or Python
 * (unreal.AudioSlicerLibrary.export_slices).
 */
UCLASS()
class AUDIOSLICER_API UAudioSlicerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Writes every range as its own Sound Wave asset. Returns the assets that were created or updated. */
	UFUNCTION(BlueprintCallable, Category = "Audio Slicer")
	static TArray<USoundWave*> ExportSlices(USoundWave* Source, const TArray<FAudioSliceRange>& Slices, const FAudioSliceExportOptions& Options);

	/** Splits the sound at its silent parts. The returned ranges have no names yet. */
	UFUNCTION(BlueprintCallable, Category = "Audio Slicer")
	static TArray<FAudioSliceRange> DetectSlices(USoundWave* Source, const FAudioSilenceDetectionSettings& Settings);
};

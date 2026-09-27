// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AudioSlicerLibrary.h"
#include "AudioSlicerSettings.generated.h"

/** Found under Editor Preferences > Plugins > Audio Slicer, and in the Options menu of the window. */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Audio Slicer"))
class AUDIOSLICER_API UAudioSlicerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Name for slices you don't name yourself. {Base} is the base name from the window, {Index} the running number. */
	UPROPERTY(config, EditAnywhere, Category = "Naming")
	FString NamePattern = TEXT("{Base}_{Index}");

	/** Digits for {Index}. With 2 you get _01, _02 and so on. */
	UPROPERTY(config, EditAnywhere, Category = "Naming", meta = (ClampMin = "1", ClampMax = "6"))
	int32 IndexDigits = 2;

	/** Where slices go, relative to the source asset. Leave empty to put them right next to it. */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	FString DefaultSubfolder = TEXT("Slices");

	UPROPERTY(config, EditAnywhere, Category = "Export", meta = (ClampMin = "0", ClampMax = "1000", Units = "ms"))
	float FadeInMs = 2.f;

	UPROPERTY(config, EditAnywhere, Category = "Export", meta = (ClampMin = "0", ClampMax = "1000", Units = "ms"))
	float FadeOutMs = 5.f;

	/** Move each cut to the closest zero crossing. Together with the short fades this keeps the cuts from clicking. */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bSnapToZeroCrossing = true;

	/** Take sound class, submix, attenuation, concurrency and compression over from the source. */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bCopySourceSettings = true;

	/** Save the new assets right away instead of leaving them unsaved. */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bSaveAfterExport = false;

	UPROPERTY(config, EditAnywhere, Category = "Detect Slices", meta = (ShowOnlyInnerProperties))
	FAudioSilenceDetectionSettings Detection;

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	FString FormatSliceName(const FString& BaseName, int32 Index) const;
	FAudioSliceExportOptions MakeExportOptions(const FString& DestinationPath) const;
};

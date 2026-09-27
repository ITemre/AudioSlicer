// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AudioSlicerLibrary.h"
#include "AudioSlicerSession.generated.h"

class USoundWave;

/**
 * What the window is working on. Lives in a UObject so every edit can go through
 * the editor transaction system and Ctrl+Z just works.
 */
UCLASS()
class UAudioSlicerSession : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USoundWave> Sound;

	/** Always sorted by start time, see SortSlices. */
	UPROPERTY()
	TArray<FAudioSliceRange> Slices;

	/** Sorts by start time and returns the new index of the slice that was at TrackedIndex. */
	int32 SortSlices(int32 TrackedIndex = INDEX_NONE);

	/** Index of the slice under Time, preferring the one that starts last when they overlap. */
	int32 FindSliceAt(float Time) const;
};

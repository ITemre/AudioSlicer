// Copyright 2026 Emre Erdogan.

#include "AudioSlicerSession.h"
#include "Sound/SoundWave.h"

int32 UAudioSlicerSession::SortSlices(int32 TrackedIndex)
{
	TArray<int32> Order;
	Order.Reserve(Slices.Num());
	for (int32 Index = 0; Index < Slices.Num(); ++Index)
	{
		Order.Add(Index);
	}

	Order.StableSort([this](int32 A, int32 B)
	{
		return Slices[A].StartTime < Slices[B].StartTime;
	});

	TArray<FAudioSliceRange> Sorted;
	Sorted.Reserve(Slices.Num());
	int32 NewTrackedIndex = INDEX_NONE;

	for (int32 NewIndex = 0; NewIndex < Order.Num(); ++NewIndex)
	{
		Sorted.Add(Slices[Order[NewIndex]]);
		if (Order[NewIndex] == TrackedIndex)
		{
			NewTrackedIndex = NewIndex;
		}
	}

	Slices = MoveTemp(Sorted);
	return NewTrackedIndex;
}

int32 UAudioSlicerSession::FindSliceAt(float Time) const
{
	for (int32 Index = Slices.Num() - 1; Index >= 0; --Index)
	{
		if (Time >= Slices[Index].StartTime && Time <= Slices[Index].EndTime)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

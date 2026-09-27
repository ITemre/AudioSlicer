// Copyright 2026 Emre Erdogan.

#include "AudioSlicerLibrary.h"
#include "AudioSlicerCore.h"
#include "Sound/SoundWave.h"

TArray<USoundWave*> UAudioSlicerLibrary::ExportSlices(USoundWave* Source, const TArray<FAudioSliceRange>& Slices, const FAudioSliceExportOptions& Options)
{
	FAudioSlicerPCM PCM;
	if (!PCM.Load(Source))
	{
		return {};
	}
	return AudioSlicer::ExportSlices(PCM, Source, Source->GetName(), Slices, Options);
}

TArray<FAudioSliceRange> UAudioSlicerLibrary::DetectSlices(USoundWave* Source, const FAudioSilenceDetectionSettings& Settings)
{
	FAudioSlicerPCM PCM;
	if (!PCM.Load(Source))
	{
		return {};
	}
	return PCM.DetectSoundRegions(Settings);
}

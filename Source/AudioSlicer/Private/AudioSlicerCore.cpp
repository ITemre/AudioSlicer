// Copyright 2026 Emre Erdogan.

#include "AudioSlicerCore.h"
#include "AudioSlicerLibrary.h"
#include "AudioSlicerSettings.h"
#include "Audio.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/SoundFactory.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

// Use the same bitwise finite check on all supported engine versions and platforms.

bool FAudioSlicerPCM::Load(const USoundWave* SoundWave)
{
	Samples.Reset();
	SampleRate = 0;
	NumChannels = 0;

	if (!SoundWave)
	{
		return false;
	}

	TArray<uint8> RawBytes;
	uint32 RawSampleRate = 0;
	uint16 RawNumChannels = 0;
	if (!SoundWave->GetImportedSoundWaveData(RawBytes, RawSampleRate, RawNumChannels) || RawBytes.Num() == 0)
	{
		UE_LOG(LogAudioSlicer, Warning, TEXT("%s has no imported audio data. Only imported sounds can be sliced."), *SoundWave->GetPathName());
		return false;
	}

	SampleRate = int32(RawSampleRate);
	NumChannels = int32(RawNumChannels);

	Samples.SetNumUninitialized(RawBytes.Num() / int32(sizeof(int16)));
	FMemory::Memcpy(Samples.GetData(), RawBytes.GetData(), Samples.Num() * sizeof(int16));

	// Discard any incomplete trailing frame.
	Samples.SetNum(GetNumFrames() * NumChannels);

	return IsValid();
}

int32 FAudioSlicerPCM::TimeToFrame(float Seconds) const
{
	if (!FGenericPlatformMath::IsFinite(Seconds) || SampleRate <= 0)
	{
		return 0;
	}
	const double Frame = FMath::Clamp(double(Seconds) * SampleRate, 0.0, double(GetNumFrames()));
	return int32(FMath::RoundToInt64(Frame));
}

float FAudioSlicerPCM::FrameToTime(int32 Frame) const
{
	return SampleRate > 0 ? float(Frame) / float(SampleRate) : 0.f;
}

int32 FAudioSlicerPCM::MsToFrames(float Milliseconds) const
{
	if (!FGenericPlatformMath::IsFinite(Milliseconds) || SampleRate <= 0)
	{
		return 0;
	}
	const double Frames = FMath::Clamp(double(Milliseconds) * 0.001 * SampleRate, 0.0, double(MAX_int32));
	return int32(FMath::RoundToInt64(Frames));
}

int32 FAudioSlicerPCM::FindZeroCrossing(int32 Frame, int32 SearchRadius) const
{
	const int32 NumFrames = GetNumFrames();
	if (Frame <= 0 || Frame >= NumFrames)
	{
		return Frame;
	}

	auto MixAt = [this](int32 InFrame)
	{
		int32 Sum = 0;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			Sum += Samples[InFrame * NumChannels + Channel];
		}
		return Sum;
	};

	auto IsCrossing = [&MixAt](int32 InFrame)
	{
		const int32 Previous = MixAt(InFrame - 1);
		const int32 Current = MixAt(InFrame);
		return Current == 0 || (Previous < 0) != (Current < 0);
	};

	// Walk outwards from the cut so the nearest crossing wins
	for (int32 Offset = 0; Offset <= SearchRadius; ++Offset)
	{
		const int32 Before = Frame - Offset;
		if (Before > 0 && IsCrossing(Before))
		{
			return Before;
		}

		const int32 After = Frame + Offset;
		if (After < NumFrames && IsCrossing(After))
		{
			return After;
		}
	}

	return Frame;
}

TArray<FAudioSliceRange> FAudioSlicerPCM::DetectSoundRegions(const FAudioSilenceDetectionSettings& Settings) const
{
	TArray<FAudioSliceRange> Regions;
	if (!IsValid() || !FGenericPlatformMath::IsFinite(Settings.ThresholdDb)
		|| !FGenericPlatformMath::IsFinite(Settings.MinSilenceMs) || !FGenericPlatformMath::IsFinite(Settings.MinSliceMs)
		|| !FGenericPlatformMath::IsFinite(Settings.PaddingMs))
	{
		return Regions;
	}

	const int32 NumFrames = GetNumFrames();

	// 10 ms blocks: fine enough for gaps between words, coarse enough not to flicker around every zero crossing
	const int32 BlockFrames = FMath::Max(1, SampleRate / 100);
	const float Threshold = 32768.f * FMath::Pow(10.f, Settings.ThresholdDb / 20.f);

	TArray<TPair<int32, int32>> Loud;
	int32 LoudStart = INDEX_NONE;

	for (int32 BlockStart = 0; BlockStart < NumFrames; BlockStart += BlockFrames)
	{
		const int32 BlockEnd = FMath::Min(BlockStart + BlockFrames, NumFrames);

		int32 Peak = 0;
		for (int32 SampleIndex = BlockStart * NumChannels; SampleIndex < BlockEnd * NumChannels; ++SampleIndex)
		{
			Peak = FMath::Max(Peak, FMath::Abs(int32(Samples[SampleIndex])));
		}

		const bool bLoud = float(Peak) >= Threshold;
		if (bLoud && LoudStart == INDEX_NONE)
		{
			LoudStart = BlockStart;
		}
		else if (!bLoud && LoudStart != INDEX_NONE)
		{
			Loud.Emplace(LoudStart, BlockStart);
			LoudStart = INDEX_NONE;
		}
	}

	if (LoudStart != INDEX_NONE)
	{
		Loud.Emplace(LoudStart, NumFrames);
	}

	// Short pauses belong to the slice around them
	const int32 MinSilenceFrames = MsToFrames(Settings.MinSilenceMs);
	TArray<TPair<int32, int32>> Merged;
	for (const TPair<int32, int32>& Range : Loud)
	{
		if (Merged.Num() > 0 && Range.Key - Merged.Last().Value < MinSilenceFrames)
		{
			Merged.Last().Value = Range.Value;
		}
		else
		{
			Merged.Add(Range);
		}
	}

	const int32 MinSliceFrames = MsToFrames(Settings.MinSliceMs);
	const int32 PaddingFrames = MsToFrames(Settings.PaddingMs);
	int32 PreviousEnd = 0;

	for (const TPair<int32, int32>& Range : Merged)
	{
		if (Range.Value - Range.Key < MinSliceFrames)
		{
			continue;
		}

		// Padding must not reach back into the slice before
		const int32 Start = FMath::Max(Range.Key - PaddingFrames, PreviousEnd);
		const int32 End = int32(FMath::Min(int64(Range.Value) + PaddingFrames, int64(NumFrames)));
		if (End <= Start)
		{
			continue;
		}

		FAudioSliceRange& Region = Regions.AddDefaulted_GetRef();
		Region.StartTime = FrameToTime(Start);
		Region.EndTime = FrameToTime(End);

		PreviousEnd = End;
	}

	return Regions;
}

namespace AudioSlicer
{
	static void ApplyFades(TArray<int16>& Samples, int32 NumChannels, int32 FadeInFrames, int32 FadeOutFrames)
	{
		const int32 NumFrames = Samples.Num() / NumChannels;

		// On very short slices the two fades would overlap, so they split the slice between them
		FadeInFrames = FMath::Min(FadeInFrames, NumFrames / 2);
		FadeOutFrames = FMath::Min(FadeOutFrames, NumFrames - FadeInFrames);

		auto ScaleFrame = [&Samples, NumChannels](int32 Frame, float Gain)
		{
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				int16& Sample = Samples[Frame * NumChannels + Channel];
				Sample = int16(FMath::RoundToInt(float(Sample) * Gain));
			}
		};

		for (int32 Frame = 0; Frame < FadeInFrames; ++Frame)
		{
			ScaleFrame(Frame, float(Frame) / float(FadeInFrames));
		}

		for (int32 Step = 0; Step < FadeOutFrames; ++Step)
		{
			ScaleFrame(NumFrames - 1 - Step, float(Step) / float(FadeOutFrames));
		}
	}

	static void CopyPlaybackSettings(const USoundWave& From, USoundWave& To)
	{
		// Some of these are private and the list shifts a bit between engine versions,
		// so go through reflection and skip whatever a version doesn't have.
		static const FName PropertyNames[] =
		{
			TEXT("SoundClassObject"),
			TEXT("SoundSubmixObject"),
			TEXT("SoundSubmixSends"),
			TEXT("SourceEffectChain"),
			TEXT("AttenuationSettings"),
			TEXT("bOverrideConcurrency"),
			TEXT("ConcurrencySet"),
			TEXT("ConcurrencyOverrides"),
			TEXT("Priority"),
			TEXT("Volume"),
			TEXT("Pitch"),
			TEXT("CompressionQuality"),
			TEXT("LoadingBehavior"),
		};

		for (const FName& PropertyName : PropertyNames)
		{
			if (const FProperty* Property = USoundWave::StaticClass()->FindPropertyByName(PropertyName))
			{
				Property->CopyCompleteValue_InContainer(&To, &From);
			}
		}

		To.SetSoundAssetCompressionType(From.GetSoundAssetCompressionType(), false);
		To.PostEditChange();
	}

	static USoundWave* ImportWave(const TArray<uint8>& WaveFile, const FString& PackageName, const FString& AssetName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}
		Package->FullyLoad();

		const bool bIsNew = FindObject<USoundWave>(Package, *AssetName) == nullptr;

		// With the dialogs suppressed the factory updates an existing wave in place rather than
		// replacing the object, so everything that references it keeps working.
		USoundFactory* Factory = NewObject<USoundFactory>();
		Factory->SuppressImportDialogs();

		const uint8* Buffer = WaveFile.GetData();
		UObject* Imported = Factory->FactoryCreateBinary(
			USoundWave::StaticClass(),
			Package,
			FName(*AssetName),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			TEXT("wav"),
			Buffer,
			Buffer + WaveFile.Num(),
			GWarn);

		USoundWave* Wave = Cast<USoundWave>(Imported);
		if (Wave && bIsNew)
		{
			FAssetRegistryModule::AssetCreated(Wave);
		}
		return Wave;
	}

	FString GetSliceAssetName(const FAudioSliceRange& Slice, const FString& BaseName, int32 Index)
	{
		FString Name = Slice.Name.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			Name = GetDefault<UAudioSlicerSettings>()->FormatSliceName(BaseName, Index);
		}
		return ObjectTools::SanitizeObjectName(Name);
	}

	bool IsValidContentFolder(const FString& Path)
	{
		return !Path.IsEmpty() && FPackageName::IsValidLongPackageName(Path / TEXT("Slice"));
	}

	FAssetData FindExistingAsset(const FString& FolderPath, const FString& AssetName)
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FString PackageName = FolderPath / AssetName;
		return AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(PackageName + TEXT(".") + AssetName));
	}

	TArray<USoundWave*> ExportSlices(const FAudioSlicerPCM& PCM, const USoundWave* Source, const FString& BaseName, const TArray<FAudioSliceRange>& Slices, const FAudioSliceExportOptions& Options)
	{
		TArray<USoundWave*> Exported;
		if (!PCM.IsValid() || Slices.Num() == 0)
		{
			return Exported;
		}
		if (!FGenericPlatformMath::IsFinite(Options.FadeInMs) || !FGenericPlatformMath::IsFinite(Options.FadeOutMs))
		{
			UE_LOG(LogAudioSlicer, Warning, TEXT("Can't export with non-finite fade durations."));
			return Exported;
		}

		FString Folder = Options.DestinationPath.TrimStartAndEnd();
		if (Folder.IsEmpty() && Source)
		{
			Folder = FPackageName::GetLongPackagePath(Source->GetPackage()->GetName());
		}
		Folder.RemoveFromEnd(TEXT("/"));

		if (!IsValidContentFolder(Folder))
		{
			UE_LOG(LogAudioSlicer, Error, TEXT("Can't export to '%s', that is not a content folder."), *Folder);
			return Exported;
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

		// 5 ms either way finds a crossing in anything above sub-bass
		const int32 ZeroCrossingRadius = PCM.MsToFrames(5.f);
		const int32 FadeInFrames = PCM.MsToFrames(Options.FadeInMs);
		const int32 FadeOutFrames = PCM.MsToFrames(Options.FadeOutMs);

		TArray<UPackage*> Packages;
		TSet<FName> WrittenPackages;

		FScopedSlowTask SlowTask(float(Slices.Num()), LOCTEXT("ExportingSlices", "Exporting slices..."));
		SlowTask.MakeDialogDelayed(0.5f, true);

		for (int32 Index = 0; Index < Slices.Num(); ++Index)
		{
			SlowTask.EnterProgressFrame(1.f);
			if (SlowTask.ShouldCancel())
			{
				break;
			}

			const FAudioSliceRange& Slice = Slices[Index];
			FString AssetName = GetSliceAssetName(Slice, BaseName, Index + 1);
			FString PackageName = Folder / AssetName;
			if (AssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName)
				|| !FGenericPlatformMath::IsFinite(Slice.StartTime) || !FGenericPlatformMath::IsFinite(Slice.EndTime))
			{
				UE_LOG(LogAudioSlicer, Warning, TEXT("Skipped slice %d, invalid asset name or non-finite time."), Index + 1);
				continue;
			}
			if (Options.bOverwriteExisting && ((Source && FName(*PackageName) == Source->GetPackage()->GetFName())
				|| WrittenPackages.Contains(FName(*PackageName))))
			{
				UE_LOG(LogAudioSlicer, Warning, TEXT("Skipped %s: cannot overwrite the source or another slice from this export. Choose a different name."), *AssetName);
				continue;
			}

			int32 StartFrame = PCM.TimeToFrame(FMath::Min(Slice.StartTime, Slice.EndTime));
			int32 EndFrame = PCM.TimeToFrame(FMath::Max(Slice.StartTime, Slice.EndTime));
			if (Options.bSnapToZeroCrossing)
			{
				StartFrame = PCM.FindZeroCrossing(StartFrame, ZeroCrossingRadius);
				EndFrame = PCM.FindZeroCrossing(EndFrame, ZeroCrossingRadius);
			}

			if (EndFrame - StartFrame < 2)
			{
				UE_LOG(LogAudioSlicer, Warning, TEXT("Skipped %s, the slice is empty."), *AssetName);
				continue;
			}

			const FAssetData Existing = FindExistingAsset(Folder, AssetName);
			if (Existing.IsValid())
			{
				if (!Options.bOverwriteExisting)
				{
					AssetTools.CreateUniqueAssetName(PackageName, FString(), PackageName, AssetName);
				}
				else if (Existing.AssetClassPath != USoundWave::StaticClass()->GetClassPathName())
				{
					UE_LOG(LogAudioSlicer, Error, TEXT("Skipped %s, there already is a %s with that name."), *AssetName, *Existing.AssetClassPath.GetAssetName().ToString());
					continue;
				}
			}

			TArray<int16> SliceSamples(PCM.Samples.GetData() + StartFrame * PCM.NumChannels, (EndFrame - StartFrame) * PCM.NumChannels);
			ApplyFades(SliceSamples, PCM.NumChannels, FadeInFrames, FadeOutFrames);

			TArray<uint8> WaveFile;
			SerializeWaveFile(WaveFile, reinterpret_cast<const uint8*>(SliceSamples.GetData()), SliceSamples.Num() * int32(sizeof(int16)), PCM.NumChannels, PCM.SampleRate);

			USoundWave* Wave = ImportWave(WaveFile, PackageName, AssetName);
			if (!Wave)
			{
				UE_LOG(LogAudioSlicer, Error, TEXT("Creating %s failed."), *PackageName);
				continue;
			}

			if (Options.bCopySourceSettings && Source)
			{
				CopyPlaybackSettings(*Source, *Wave);
			}

			Wave->MarkPackageDirty();
			Packages.Add(Wave->GetPackage());
			Exported.Add(Wave);
			WrittenPackages.Add(FName(*PackageName));
		}

		if (Options.bSaveAssets && Packages.Num() > 0)
		{
			if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
			{
				UE_LOG(LogAudioSlicer, Error, TEXT("Some exported slices could not be saved. Save the remaining dirty assets before closing the editor."));
			}
		}

		UE_LOG(LogAudioSlicer, Log, TEXT("Exported %d of %d slices to %s"), Exported.Num(), Slices.Num(), *Folder);
		return Exported;
	}
}

#undef LOCTEXT_NAMESPACE

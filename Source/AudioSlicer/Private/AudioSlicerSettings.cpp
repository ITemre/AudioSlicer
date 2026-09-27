// Copyright 2026 Emre Erdogan.

#include "AudioSlicerSettings.h"

FString UAudioSlicerSettings::FormatSliceName(const FString& BaseName, int32 Index) const
{
	FString Number = FString::FromInt(Index);
	const int32 Digits = FMath::Clamp(IndexDigits, 1, 6);
	if (Number.Len() < Digits)
	{
		Number = FString::ChrN(Digits - Number.Len(), TEXT('0')) + Number;
	}

	FString Name = NamePattern.IsEmpty() ? FString(TEXT("{Base}_{Index}")) : NamePattern;
	Name.ReplaceInline(TEXT("{Base}"), *BaseName);
	Name.ReplaceInline(TEXT("{Index}"), *Number);
	return Name;
}

FAudioSliceExportOptions UAudioSlicerSettings::MakeExportOptions(const FString& DestinationPath) const
{
	FAudioSliceExportOptions Options;
	Options.DestinationPath = DestinationPath;
	Options.FadeInMs = FadeInMs;
	Options.FadeOutMs = FadeOutMs;
	Options.bSnapToZeroCrossing = bSnapToZeroCrossing;
	Options.bCopySourceSettings = bCopySourceSettings;
	Options.bSaveAssets = bSaveAfterExport;
	return Options;
}

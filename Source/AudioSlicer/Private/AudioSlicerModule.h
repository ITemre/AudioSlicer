// Copyright 2026 Emre Erdogan.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSlateStyleSet;
class FSpawnTabArgs;
class SAudioSlicerWindow;
class SDockTab;
class USoundWave;

class FAudioSlicerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Opens the slicer tab, or brings it to the front, with Sound loaded. */
	void OpenSound(USoundWave* Sound);

private:
	void RegisterStyle();
	void UnregisterStyle();
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	TSharedPtr<FSlateStyleSet> Style;
	TWeakPtr<SAudioSlicerWindow> Window;
};

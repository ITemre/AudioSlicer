// Copyright 2026 Emre Erdogan.

#include "AudioSlicerModule.h"
#include "AudioSlicerCore.h"
#include "SAudioSlicerWindow.h"
#include "Brushes/SlateImageBrush.h"
#include "ContentBrowserMenuContexts.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundWave.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

DEFINE_LOG_CATEGORY(LogAudioSlicer);

#define LOCTEXT_NAMESPACE "AudioSlicer"

namespace AudioSlicerModule
{
	const FName TabName(TEXT("AudioSlicer"));
	const FName StyleName(TEXT("AudioSlicerStyle"));
	const FName IconName(TEXT("AudioSlicer.Icon"));
}

void FAudioSlicerModule::StartupModule()
{
	using namespace AudioSlicerModule;

	RegisterStyle();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FAudioSlicerModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Audio Slicer"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Cut a Sound Wave into separate assets"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(StyleName, IconName));

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAudioSlicerModule::RegisterMenus));
}

void FAudioSlicerModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AudioSlicerModule::TabName);
	}

	UnregisterStyle();
}

void FAudioSlicerModule::OpenSound(USoundWave* Sound)
{
	FGlobalTabmanager::Get()->TryInvokeTab(AudioSlicerModule::TabName);

	if (TSharedPtr<SAudioSlicerWindow> OpenWindow = Window.Pin())
	{
		OpenWindow->SetSound(Sound);
	}
}

void FAudioSlicerModule::RegisterStyle()
{
	using namespace AudioSlicerModule;

	Style = MakeShared<FSlateStyleSet>(StyleName);

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AudioSlicer")))
	{
		Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
	}

	Style->Set(IconName, new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("SlicerIcon"), TEXT(".svg")), FVector2f(16.f, 16.f)));

	FSlateStyleRegistry::RegisterSlateStyle(*Style);
}

void FAudioSlicerModule::UnregisterStyle()
{
	if (Style.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
		Style.Reset();
	}
}

void FAudioSlicerModule::RegisterMenus()
{
	using namespace AudioSlicerModule;

	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.SoundWave"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));

	Section.AddDynamicEntry(TEXT("OpenInAudioSlicer"), FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
	{
		const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context || Context->SelectedAssets.Num() != 1)
		{
			return;
		}

		const FAssetData Asset = Context->SelectedAssets[0];

		InSection.AddMenuEntry(
			TEXT("OpenInAudioSlicer"),
			LOCTEXT("OpenInAudioSlicer", "Open in Audio Slicer"),
			LOCTEXT("OpenInAudioSlicerTooltip", "Cut this sound into separate assets"),
			FSlateIcon(StyleName, IconName),
			FUIAction(FExecuteAction::CreateLambda([this, Asset]()
			{
				OpenSound(Cast<USoundWave>(Asset.GetAsset()));
			})));
	}));
}

TSharedRef<SDockTab> FAudioSlicerModule::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SAudioSlicerWindow> NewWindow = SNew(SAudioSlicerWindow);
	Window = NewWindow;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			NewWindow
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAudioSlicerModule, AudioSlicer)

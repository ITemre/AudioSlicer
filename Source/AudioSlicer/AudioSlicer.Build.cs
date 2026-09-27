// Copyright 2026 Emre Erdogan.

using UnrealBuildTool;

public class AudioSlicer : ModuleRules
{
	public AudioSlicer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"AudioEditor",
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"Projects",
			"PropertyEditor",
			"ToolMenus",
			"WorkspaceMenuStructure"
		});
	}
}

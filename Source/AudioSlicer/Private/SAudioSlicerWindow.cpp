// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWindow.h"
#include "AudioSlicerCore.h"
#include "AudioSlicerLibrary.h"
#include "AudioSlicerSession.h"
#include "AudioSlicerSettings.h"
#include "SAudioSlicerWaveform.h"
#include "Components/AudioComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Sound/SoundWave.h"
#include "Styling/AppStyle.h"
#include "Subsystems/ImportSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "AudioSlicer"

namespace AudioSlicerWindow
{
	const FName IndexColumn(TEXT("Index"));
	const FName NameColumn(TEXT("Name"));
	const FName StartColumn(TEXT("Start"));
	const FName EndColumn(TEXT("End"));
	const FName LengthColumn(TEXT("Length"));
	const FName ActionsColumn(TEXT("Actions"));

	constexpr float MinSliceLength = 0.01f;

	static FString FormatTime(float Seconds)
	{
		const double Clamped = FMath::Max(double(Seconds), 0.0);
		const int32 Minutes = FMath::FloorToInt(Clamped / 60.0);
		return FString::Printf(TEXT("%d:%05.2f"), Minutes, Clamped - Minutes * 60.0);
	}

	static FString GetDefaultFolder(const USoundWave* Sound)
	{
		if (!Sound)
		{
			return FString();
		}

		FString Folder = FPackageName::GetLongPackagePath(Sound->GetPackage()->GetName());

		FString Subfolder = GetDefault<UAudioSlicerSettings>()->DefaultSubfolder.TrimStartAndEnd();
		Subfolder.RemoveFromStart(TEXT("/"));
		Subfolder.RemoveFromEnd(TEXT("/"));
		if (!Subfolder.IsEmpty())
		{
			Folder /= Subfolder;
		}
		return Folder;
	}

	static void Notify(const FText& Message, SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 5.f;

		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(State);
		}
	}
}

/** One line in the slice list. Reads everything live from the window, so it never shows stale values. */
class SAudioSliceRow : public SMultiColumnTableRow<TSharedPtr<int32>>
{
public:
	SLATE_BEGIN_ARGS(SAudioSliceRow) {}
		SLATE_ARGUMENT(SAudioSlicerWindow*, Window)
		SLATE_ARGUMENT(int32, SliceIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Window = InArgs._Window;
		SliceIndex = InArgs._SliceIndex;
		FSuperRowType::Construct(FSuperRowType::FArguments().Padding(FMargin(0.f, 1.f)), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		using namespace AudioSlicerWindow;

		if (ColumnName == IndexColumn)
		{
			return SNew(SBox)
				.Padding(FMargin(6.f, 0.f))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%02d"), SliceIndex + 1)))
				];
		}

		if (ColumnName == NameColumn)
		{
			return SNew(SBox)
				.Padding(FMargin(2.f, 0.f))
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]()
					{
						return IsValidSlice() ? FText::FromString(Window->GetSlices()[SliceIndex].Name) : FText::GetEmpty();
					})
					.HintText_Lambda([this]()
					{
						return FText::FromString(Window->GetAutoName(SliceIndex));
					})
					.ToolTipText(LOCTEXT("NameTooltip", "Asset name for this slice. Leave it empty to use the base name and a number."))
					.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
					{
						if (CommitType != ETextCommit::OnCleared)
						{
							Window->RenameSlice(SliceIndex, NewText.ToString());
						}
					})
				];
		}

		if (ColumnName == StartColumn || ColumnName == EndColumn)
		{
			const bool bStart = ColumnName == StartColumn;

			return SNew(SBox)
				.Padding(FMargin(2.f, 0.f))
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<float>)
					.AllowSpin(false)
					.MinValue(0.f)
					.MaxValue_Lambda([this]() -> TOptional<float> { return Window->GetDuration(); })
					.MinFractionalDigits(3)
					.MaxFractionalDigits(3)
					.Value_Lambda([this, bStart]() -> TOptional<float>
					{
						if (!IsValidSlice())
						{
							return TOptional<float>();
						}
						const FAudioSliceRange& Slice = Window->GetSlices()[SliceIndex];
						return bStart ? Slice.StartTime : Slice.EndTime;
					})
					.OnValueCommitted_Lambda([this, bStart](float NewValue, ETextCommit::Type)
					{
						Window->SetSliceTime(SliceIndex, bStart, NewValue);
					})
				];
		}

		if (ColumnName == LengthColumn)
		{
			return SNew(SBox)
				.Padding(FMargin(6.f, 0.f))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]()
					{
						if (!IsValidSlice())
						{
							return FText::GetEmpty();
						}
						FNumberFormattingOptions Format;
						Format.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
						return FText::Format(LOCTEXT("SliceLength", "{0} s"), FText::AsNumber(Window->GetSlices()[SliceIndex].GetDuration(), &Format));
					})
				];
		}

		if (ColumnName == ActionsColumn)
		{
			return SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsFocusable(false)
					.ToolTipText(LOCTEXT("PlaySliceTooltip", "Play this slice"))
					.OnClicked_Lambda([this]()
					{
						Window->PlaySlice(SliceIndex);
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Animation.Forward"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsFocusable(false)
					.ToolTipText(LOCTEXT("DeleteSliceTooltip", "Delete this slice"))
					.OnClicked_Lambda([this]()
					{
						Window->DeleteSlice(SliceIndex);
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Delete"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				];
		}

		return SNullWidget::NullWidget;
	}

private:
	bool IsValidSlice() const
	{
		return Window && Window->GetSlices().IsValidIndex(SliceIndex);
	}

	SAudioSlicerWindow* Window = nullptr;
	int32 SliceIndex = INDEX_NONE;
};

void SAudioSlicerWindow::Construct(const FArguments& InArgs)
{
	Session.Reset(NewObject<UAudioSlicerSession>(GetTransientPackage(), NAME_None, RF_Transactional));

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 6.f, 6.f, 4.f)
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(6.f, 0.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)

			+ SSplitter::Slot()
			.Value(0.62f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(Waveform, SAudioSlicerWaveform)
					.Session(Session.Get())
					.PlayheadTime_Lambda([this]() { return PlayheadTime; })
					.SelectedSlice_Lambda([this]() { return SelectedSlice; })
					.OnSeek(this, &SAudioSlicerWindow::OnSeek)
					.OnSliceSelected_Lambda([this](int32 SliceIndex) { SelectSlice(SliceIndex); })
					.OnSliceDoubleClicked_Lambda([this](int32 SliceIndex) { PlaySlice(SliceIndex); })
					.OnSlicesChanged_Lambda([this]() { OnSessionChanged(); })
					.OnViewChanged_Lambda([this]() { UpdateScrollBar(); })
					.OnContextMenu(this, &SAudioSlicerWindow::MakeContextMenu)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ScrollBar, SScrollBar)
					.Orientation(Orient_Horizontal)
					.AlwaysShowScrollbar(true)
					.OnUserScrolled(this, &SAudioSlicerWindow::OnUserScrolled)
				]
			]

			+ SSplitter::Slot()
			.Value(0.38f)
			[
				BuildSliceList()
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 4.f, 6.f, 6.f)
		[
			BuildExportBar()
		]
	];

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ReimportHandle = ImportSubsystem->OnAssetReimport.AddSP(this, &SAudioSlicerWindow::OnAssetReimported);
		}
	}

	OnSessionChanged();
}

SAudioSlicerWindow::~SAudioSlicerWindow()
{
	PausePlayback();

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetReimport.Remove(ReimportHandle);
		}
	}
}

TSharedRef<SWidget> SAudioSlicerWindow::BuildToolbar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SoundLabel", "Sound"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(320.f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(USoundWave::StaticClass())
				.ObjectPath_Lambda([this]()
				{
					return Session->Sound ? Session->Sound->GetPathName() : FString();
				})
				.OnObjectChanged(this, &SAudioSlicerWindow::OnSoundPicked)
				.AllowClear(false)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.DisplayThumbnail(false)
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.f, 2.f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsFocusable(false)
			.ToolTipText(LOCTEXT("PlayTooltip", "Play or pause (Space)"))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked_Lambda([this]()
			{
				TogglePlayback();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image_Lambda([this]()
				{
					return FAppStyle::GetBrush(bIsPlaying ? "Animation.Pause" : "Animation.Forward");
				})
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsFocusable(false)
			.ToolTipText(LOCTEXT("StopTooltip", "Stop and jump back to where playback started"))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked_Lambda([this]()
			{
				StopPlayback();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Animation.Stop"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.f, 0.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
			.Text_Lambda([this]()
			{
				using namespace AudioSlicerWindow;
				return FText::FromString(FormatTime(PlayheadTime) + TEXT(" / ") + FormatTime(GetDuration()));
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.f, 2.f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("DetectSlices", "Detect Slices"))
			.ToolTipText(LOCTEXT("DetectSlicesTooltip", "Split the sound at its silent parts. Threshold and timings are under Options."))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked(this, &SAudioSlicerWindow::OnDetectSlices)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ZoomToFit", "Fit"))
			.ToolTipText(LOCTEXT("ZoomToFitTooltip", "Show the whole sound (F)"))
			.IsEnabled_Lambda([this]() { return PCM.IsValid(); })
			.OnClicked_Lambda([this]()
			{
				Waveform->ZoomToFit();
				return FReply::Handled();
			})
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNullWidget::NullWidget
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("OptionsTooltip", "Naming, fades and silence detection"))
			.OnGetMenuContent(this, &SAudioSlicerWindow::MakeOptionsMenu)
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Settings"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Options", "Options"))
				]
			]
		];
}

TSharedRef<SWidget> SAudioSlicerWindow::BuildSliceList()
{
	using namespace AudioSlicerWindow;

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SAssignNew(SliceList, SListView<FSliceItem>)
				.ListItemsSource(&SliceItems)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SAudioSlicerWindow::MakeSliceRow)
				.OnSelectionChanged(this, &SAudioSlicerWindow::OnListSelectionChanged)
				.OnMouseButtonDoubleClick_Lambda([this](FSliceItem Item)
				{
					if (Item.IsValid())
					{
						PlaySlice(*Item);
					}
				})
				.HeaderRow
				(
					SNew(SHeaderRow)

					+ SHeaderRow::Column(IndexColumn)
					.DefaultLabel(LOCTEXT("ColumnIndex", "#"))
					.FixedWidth(40.f)

					+ SHeaderRow::Column(NameColumn)
					.DefaultLabel(LOCTEXT("ColumnName", "Name"))
					.FillWidth(1.f)

					+ SHeaderRow::Column(StartColumn)
					.DefaultLabel(LOCTEXT("ColumnStart", "Start (s)"))
					.FixedWidth(100.f)

					+ SHeaderRow::Column(EndColumn)
					.DefaultLabel(LOCTEXT("ColumnEnd", "End (s)"))
					.FixedWidth(100.f)

					+ SHeaderRow::Column(LengthColumn)
					.DefaultLabel(LOCTEXT("ColumnLength", "Length"))
					.FixedWidth(76.f)

					+ SHeaderRow::Column(ActionsColumn)
					.DefaultLabel(FText::GetEmpty())
					.FixedWidth(60.f)
				)
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(16.f, 32.f, 16.f, 16.f)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]()
				{
					return Session->Slices.Num() == 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("NoSlicesHint", "Drag across the waveform to add a slice, or let Detect Slices do it for you.\nScroll to zoom, Shift+scroll or middle mouse to pan, double-click a slice to hear it."))
			]
		];
}

TSharedRef<SWidget> SAudioSlicerWindow::BuildExportBar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BaseNameLabel", "Base Name"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(200.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(BaseName); })
				.HintText(LOCTEXT("BaseNameHint", "e.g. VO_Hero_Greeting"))
				.ToolTipText(LOCTEXT("BaseNameTooltip", "Slices without a name of their own are named after this, see the naming pattern under Options."))
				.OnTextChanged_Lambda([this](const FText& NewText) { BaseName = NewText.ToString().TrimStartAndEnd(); })
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(12.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FolderLabel", "Folder"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(280.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(OutputFolder); })
				.HintText(LOCTEXT("FolderHint", "/Game/Audio"))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					OutputFolder = NewText.ToString().TrimStartAndEnd();
					OutputFolder.RemoveFromEnd(TEXT("/"));
				})
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 0.f)
		[
			SAssignNew(FolderPickerButton, SComboButton)
			.ToolTipText(LOCTEXT("PickFolderTooltip", "Pick a folder"))
			.HasDownArrow(false)
			.OnGetMenuContent(this, &SAudioSlicerWindow::MakeFolderPicker)
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.FolderClosed"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(12.f, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &SAudioSlicerWindow::GetExportPreview)
			.ColorAndOpacity_Lambda([this]()
			{
				return CanExport() || Session->Slices.Num() == 0 ? FSlateColor::UseSubduedForeground() : FSlateColor(FLinearColor(1.f, 0.6f, 0.3f));
			})
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.IsEnabled_Lambda([this]() { return CanExport(); })
			.OnClicked(this, &SAudioSlicerWindow::OnExport)
			.Text_Lambda([this]()
			{
				return FText::Format(LOCTEXT("ExportButton", "Export {0} {0}|plural(one=Slice,other=Slices)"), Session->Slices.Num());
			})
		];
}

TSharedRef<SWidget> SAudioSlicerWindow::MakeOptionsMenu()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	TSharedRef<IDetailsView> DetailsView = PropertyEditor.CreateDetailView(Args);
	DetailsView->SetObject(GetMutableDefault<UAudioSlicerSettings>());
	DetailsView->OnFinishedChangingProperties().AddLambda([](const FPropertyChangedEvent&)
	{
		GetMutableDefault<UAudioSlicerSettings>()->SaveConfig();
	});

	return SNew(SBox)
		.WidthOverride(440.f)
		.MaxDesiredHeight(560.f)
		[
			DetailsView
		];
}

TSharedRef<SWidget> SAudioSlicerWindow::MakeFolderPicker()
{
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FPathPickerConfig Config;
	Config.DefaultPath = OutputFolder;
	Config.OnPathSelected = FOnPathSelected::CreateSP(this, &SAudioSlicerWindow::OnFolderPicked);

	return SNew(SBox)
		.WidthOverride(300.f)
		.HeightOverride(400.f)
		[
			ContentBrowser.Get().CreatePathPicker(Config)
		];
}

TSharedPtr<SWidget> SAudioSlicerWindow::MakeContextMenu(int32 SliceIndex, float Time)
{
	FMenuBuilder Menu(true, nullptr);

	if (Session->Slices.IsValidIndex(SliceIndex))
	{
		Menu.BeginSection(TEXT("Slice"), LOCTEXT("SliceSection", "Slice"));

		Menu.AddMenuEntry(
			LOCTEXT("PlaySlice", "Play Slice"),
			FText::GetEmpty(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Animation.Forward"),
			FUIAction(FExecuteAction::CreateSP(this, &SAudioSlicerWindow::PlaySlice, SliceIndex)));

		const FAudioSliceRange& Slice = Session->Slices[SliceIndex];
		const bool bCanSplit = Time - Slice.StartTime >= AudioSlicerWindow::MinSliceLength && Slice.EndTime - Time >= AudioSlicerWindow::MinSliceLength;

		Menu.AddMenuEntry(
			LOCTEXT("SplitSlice", "Split Here"),
			LOCTEXT("SplitSliceTooltip", "Cut this slice in two at the mouse position"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAudioSlicerWindow::SplitSliceAt, SliceIndex, Time),
				FCanExecuteAction::CreateLambda([bCanSplit]() { return bCanSplit; })));

		Menu.AddMenuEntry(
			LOCTEXT("ZoomToSlice", "Zoom to Slice"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAudioSlicerWindow::ZoomToSlice, SliceIndex)));

		Menu.AddMenuEntry(
			LOCTEXT("DeleteSlice", "Delete Slice"),
			FText::GetEmpty(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			FUIAction(FExecuteAction::CreateSP(this, &SAudioSlicerWindow::DeleteSlice, SliceIndex)));

		Menu.EndSection();
	}

	Menu.BeginSection(TEXT("View"), LOCTEXT("ViewSection", "View"));

	Menu.AddMenuEntry(
		LOCTEXT("PlayFromHere", "Play From Here"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SAudioSlicerWindow::PlayFromTime, Time)));

	Menu.AddMenuEntry(
		LOCTEXT("ZoomToFitMenu", "Zoom to Fit"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(Waveform.ToSharedRef(), &SAudioSlicerWaveform::ZoomToFit)));

	Menu.EndSection();

	return Menu.MakeWidget();
}

TSharedRef<ITableRow> SAudioSlicerWindow::MakeSliceRow(FSliceItem Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAudioSliceRow, OwnerTable)
		.Window(this)
		.SliceIndex(Item.IsValid() ? *Item : INDEX_NONE);
}

void SAudioSlicerWindow::SetSound(USoundWave* Sound)
{
	if (Sound == Session->Sound)
	{
		return;
	}

	{
		// Part of the undo history, so picking the wrong sound by accident doesn't cost the slices
		FScopedTransaction Transaction(LOCTEXT("ChangeSound", "Change Sound"));
		Session->Modify();
		Session->Sound = Sound;
		Session->Slices.Reset();
	}

	SelectedSlice = INDEX_NONE;
	OnSessionChanged();
}

FReply SAudioSlicerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bNoModifiers = !InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown();

	if (InKeyEvent.IsControlDown() && GEditor)
	{
		if (Key == EKeys::Z && !InKeyEvent.IsShiftDown())
		{
			GEditor->UndoTransaction();
			return FReply::Handled();
		}
		if (Key == EKeys::Y || (Key == EKeys::Z && InKeyEvent.IsShiftDown()))
		{
			GEditor->RedoTransaction();
			return FReply::Handled();
		}
	}

	if (!bNoModifiers)
	{
		return FReply::Unhandled();
	}

	if (Key == EKeys::SpaceBar)
	{
		TogglePlayback();
		return FReply::Handled();
	}
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		DeleteSlice(SelectedSlice);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		Waveform->CancelDrag();
		return FReply::Handled();
	}
	if (Key == EKeys::F)
	{
		if (Session->Slices.IsValidIndex(SelectedSlice))
		{
			ZoomToSlice(SelectedSlice);
		}
		else
		{
			Waveform->ZoomToFit();
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAudioSlicerWindow::PostUndo(bool bSuccess)
{
	OnSessionChanged();
}

void SAudioSlicerWindow::PostRedo(bool bSuccess)
{
	OnSessionChanged();
}

const TArray<FAudioSliceRange>& SAudioSlicerWindow::GetSlices() const
{
	return Session->Slices;
}

FString SAudioSlicerWindow::GetAutoName(int32 SliceIndex) const
{
	return AudioSlicer::GetSliceAssetName(FAudioSliceRange(), BaseName, SliceIndex + 1);
}

float SAudioSlicerWindow::GetDuration() const
{
	return PCM.IsValid() ? PCM->GetDuration() : 0.f;
}

void SAudioSlicerWindow::RenameSlice(int32 SliceIndex, const FString& NewName)
{
	if (!Session->Slices.IsValidIndex(SliceIndex))
	{
		return;
	}

	const FString Trimmed = NewName.TrimStartAndEnd();
	if (Trimmed == Session->Slices[SliceIndex].Name)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RenameSlice", "Rename Slice"));
	Session->Modify();
	Session->Slices[SliceIndex].Name = Trimmed;
	OnSessionChanged();
}

void SAudioSlicerWindow::SetSliceTime(int32 SliceIndex, bool bStart, float Time)
{
	using namespace AudioSlicerWindow;

	if (!Session->Slices.IsValidIndex(SliceIndex))
	{
		return;
	}

	Time = FMath::Clamp(Time, 0.f, GetDuration());

	FScopedTransaction Transaction(bStart ? LOCTEXT("SetSliceStart", "Set Slice Start") : LOCTEXT("SetSliceEnd", "Set Slice End"));
	Session->Modify();

	FAudioSliceRange& Slice = Session->Slices[SliceIndex];
	if (bStart)
	{
		Slice.StartTime = FMath::Clamp(Time, 0.f, FMath::Max(0.f, Slice.EndTime - MinSliceLength));
	}
	else
	{
		Slice.EndTime = FMath::Clamp(Time, FMath::Min(GetDuration(), Slice.StartTime + MinSliceLength), GetDuration());
	}

	const int32 NewIndex = Session->SortSlices(SliceIndex);
	OnSessionChanged();
	SelectSlice(NewIndex);
}

void SAudioSlicerWindow::DeleteSlice(int32 SliceIndex)
{
	if (!Session->Slices.IsValidIndex(SliceIndex))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("DeleteSliceTransaction", "Delete Slice"));
	Session->Modify();
	Session->Slices.RemoveAt(SliceIndex);

	// Keep a selection so Delete can be pressed repeatedly
	SelectedSlice = Session->Slices.Num() > 0 ? FMath::Min(SliceIndex, Session->Slices.Num() - 1) : INDEX_NONE;
	OnSessionChanged();
}

void SAudioSlicerWindow::PlaySlice(int32 SliceIndex)
{
	if (Session->Slices.IsValidIndex(SliceIndex))
	{
		const FAudioSliceRange& Slice = Session->Slices[SliceIndex];
		StartPlayback(Slice.StartTime, Slice.EndTime);
	}
}

void SAudioSlicerWindow::OnSessionChanged()
{
	if (Session->Sound != LoadedSound.Get())
	{
		ReloadAudio(true);
	}

	// Items are only indices. Reusing them keeps the rows alive, so a text box that is
	// being edited doesn't vanish because some other slice changed.
	const int32 NumSlices = Session->Slices.Num();
	while (SliceItems.Num() < NumSlices)
	{
		SliceItems.Add(MakeShared<int32>(SliceItems.Num()));
	}
	SliceItems.SetNum(NumSlices);

	if (!Session->Slices.IsValidIndex(SelectedSlice))
	{
		SelectedSlice = INDEX_NONE;
	}

	if (SliceList.IsValid())
	{
		SliceList->RequestListRefresh();
		if (SelectedSlice != INDEX_NONE)
		{
			SliceList->SetSelection(SliceItems[SelectedSlice], ESelectInfo::Direct);
		}
		else
		{
			SliceList->ClearSelection();
		}
	}

	if (Waveform.IsValid())
	{
		Waveform->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SAudioSlicerWindow::ReloadAudio(bool bResetNaming)
{
	PausePlayback();

	USoundWave* Sound = Session->Sound;
	LoadedSound = Sound;
	PCM.Reset();

	if (Sound)
	{
		TSharedPtr<FAudioSlicerPCM> NewPCM = MakeShared<FAudioSlicerPCM>();
		if (NewPCM->Load(Sound))
		{
			PCM = NewPCM;
		}
		else
		{
			AudioSlicerWindow::Notify(
				FText::Format(LOCTEXT("LoadFailed", "Couldn't read the audio of {0}. Only imported Sound Waves can be sliced."), FText::FromString(Sound->GetName())),
				SNotificationItem::CS_Fail);
		}
	}

	if (Waveform.IsValid())
	{
		Waveform->SetAudio(PCM);
	}

	PlayheadTime = 0.f;
	PlayFrom = 0.f;

	if (bResetNaming)
	{
		BaseName = Sound ? Sound->GetName() : FString();
		OutputFolder = AudioSlicerWindow::GetDefaultFolder(Sound);
	}

	UpdateScrollBar();
}

void SAudioSlicerWindow::OnAssetReimported(UObject* Object)
{
	// The slices stay, only the audio under them changes
	if (Object && Object == LoadedSound.Get())
	{
		ReloadAudio(false);
	}
}

void SAudioSlicerWindow::OnSoundPicked(const FAssetData& AssetData)
{
	SetSound(Cast<USoundWave>(AssetData.GetAsset()));
}

void SAudioSlicerWindow::OnFolderPicked(const FString& Path)
{
	OutputFolder = Path;
	if (FolderPickerButton.IsValid())
	{
		FolderPickerButton->SetIsOpen(false);
	}
}

void SAudioSlicerWindow::SelectSlice(int32 SliceIndex, bool bFromList)
{
	SelectedSlice = Session->Slices.IsValidIndex(SliceIndex) ? SliceIndex : INDEX_NONE;

	if (!bFromList && SliceList.IsValid() && SliceItems.Num() == Session->Slices.Num())
	{
		if (SelectedSlice != INDEX_NONE)
		{
			SliceList->SetSelection(SliceItems[SelectedSlice], ESelectInfo::Direct);
			SliceList->RequestScrollIntoView(SliceItems[SelectedSlice]);
		}
		else
		{
			SliceList->ClearSelection();
		}
	}

	if (bFromList && SelectedSlice != INDEX_NONE)
	{
		const FAudioSliceRange& Slice = Session->Slices[SelectedSlice];
		Waveform->ScrollIntoView(Slice.StartTime);
		if (!bIsPlaying)
		{
			PlayheadTime = Slice.StartTime;
			PlayFrom = Slice.StartTime;
		}
	}

	Waveform->Invalidate(EInvalidateWidgetReason::Paint);
}

void SAudioSlicerWindow::OnListSelectionChanged(FSliceItem Item, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		SelectSlice(Item.IsValid() ? *Item : INDEX_NONE, true);
	}
}

void SAudioSlicerWindow::SplitSliceAt(int32 SliceIndex, float Time)
{
	if (!Session->Slices.IsValidIndex(SliceIndex))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SplitSliceTransaction", "Split Slice"));
	Session->Modify();

	FAudioSliceRange SecondHalf = Session->Slices[SliceIndex];
	SecondHalf.StartTime = Time;
	SecondHalf.Name.Reset();

	Session->Slices[SliceIndex].EndTime = Time;
	Session->Slices.Insert(SecondHalf, SliceIndex + 1);

	const int32 NewIndex = Session->SortSlices(SliceIndex + 1);
	OnSessionChanged();
	SelectSlice(NewIndex);
}

void SAudioSlicerWindow::ZoomToSlice(int32 SliceIndex)
{
	if (Session->Slices.IsValidIndex(SliceIndex))
	{
		const FAudioSliceRange& Slice = Session->Slices[SliceIndex];
		Waveform->ZoomToRange(Slice.StartTime, Slice.EndTime);
	}
}

void SAudioSlicerWindow::OnSeek(float Time)
{
	if (bIsPlaying)
	{
		StartPlayback(Time, GetDuration());
	}
	else
	{
		PlayheadTime = Time;
		PlayFrom = Time;
	}
}

void SAudioSlicerWindow::PlayFromTime(float Time)
{
	StartPlayback(Time, GetDuration());
}

void SAudioSlicerWindow::TogglePlayback()
{
	if (bIsPlaying)
	{
		PausePlayback();
		return;
	}

	// At the very end, start over instead of playing nothing
	const float From = PlayheadTime >= GetDuration() - 0.001f ? 0.f : PlayheadTime;
	StartPlayback(From, GetDuration());
}

void SAudioSlicerWindow::StartPlayback(float From, float Until)
{
	USoundWave* Sound = Session->Sound;
	if (!Sound || !PCM.IsValid() || !GEditor)
	{
		return;
	}

	PausePlayback();

	// The editor's preview component is the same one the Content Browser uses, so starting
	// a preview there stops ours and the other way round, which is what you'd expect.
	UAudioComponent* Component = GEditor->ResetPreviewAudioComponent(Sound);
	if (!Component)
	{
		return;
	}

	Component->Play(From);
	PreviewComponent = Component;

	bIsPlaying = true;
	PlayFrom = From;
	PlayUntil = Until;
	PlayheadTime = From;
	PlayStartedAt = FPlatformTime::Seconds();

	if (!PlaybackTimer.IsValid())
	{
		PlaybackTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAudioSlicerWindow::TickPlayback));
	}
}

void SAudioSlicerWindow::PausePlayback()
{
	if (!bIsPlaying)
	{
		return;
	}

	if (UAudioComponent* Component = PreviewComponent.Get())
	{
		Component->Stop();
	}
	bIsPlaying = false;
}

void SAudioSlicerWindow::StopPlayback()
{
	if (bIsPlaying)
	{
		PausePlayback();
		PlayheadTime = PlayFrom;
	}
	else
	{
		PlayheadTime = 0.f;
		PlayFrom = 0.f;
	}
}

EActiveTimerReturnType SAudioSlicerWindow::TickPlayback(double CurrentTime, float DeltaTime)
{
	if (!bIsPlaying)
	{
		PlaybackTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	PlayheadTime = PlayFrom + float(FPlatformTime::Seconds() - PlayStartedAt);

	const UAudioComponent* Component = PreviewComponent.Get();
	if (PlayheadTime >= PlayUntil || !Component || !Component->IsPlaying())
	{
		PausePlayback();
		PlayheadTime = PlayFrom;
		PlaybackTimer.Reset();
		Waveform->Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Stop;
	}

	Waveform->ScrollIntoView(PlayheadTime);
	Waveform->Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

FReply SAudioSlicerWindow::OnDetectSlices()
{
	if (!PCM.IsValid())
	{
		return FReply::Handled();
	}

	const FAudioSilenceDetectionSettings& Detection = GetDefault<UAudioSlicerSettings>()->Detection;
	TArray<FAudioSliceRange> Found = PCM->DetectSoundRegions(Detection);

	if (Found.Num() == 0)
	{
		AudioSlicerWindow::Notify(
			FText::Format(LOCTEXT("NothingDetected", "Nothing in this sound is louder than {0} dB. Try a lower threshold under Options."), FText::AsNumber(Detection.ThresholdDb)),
			SNotificationItem::CS_None);
		return FReply::Handled();
	}

	if (Session->Slices.Num() > 0)
	{
		const FText Question = FText::Format(
			LOCTEXT("ReplaceSlices", "Replace the {0} existing {0}|plural(one=slice,other=slices) with {1} detected ones?\n\nCtrl+Z brings them back."),
			Session->Slices.Num(), Found.Num());

		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
	}

	FScopedTransaction Transaction(LOCTEXT("DetectSlicesTransaction", "Detect Slices"));
	Session->Modify();
	Session->Slices = MoveTemp(Found);

	SelectedSlice = INDEX_NONE;
	OnSessionChanged();

	return FReply::Handled();
}

FReply SAudioSlicerWindow::OnExport()
{
	FText Problem;
	if (!CanExport(&Problem))
	{
		AudioSlicerWindow::Notify(Problem, SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	// Resolve the names here so the checks below see exactly what will be written
	TArray<FAudioSliceRange> Slices = Session->Slices;
	TSet<FName> UsedNames;
	int32 NumExisting = 0;

	for (int32 Index = 0; Index < Slices.Num(); ++Index)
	{
		Slices[Index].Name = AudioSlicer::GetSliceAssetName(Slices[Index], BaseName, Index + 1);

		if (UsedNames.Contains(FName(*Slices[Index].Name)))
		{
			AudioSlicerWindow::Notify(
				FText::Format(LOCTEXT("DuplicateName", "Two slices would both be called {0}. Rename one of them."), FText::FromString(Slices[Index].Name)),
				SNotificationItem::CS_Fail);
			SelectSlice(Index);
			return FReply::Handled();
		}
		UsedNames.Add(FName(*Slices[Index].Name));

		if (AudioSlicer::FindExistingAsset(OutputFolder, Slices[Index].Name).IsValid())
		{
			++NumExisting;
		}
	}

	bool bOverwrite = false;
	if (NumExisting > 0)
	{
		const FText Question = FText::Format(
			LOCTEXT("OverwritePrompt", "{0} of these assets already exist in {1}.\n\nYes: overwrite them. Anything that uses them keeps working.\nNo: keep them and give the new slices unique names."),
			NumExisting, FText::FromString(OutputFolder));

		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNoCancel, Question);
		if (Answer == EAppReturnType::Cancel)
		{
			return FReply::Handled();
		}
		bOverwrite = Answer == EAppReturnType::Yes;
	}

	PausePlayback();

	FAudioSliceExportOptions Options = GetDefault<UAudioSlicerSettings>()->MakeExportOptions(OutputFolder);
	Options.bOverwriteExisting = bOverwrite;

	const TArray<USoundWave*> Exported = AudioSlicer::ExportSlices(*PCM, Session->Sound, BaseName, Slices, Options);

	if (Exported.Num() == 0)
	{
		AudioSlicerWindow::Notify(LOCTEXT("ExportFailed", "Nothing was exported. The Output Log has the details."), SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	TArray<TWeakObjectPtr<UObject>> ExportedAssets;
	for (USoundWave* Wave : Exported)
	{
		ExportedAssets.Add(Wave);
	}

	FNotificationInfo Info(Exported.Num() == Slices.Num()
		? FText::Format(LOCTEXT("ExportDone", "Exported {0} {0}|plural(one=slice,other=slices) to {1}"), Exported.Num(), FText::FromString(OutputFolder))
		: FText::Format(LOCTEXT("ExportPartial", "Exported {0} of {1} slices. The Output Log says what went wrong with the rest."), Exported.Num(), Slices.Num()));
	Info.ExpireDuration = 8.f;
	Info.HyperlinkText = LOCTEXT("ShowInContentBrowser", "Show in Content Browser");
	Info.Hyperlink = FSimpleDelegate::CreateLambda([ExportedAssets]()
	{
		TArray<UObject*> Assets;
		for (const TWeakObjectPtr<UObject>& Asset : ExportedAssets)
		{
			if (Asset.IsValid())
			{
				Assets.Add(Asset.Get());
			}
		}

		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().SyncBrowserToAssets(Assets);
	});

	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(Exported.Num() == Slices.Num() ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	return FReply::Handled();
}

bool SAudioSlicerWindow::CanExport(FText* OutReason) const
{
	auto Fail = [OutReason](const FText& Reason)
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};

	if (!Session->Sound || !PCM.IsValid())
	{
		return Fail(LOCTEXT("NoSound", "Pick a sound first."));
	}
	if (Session->Slices.Num() == 0)
	{
		return Fail(LOCTEXT("NoSlices", "Add a slice first."));
	}
	if (BaseName.IsEmpty() && Session->Slices.ContainsByPredicate([](const FAudioSliceRange& Slice) { return Slice.Name.TrimStartAndEnd().IsEmpty(); }))
	{
		return Fail(LOCTEXT("NoBaseName", "Enter a base name, or give every slice a name."));
	}
	if (!AudioSlicer::IsValidContentFolder(OutputFolder))
	{
		return Fail(LOCTEXT("BadFolder", "The folder has to be a content folder, like /Game/Audio."));
	}
	return true;
}

FText SAudioSlicerWindow::GetExportPreview() const
{
	FText Problem;
	if (!CanExport(&Problem))
	{
		return Session->Slices.Num() > 0 ? Problem : FText::GetEmpty();
	}

	const int32 NumSlices = Session->Slices.Num();
	const FText First = FText::FromString(AudioSlicer::GetSliceAssetName(Session->Slices[0], BaseName, 1));
	const FText Folder = FText::FromString(OutputFolder);

	if (NumSlices == 1)
	{
		return FText::Format(LOCTEXT("PreviewSingle", "{0} in {1}"), First, Folder);
	}

	const FText Last = FText::FromString(AudioSlicer::GetSliceAssetName(Session->Slices.Last(), BaseName, NumSlices));
	return FText::Format(LOCTEXT("PreviewRange", "{0} to {1} in {2}"), First, Last, Folder);
}

void SAudioSlicerWindow::UpdateScrollBar()
{
	if (!ScrollBar.IsValid() || !Waveform.IsValid())
	{
		return;
	}

	const float Total = Waveform->GetTotalDuration();
	if (Total <= 0.f)
	{
		ScrollBar->SetState(0.f, 1.f);
		return;
	}

	ScrollBar->SetState(Waveform->GetViewStart() / Total, Waveform->GetViewDuration() / Total);
}

void SAudioSlicerWindow::OnUserScrolled(float ScrollOffset)
{
	Waveform->SetViewStart(ScrollOffset * Waveform->GetTotalDuration());
}

#undef LOCTEXT_NAMESPACE

// Copyright 2026 Emre Erdogan.

#include "SAudioSlicerWindow.h"
#include "AudioSlicerWindowUtils.h"
#include "AudioSlicerCore.h"
#include "SAudioSlicerWaveform.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SOverlay.h"
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

#undef LOCTEXT_NAMESPACE

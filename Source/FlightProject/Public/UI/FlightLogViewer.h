// FlightLogViewer.h
// Interactive log viewer widget with filtering and visualization
// Uses FlightSlate declarative patterns and FlightReactiveUI bindings

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "FlightLogCapture.h"
#include "FlightSlate.h"

namespace Flight::Log
{
    // ============================================================================
    // Log Entry Row Widget
    // ============================================================================

    class SLogEntryRow : public SMultiColumnTableRow<TSharedPtr<FLogEntry>>
    {
    public:
        SLATE_BEGIN_ARGS(SLogEntryRow) {}
            SLATE_ARGUMENT(TSharedPtr<FLogEntry>, Entry)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
        {
            Entry = InArgs._Entry;
            SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), InOwnerTable);
        }

        virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
        {
            if (!Entry.IsValid())
            {
                return SNullWidget::NullWidget;
            }

            const FSlateColor TextColor = FSlateColor(VerbosityColor(Entry->Verbosity));

            if (ColumnName == TEXT("Time"))
            {
                return SNew(STextBlock)
                    .Text(FText::FromString(Entry->FormatTimestamp()))
                    .ColorAndOpacity(TextColor)
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9));
            }
            else if (ColumnName == TEXT("Verbosity"))
            {
                return SNew(SBorder)
                    .BorderBackgroundColor(VerbosityColor(Entry->Verbosity) * 0.3f)
                    .Padding(FMargin(4, 2))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(VerbosityName(Entry->Verbosity)))
                        .ColorAndOpacity(TextColor)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    ];
            }
            else if (ColumnName == TEXT("Category"))
            {
                return SNew(STextBlock)
                    .Text(FText::FromName(Entry->Category))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 1.0f)))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
            }
            else if (ColumnName == TEXT("Message"))
            {
                return SNew(STextBlock)
                    .Text(FText::FromString(Entry->Message))
                    .ColorAndOpacity(TextColor)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .AutoWrapText(false);
            }
            else if (ColumnName == TEXT("Frame"))
            {
                return SNew(STextBlock)
                    .Text(FText::AsNumber(Entry->FrameNumber))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9));
            }

            return SNullWidget::NullWidget;
        }

    private:
        TSharedPtr<FLogEntry> Entry;
    };

    // ============================================================================
    // Verbosity Filter Button
    // ============================================================================

    class SVerbosityFilterButton : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SVerbosityFilterButton) {}
            SLATE_ARGUMENT(EVerbosity, Verbosity)
            SLATE_ARGUMENT(bool, bInitiallyEnabled)
            SLATE_EVENT(FSimpleDelegate, OnToggled)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            Verbosity = InArgs._Verbosity;
            bEnabled = InArgs._bInitiallyEnabled;
            OnToggled = InArgs._OnToggled;

            ChildSlot
            [
                SNew(SCheckBox)
                .IsChecked(bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged(this, &SVerbosityFilterButton::OnCheckChanged)
                .Style(&FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox"))
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(VerbosityColor(Verbosity) * (bEnabled ? 0.8f : 0.2f))
                    .Padding(FMargin(6, 2))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(VerbosityName(Verbosity)))
                        .ColorAndOpacity(FSlateColor(VerbosityColor(Verbosity)))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                    ]
                ]
            ];
        }

        bool IsEnabled() const { return bEnabled; }

    private:
        void OnCheckChanged(ECheckBoxState NewState)
        {
            bEnabled = (NewState == ECheckBoxState::Checked);
            OnToggled.ExecuteIfBound();
        }

        EVerbosity Verbosity;
        bool bEnabled;
        FSimpleDelegate OnToggled;
    };

    // ============================================================================
    // Main Log Viewer Widget
    // ============================================================================

    class SLogViewer : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SLogViewer) {}
            SLATE_ARGUMENT(TSharedPtr<FReactiveLogStream>, LogStream)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            if (InArgs._LogStream.IsValid())
            {
                LogStream = InArgs._LogStream;
            }
            else
            {
                // Use global log capture
                LogStream = MakeShared<FReactiveLogStream>(FGlobalLogCapture::Get());
            }

            // Subscribe to filtered log changes
            LogStream->FilteredLogs().Subscribe(
                [](const TArray<FLogEntry>& OldLogs, const TArray<FLogEntry>& NewLogs, void* UserData)
                {
                    static_cast<SLogViewer*>(UserData)->OnLogsChanged(NewLogs);
                },
                this
            );

            // Build column header
            HeaderRow = SNew(SHeaderRow)
                + SHeaderRow::Column(TEXT("Time"))
                    .DefaultLabel(FText::FromString(TEXT("Time")))
                    .FixedWidth(100)
                + SHeaderRow::Column(TEXT("Verbosity"))
                    .DefaultLabel(FText::FromString(TEXT("Level")))
                    .FixedWidth(80)
                + SHeaderRow::Column(TEXT("Category"))
                    .DefaultLabel(FText::FromString(TEXT("Category")))
                    .FixedWidth(120)
                + SHeaderRow::Column(TEXT("Frame"))
                    .DefaultLabel(FText::FromString(TEXT("Frame")))
                    .FixedWidth(60)
                + SHeaderRow::Column(TEXT("Message"))
                    .DefaultLabel(FText::FromString(TEXT("Message")))
                    .FillWidth(1.0f);

            ChildSlot
            [
                SNew(SVerticalBox)

                // Toolbar
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(4)
                [
                    BuildToolbar()
                ]

                // Stats bar
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(4, 0)
                [
                    BuildStatsBar()
                ]

                // Main content - splitter with log list and detail panel
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SSplitter)
                    .Orientation(Orient_Vertical)

                    + SSplitter::Slot()
                    .Value(0.7f)
                    [
                        SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
                        [
                            SAssignNew(ListView, SListView<TSharedPtr<FLogEntry>>)
                            .ListItemsSource(&DisplayedLogs)
                            .OnGenerateRow(this, &SLogViewer::OnGenerateRow)
                            .OnSelectionChanged(this, &SLogViewer::OnSelectionChanged)
                            .HeaderRow(HeaderRow)
                            .SelectionMode(ESelectionMode::Single)
                        ]
                    ]

                    + SSplitter::Slot()
                    .Value(0.3f)
                    [
                        BuildDetailPanel()
                    ]
                ]
            ];

            // Initial refresh
            RefreshDisplayedLogs();
        }

    private:
        TSharedRef<SWidget> BuildToolbar()
        {
            return SNew(SHorizontalBox)

                // Search box
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(0, 0, 8, 0)
                [
                    SAssignNew(SearchBox, SSearchBox)
                    .HintText(FText::FromString(TEXT("Filter logs...")))
                    .OnTextChanged(this, &SLogViewer::OnSearchTextChanged)
                ]

                // Verbosity filters
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(4, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                    [
                        SAssignNew(FilterFatal, SVerbosityFilterButton)
                        .Verbosity(EVerbosity::Fatal)
                        .bInitiallyEnabled(true)
                        .OnToggled(FSimpleDelegate::CreateSP(this, &SLogViewer::OnFilterChanged))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                    [
                        SAssignNew(FilterError, SVerbosityFilterButton)
                        .Verbosity(EVerbosity::Error)
                        .bInitiallyEnabled(true)
                        .OnToggled(FSimpleDelegate::CreateSP(this, &SLogViewer::OnFilterChanged))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                    [
                        SAssignNew(FilterWarning, SVerbosityFilterButton)
                        .Verbosity(EVerbosity::Warning)
                        .bInitiallyEnabled(true)
                        .OnToggled(FSimpleDelegate::CreateSP(this, &SLogViewer::OnFilterChanged))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                    [
                        SAssignNew(FilterLog, SVerbosityFilterButton)
                        .Verbosity(EVerbosity::Log)
                        .bInitiallyEnabled(true)
                        .OnToggled(FSimpleDelegate::CreateSP(this, &SLogViewer::OnFilterChanged))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                    [
                        SAssignNew(FilterVerbose, SVerbosityFilterButton)
                        .Verbosity(EVerbosity::Verbose)
                        .bInitiallyEnabled(false)
                        .OnToggled(FSimpleDelegate::CreateSP(this, &SLogViewer::OnFilterChanged))
                    ]
                ]

                // Clear button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Clear")))
                    .OnClicked(this, &SLogViewer::OnClearClicked)
                ]

                // Auto-scroll toggle
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(4, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked(bAutoScroll ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged(this, &SLogViewer::OnAutoScrollChanged)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Auto-scroll")))
                    ]
                ];
        }

        TSharedRef<SWidget> BuildStatsBar()
        {
            return SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 2)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        return FText::Format(
                            FText::FromString(TEXT("Total: {0}")),
                            FText::AsNumber(LogStream->TotalCount().Get()));
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 2)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        return FText::Format(
                            FText::FromString(TEXT("Errors: {0}")),
                            FText::AsNumber(LogStream->ErrorCount().Get()));
                    })
                    .ColorAndOpacity(FSlateColor(VerbosityColor(EVerbosity::Error)))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 2)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        return FText::Format(
                            FText::FromString(TEXT("Warnings: {0}")),
                            FText::AsNumber(LogStream->WarningCount().Get()));
                    })
                    .ColorAndOpacity(FSlateColor(VerbosityColor(EVerbosity::Warning)))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 2)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        return FText::Format(
                            FText::FromString(TEXT("Showing: {0}")),
                            FText::AsNumber(DisplayedLogs.Num()));
                    })
                ];
        }

        TSharedRef<SWidget> BuildDetailPanel()
        {
            return SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
                .Padding(8)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SAssignNew(DetailText, STextBlock)
                        .Text(FText::FromString(TEXT("Select a log entry to view details")))
                        .AutoWrapText(true)
                        .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                    ]
                ];
        }

        TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FLogEntry> Entry,
                                            const TSharedRef<STableViewBase>& OwnerTable)
        {
            return SNew(SLogEntryRow, OwnerTable)
                .Entry(Entry);
        }

        void OnSelectionChanged(TSharedPtr<FLogEntry> Selected, ESelectInfo::Type SelectInfo)
        {
            if (Selected.IsValid() && DetailText.IsValid())
            {
                FString Details = FString::Printf(
                    TEXT("ID: %llu\n")
                    TEXT("Time: %s\n")
                    TEXT("Frame: %u\n")
                    TEXT("Thread: %u\n")
                    TEXT("Verbosity: %s\n")
                    TEXT("Category: %s\n")
                    TEXT("\n--- Message ---\n%s"),
                    Selected->Id,
                    *Selected->FormatTimestamp(),
                    Selected->FrameNumber,
                    Selected->ThreadId,
                    VerbosityName(Selected->Verbosity),
                    *Selected->Category.ToString(),
                    *Selected->Message);

                if (!Selected->File.IsEmpty())
                {
                    Details += FString::Printf(TEXT("\n\n--- Source ---\n%s:%d"),
                        *Selected->File, Selected->Line);
                }

                DetailText->SetText(FText::FromString(Details));
            }
        }

        void OnSearchTextChanged(const FText& NewText)
        {
            LogStream->SetSearchText(NewText.ToString());
        }

        void OnFilterChanged()
        {
            UpdateFilterFromUI();
        }

        void UpdateFilterFromUI()
        {
            FLogFilter Filter = LogStream->GetFilter();

            // Build verbosity filter based on which buttons are enabled
            // Set MinLevel to the most verbose enabled level
            EVerbosity MinLevel = EVerbosity::Fatal;

            if (FilterVerbose.IsValid() && FilterVerbose->IsEnabled())
                MinLevel = EVerbosity::Verbose;
            else if (FilterLog.IsValid() && FilterLog->IsEnabled())
                MinLevel = EVerbosity::Log;
            else if (FilterWarning.IsValid() && FilterWarning->IsEnabled())
                MinLevel = EVerbosity::Warning;
            else if (FilterError.IsValid() && FilterError->IsEnabled())
                MinLevel = EVerbosity::Error;
            else if (FilterFatal.IsValid() && FilterFatal->IsEnabled())
                MinLevel = EVerbosity::Fatal;

            Filter.Verbosity.MinLevel = MinLevel;
            LogStream->SetFilter(Filter);
        }

        FReply OnClearClicked()
        {
            FGlobalLogCapture::Get().Clear();
            DisplayedLogs.Empty();
            if (ListView.IsValid())
            {
                ListView->RequestListRefresh();
            }
            return FReply::Handled();
        }

        void OnAutoScrollChanged(ECheckBoxState NewState)
        {
            bAutoScroll = (NewState == ECheckBoxState::Checked);
        }

        void OnLogsChanged(const TArray<FLogEntry>& NewLogs)
        {
            RefreshDisplayedLogs();
        }

        void RefreshDisplayedLogs()
        {
            const auto& Logs = LogStream->FilteredLogs().Get();

            DisplayedLogs.Empty(Logs.Num());
            for (const auto& Entry : Logs)
            {
                DisplayedLogs.Add(MakeShared<FLogEntry>(Entry));
            }

            if (ListView.IsValid())
            {
                ListView->RequestListRefresh();

                if (bAutoScroll && DisplayedLogs.Num() > 0)
                {
                    ListView->RequestScrollIntoView(DisplayedLogs.Last());
                }
            }
        }

        TSharedPtr<FReactiveLogStream> LogStream;
        TArray<TSharedPtr<FLogEntry>> DisplayedLogs;

        TSharedPtr<SListView<TSharedPtr<FLogEntry>>> ListView;
        TSharedPtr<SHeaderRow> HeaderRow;
        TSharedPtr<SSearchBox> SearchBox;
        TSharedPtr<STextBlock> DetailText;

        TSharedPtr<SVerbosityFilterButton> FilterFatal;
        TSharedPtr<SVerbosityFilterButton> FilterError;
        TSharedPtr<SVerbosityFilterButton> FilterWarning;
        TSharedPtr<SVerbosityFilterButton> FilterLog;
        TSharedPtr<SVerbosityFilterButton> FilterVerbose;

        bool bAutoScroll = true;
    };

    // ============================================================================
    // Compact Log Bar - Minimal status bar style
    // ============================================================================

    class SLogBar : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SLogBar) {}
            SLATE_ARGUMENT(TSharedPtr<FReactiveLogStream>, LogStream)
            SLATE_EVENT(FSimpleDelegate, OnExpandClicked)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            LogStream = InArgs._LogStream.IsValid()
                ? InArgs._LogStream
                : MakeShared<FReactiveLogStream>(FGlobalLogCapture::Get());

            OnExpandClicked = InArgs._OnExpandClicked;

            LogStream->FilteredLogs().Subscribe(
                [](const TArray<FLogEntry>& OldLogs, const TArray<FLogEntry>& NewLogs, void* UserData)
                {
                    static_cast<SLogBar*>(UserData)->OnLogsChanged(NewLogs);
                },
                this
            );

            ChildSlot
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
                .Padding(FMargin(8, 4))
                [
                    SNew(SHorizontalBox)

                    // Last message
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SAssignNew(LastMessageText, STextBlock)
                        .Text(FText::FromString(TEXT("No logs")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
                    ]

                    // Error count
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(8, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() {
                            return FText::Format(
                                FText::FromString(TEXT("E:{0}")),
                                FText::AsNumber(LogStream->ErrorCount().Get()));
                        })
                        .ColorAndOpacity(FSlateColor(VerbosityColor(EVerbosity::Error)))
                    ]

                    // Warning count
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() {
                            return FText::Format(
                                FText::FromString(TEXT("W:{0}")),
                                FText::AsNumber(LogStream->WarningCount().Get()));
                        })
                        .ColorAndOpacity(FSlateColor(VerbosityColor(EVerbosity::Warning)))
                    ]

                    // Expand button
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(8, 0, 0, 0)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Expand")))
                        .OnClicked(this, &SLogBar::HandleExpandClicked)
                    ]
                ]
            ];
        }

    private:
        void OnLogsChanged(const TArray<FLogEntry>& Logs)
        {
            if (Logs.Num() > 0 && LastMessageText.IsValid())
            {
                const FLogEntry& Last = Logs.Last();
                LastMessageText->SetText(FText::FromString(
                    FString::Printf(TEXT("[%s] %s"),
                        *Last.Category.ToString(),
                        *Last.Message.Left(100))));
                LastMessageText->SetColorAndOpacity(FSlateColor(VerbosityColor(Last.Verbosity)));
            }
        }

        FReply HandleExpandClicked()
        {
            OnExpandClicked.ExecuteIfBound();
            return FReply::Handled();
        }

        TSharedPtr<FReactiveLogStream> LogStream;
        TSharedPtr<STextBlock> LastMessageText;
        FSimpleDelegate OnExpandClicked;
    };

} // namespace Flight::Log

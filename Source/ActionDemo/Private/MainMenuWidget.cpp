// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"

#include "ActionDemoGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "HAL/PlatformApplicationMisc.h"

namespace
{
	UTextBlock* MakeLabel(UWidgetTree* Tree, const FString& Text, int32 Size, FLinearColor Color)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(FText::FromString(Text));
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	}

	UButton* MakeButton(UWidgetTree* Tree, const FString& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* LabelBlock = MakeLabel(Tree, Label, 20, FLinearColor::White);
		Button->SetContent(LabelBlock);
		return Button;
	}
}

TSharedRef<SWidget> UMainMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UMainMenuWidget::BuildLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.92f));
	Panel->SetPadding(FMargin(32.f));

	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetAutoSize(true);
	}

	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VBox"));
	Panel->AddChild(VBox);

	auto AddRow = [VBox](UWidget* Child, float BottomPad = 12.f)
	{
		if (UVerticalBoxSlot* Slot = VBox->AddChildToVerticalBox(Child))
		{
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPad));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
	};

	TitleText = MakeLabel(WidgetTree, TEXT("ACTION DEMO"), 36, FLinearColor(0.95f, 0.95f, 0.9f));
	AddRow(TitleText, 8.f);

	UTextBlock* Subtitle = MakeLabel(WidgetTree, TEXT("Main Menu"), 18, FLinearColor(0.65f, 0.7f, 0.75f));
	AddRow(Subtitle, 24.f);

	PlaySingleButton = MakeButton(WidgetTree, TEXT("Play Single Player"));
	AddRow(PlaySingleButton);

	CreateRemoteButton = MakeButton(WidgetTree, TEXT("Create Remote Room"));
	AddRow(CreateRemoteButton);

	UHorizontalBox* MapRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MapRow"));
	UTextBlock* MapLabel = MakeLabel(WidgetTree, TEXT("Map"), 16, FLinearColor(0.75f, 0.8f, 0.85f));
	if (UHorizontalBoxSlot* MapLabelSlot = MapRow->AddChildToHorizontalBox(MapLabel))
	{
		MapLabelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		MapLabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	MapSelector = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("MapSelector"));
	const FSlateColor MapTextColor(FLinearColor::White);
	// ForegroundColor is construction-only and has no public runtime setter in UComboBoxString.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	MapSelector->ForegroundColor = MapTextColor;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FTableRowStyle MapItemStyle = MapSelector->GetItemStyle();
	MapItemStyle.SetTextColor(MapTextColor);
	MapItemStyle.SetSelectedTextColor(MapTextColor);
	MapSelector->SetItemStyle(MapItemStyle);
	if (UHorizontalBoxSlot* MapSelectorSlot = MapRow->AddChildToHorizontalBox(MapSelector))
	{
		MapSelectorSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	AddRow(MapRow);

	UHorizontalBox* ModeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ModeRow"));
	LANModeCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("LANModeCheckBox"));
	if (UHorizontalBoxSlot* CheckSlot = ModeRow->AddChildToHorizontalBox(LANModeCheckBox))
	{
		CheckSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		CheckSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* ModeLabel = MakeLabel(WidgetTree, TEXT("LAN test mode (no Steam)"), 16, FLinearColor(0.75f, 0.8f, 0.85f));
	if (UHorizontalBoxSlot* LabelSlot = ModeRow->AddChildToHorizontalBox(ModeLabel))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(ModeRow);

	UHorizontalBox* CodeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CodeRow"));
	RoomCodeText = MakeLabel(WidgetTree, TEXT("Room code: —"), 16, FLinearColor(0.8f, 0.85f, 0.55f));
	if (UHorizontalBoxSlot* CodeSlot = CodeRow->AddChildToHorizontalBox(RoomCodeText))
	{
		CodeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CodeSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		CodeSlot->SetVerticalAlignment(VAlign_Center);
	}
	CopyRoomCodeButton = MakeButton(WidgetTree, TEXT("Copy"));
	if (UHorizontalBoxSlot* CopySlot = CodeRow->AddChildToHorizontalBox(CopyRoomCodeButton))
	{
		CopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		CopySlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(CodeRow);

	UHorizontalBox* JoinRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("JoinRow"));
	RoomCodeInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomCodeInput"));
	RoomCodeInput->SetHintText(FText::FromString(TEXT("Enter room code")));
	const FSlateColor InputTextColor(FLinearColor::Black);
	RoomCodeInput->WidgetStyle.SetForegroundColor(InputTextColor);
	RoomCodeInput->WidgetStyle.SetFocusedForegroundColor(InputTextColor);
	RoomCodeInput->WidgetStyle.SetReadOnlyForegroundColor(InputTextColor);
	// EditableTextBox's nested TextStyle takes precedence over ForegroundColor.
	RoomCodeInput->WidgetStyle.TextStyle.SetColorAndOpacity(InputTextColor);
	if (UHorizontalBoxSlot* InputSlot = JoinRow->AddChildToHorizontalBox(RoomCodeInput))
	{
		InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		InputSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		InputSlot->SetVerticalAlignment(VAlign_Center);
	}

	JoinRemoteButton = MakeButton(WidgetTree, TEXT("Join Room"));
	if (UHorizontalBoxSlot* JoinSlot = JoinRow->AddChildToHorizontalBox(JoinRemoteButton))
	{
		JoinSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		JoinSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(JoinRow, 20.f);

	StatusText = MakeLabel(WidgetTree, TEXT("Ready."), 14, FLinearColor(0.55f, 0.75f, 0.95f));
	AddRow(StatusText, 0.f);
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PlaySingleButton)
	{
		PlaySingleButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnPlaySinglePlayerClicked);
	}
	if (CreateRemoteButton)
	{
		CreateRemoteButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnCreateRemoteClicked);
	}
	if (JoinRemoteButton)
	{
		JoinRemoteButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnJoinRemoteClicked);
	}
	if (CopyRoomCodeButton)
	{
		CopyRoomCodeButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnCopyRoomCodeClicked);
	}

	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		if (MapSelector)
		{
			for (const FString& MapPackageName : GI->GetAvailableMapPackageNames())
			{
				MapSelector->AddOption(MapPackageName);
			}
			MapSelector->SetSelectedOption(GI->GetSelectedMapPackageName());
			MapSelector->OnSelectionChanged.AddDynamic(this, &UMainMenuWidget::OnMapSelectionChanged);
		}
		if (LANModeCheckBox)
		{
			LANModeCheckBox->SetIsChecked(GI->IsUsingLAN());
			LANModeCheckBox->OnCheckStateChanged.AddDynamic(this, &UMainMenuWidget::OnLANModeChanged);
		}
		GI->OnSessionMessage.AddDynamic(this, &UMainMenuWidget::HandleSessionMessage);
		GI->OnRoomCodeReady.AddDynamic(this, &UMainMenuWidget::HandleRoomCodeReady);
		HandleRoomCodeReady(true, GI->GetCurrentRoomCode());
	}
}

void UMainMenuWidget::NativeDestruct()
{
	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		GI->OnSessionMessage.RemoveDynamic(this, &UMainMenuWidget::HandleSessionMessage);
		GI->OnRoomCodeReady.RemoveDynamic(this, &UMainMenuWidget::HandleRoomCodeReady);
	}
	if (LANModeCheckBox)
	{
		LANModeCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UMainMenuWidget::OnLANModeChanged);
	}
	if (MapSelector)
	{
		MapSelector->OnSelectionChanged.RemoveDynamic(this, &UMainMenuWidget::OnMapSelectionChanged);
	}
	Super::NativeDestruct();
}

void UMainMenuWidget::SetStatus(const FString& Text)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Text));
	}
}

void UMainMenuWidget::OnPlaySinglePlayerClicked()
{
	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		GI->PlaySinglePlayer();
	}
}

void UMainMenuWidget::OnCreateRemoteClicked()
{
	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		SetStatus(TEXT("Creating room..."));
		GI->CreateRemoteSession();
	}
}

void UMainMenuWidget::OnJoinRemoteClicked()
{
	if (!RoomCodeInput)
	{
		return;
	}
	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		GI->JoinRemoteSession(RoomCodeInput->GetText().ToString());
	}
}

void UMainMenuWidget::OnCopyRoomCodeClicked()
{
	if (const UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		const FString RoomCode = GI->GetCurrentRoomCode();
		if (!RoomCode.IsEmpty())
		{
			FPlatformApplicationMisc::ClipboardCopy(*RoomCode);
			SetStatus(FString::Printf(TEXT("Room code %s copied."), *RoomCode));
		}
	}
}

void UMainMenuWidget::OnLANModeChanged(bool bIsChecked)
{
	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		if (!GI->SetUseLAN(bIsChecked) && LANModeCheckBox)
		{
			LANModeCheckBox->SetIsChecked(GI->IsUsingLAN());
		}
	}
}

void UMainMenuWidget::OnMapSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (UActionDemoGameInstance* GI = GetGameInstance<UActionDemoGameInstance>())
	{
		GI->SelectTravelMap(SelectedItem);
	}
}

void UMainMenuWidget::HandleSessionMessage(const FString& Message)
{
	SetStatus(Message);
}

void UMainMenuWidget::HandleRoomCodeReady(bool bSuccess, const FString& RoomCode)
{
	if (RoomCodeText)
	{
		RoomCodeText->SetText(FText::FromString(
			bSuccess ? FString::Printf(TEXT("Room code: %s"), *RoomCode) : TEXT("Room code: —")));
	}
}

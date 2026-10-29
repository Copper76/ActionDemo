// Copyright Epic Games, Inc. All Rights Reserved.

#include "PauseMenuWidget.h"

#include "../ActionDemoController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	UTextBlock* MakePauseLabel(UWidgetTree* Tree, const FString& Text, int32 Size)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Text));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		return Label;
	}

	UButton* MakePauseButton(UWidgetTree* Tree, const FString& Text)
	{
		UButton* Button = Tree->ConstructWidget<UButton>();
		Button->SetContent(MakePauseLabel(Tree, Text, 20));
		return Button;
	}
}

TSharedRef<SWidget> UPauseMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UPauseMenuWidget::BuildLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PauseRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.94f));
	Panel->SetPadding(FMargin(36.f));
	if (UCanvasPanelSlot* PanelCanvasSlot = Root->AddChildToCanvas(Panel))
	{
		PanelCanvasSlot->SetAnchors(FAnchors(0.5f));
		PanelCanvasSlot->SetAlignment(FVector2D(0.5f));
		PanelCanvasSlot->SetAutoSize(true);
	}

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->AddChild(Box);
	auto AddRow = [Box](UWidget* Child, float BottomPadding)
	{
		if (UVerticalBoxSlot* RowSlot = Box->AddChildToVerticalBox(Child))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	};

	AddRow(MakePauseLabel(WidgetTree, TEXT("PAUSED"), 34), 24.f);
	ResumeButton = MakePauseButton(WidgetTree, TEXT("Resume"));
	AddRow(ResumeButton, 12.f);
	ReturnToMenuButton = MakePauseButton(WidgetTree, TEXT("Return to Main Menu"));
	AddRow(ReturnToMenuButton, 0.f);
}

void UPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ResumeButton)
	{
		ResumeButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnResumeClicked);
	}
	if (ReturnToMenuButton)
	{
		ReturnToMenuButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnReturnToMenuClicked);
	}
}

void UPauseMenuWidget::OnResumeClicked()
{
	if (AActionDemoController* PC = GetOwningPlayer<AActionDemoController>())
	{
		PC->ResumeFromPauseMenu();
	}
}

void UPauseMenuWidget::OnReturnToMenuClicked()
{
	if (AActionDemoController* PC = GetOwningPlayer<AActionDemoController>())
	{
		PC->ReturnToMainMenuFromPause();
	}
}

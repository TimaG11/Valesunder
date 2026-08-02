#include "ArmyDeploymentCellWidget.h"

#include "GameLoadingGameInstance.h"
#include "ArmyDeploymentWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"


namespace
{
	void HideLegacyDeploymentCellChrome(UArmyDeploymentCellWidget* CellWidget, UImage* PortraitImage)
	{
		if (!CellWidget || !CellWidget->WidgetTree)
		{
			return;
		}

		TArray<UWidget*> Widgets;
		CellWidget->WidgetTree->GetAllWidgets(Widgets);
		for (UWidget* Widget : Widgets)
		{
			if (UImage* Image = Cast<UImage>(Widget))
			{
				if (Image != PortraitImage)
				{
					// The orange hexes now come from the field photo itself. Any old cell image
					// (dark hex, blocked hex, selection fill, line marker) would be a duplicate grid.
					Image->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
	}
}

void UArmyDeploymentCellWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(this);
	}

	HideLegacyDeploymentCellChrome(this, UnitPortraitImage);
	BindButton();
	ApplyRuntimeLayout();
	RefreshVisuals();
}

FReply UArmyDeploymentCellWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bAllowedCell && OwnerDeploymentWidget && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		HandleCellClicked();
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UArmyDeploymentCellWidget::InitializeCell(UArmyDeploymentWidget* InOwnerDeploymentWidget, int32 InQ, int32 InR, bool bInAllowedCell)
{
	OwnerDeploymentWidget = InOwnerDeploymentWidget;
	Q = InQ;
	R = InR;
	bAllowedCell = bInAllowedCell;
	PlacedUnitClass = nullptr;
	PlacedUnitIndex = INDEX_NONE;
	bSelectedCell = false;
	bDeploymentLineCell = false;

	HideLegacyDeploymentCellChrome(this, UnitPortraitImage);
	BindButton();
	ApplyRuntimeLayout();
	RefreshVisuals();
}

void UArmyDeploymentCellWidget::SetPlacedUnit(TSubclassOf<AHexUnitActor> InUnitClass, int32 InUnitIndex)
{
	PlacedUnitClass = InUnitClass;
	PlacedUnitIndex = InUnitIndex;
	RefreshVisuals();
}

void UArmyDeploymentCellWidget::SetSelectedCell(bool bInSelectedCell)
{
	bSelectedCell = bInSelectedCell;
	RefreshVisuals();
}

void UArmyDeploymentCellWidget::SetDeploymentLineCell(bool bInDeploymentLineCell)
{
	bDeploymentLineCell = bInDeploymentLineCell;
	RefreshVisuals();
}

void UArmyDeploymentCellWidget::SetRuntimeCellSize(float InCellWidth, float InCellHeight)
{
	RuntimeCellWidth = FMath::Max(1.0f, InCellWidth);
	RuntimeCellHeight = FMath::Max(1.0f, InCellHeight);
	ApplyRuntimeLayout();
}

void UArmyDeploymentCellWidget::SetPortraitSize(float InPortraitSize)
{
	RuntimePortraitSize = FMath::Max(1.0f, InPortraitSize);
	RefreshVisuals();
}

void UArmyDeploymentCellWidget::HandleCellClicked()
{
	if (!bAllowedCell || !OwnerDeploymentWidget)
	{
		return;
	}

	OwnerDeploymentWidget->HandleDeploymentCellClicked(Q, R);
}

void UArmyDeploymentCellWidget::BindButton()
{
	if (!CellButton)
	{
		return;
	}

	CellButton->OnClicked.RemoveDynamic(this, &UArmyDeploymentCellWidget::HandleCellClicked);
	CellButton->OnClicked.AddDynamic(this, &UArmyDeploymentCellWidget::HandleCellClicked);
	CellButton->SetIsEnabled(bAllowedCell);
	ApplyTransparentButtonStyle();
}

void UArmyDeploymentCellWidget::ApplyTransparentButtonStyle()
{
	if (!CellButton)
	{
		return;
	}

	FSlateBrush EmptyBrush;
	EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;

	FButtonStyle TransparentStyle = CellButton->GetStyle();
	TransparentStyle.SetNormal(EmptyBrush);
	TransparentStyle.SetHovered(EmptyBrush);
	TransparentStyle.SetPressed(EmptyBrush);
	TransparentStyle.SetDisabled(EmptyBrush);

	CellButton->SetStyle(TransparentStyle);
	CellButton->SetBackgroundColor(FLinearColor::Transparent);
}

void UArmyDeploymentCellWidget::ApplyRuntimeLayout()
{
	// WBP_ArmyDeploymentCell currently has a root SizeBox. If it stays 36x36,
	// CanvasPanelSlot size from the parent will not make the visible hex bigger.
	if (USizeBox* RootSizeBox = Cast<USizeBox>(GetRootWidget()))
	{
		RootSizeBox->SetWidthOverride(RuntimeCellWidth);
		RootSizeBox->SetHeightOverride(RuntimeCellHeight);
	}

	InvalidateLayoutAndVolatility();
}

void UArmyDeploymentCellWidget::RefreshVisuals()
{
	const AHexUnitActor* DefaultUnit = PlacedUnitClass ? PlacedUnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;

	if (CoordTextBlock)
	{
		CoordTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%d,%d"), Q, R)));
		CoordTextBlock->SetVisibility(bShowDebugCoords ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (UnitNameTextBlock)
	{
		if (DefaultUnit && bShowUnitNameOnGrid)
		{
			const FString DisplayName = DefaultUnit->UnitDisplayName.IsEmpty()
				? GetNameSafe(PlacedUnitClass.Get())
				: DefaultUnit->UnitDisplayName;

			UnitNameTextBlock->SetText(FText::FromString(DisplayName));
			UnitNameTextBlock->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			UnitNameTextBlock->SetText(FText::GetEmpty());
			UnitNameTextBlock->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (UnitPortraitImage)
	{
		// v5 draws portraits directly in ArmyDeploymentWidget above the photo. Keeping the
		// old Blueprint portrait slot hidden prevents its tiny/clipped copy from showing.
		UnitPortraitImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (BlockedOverlay)
	{
		BlockedOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (SelectedOverlay)
	{
		SelectedOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (DeploymentLineOverlay)
	{
		DeploymentLineOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (CellButton)
	{
		CellButton->SetIsEnabled(bAllowedCell);
	}

	// Never dim the whole widget: doing so also dims placed portraits.
	SetRenderOpacity(1.0f);
}

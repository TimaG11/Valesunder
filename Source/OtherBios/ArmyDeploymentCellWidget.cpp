#include "ArmyDeploymentCellWidget.h"

#include "GameLoadingGameInstance.h"
#include "ArmyDeploymentWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

void UArmyDeploymentCellWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(this);
	}

	BindButton();
	ApplyRuntimeLayout();
	RefreshVisuals();
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
		if (DefaultUnit && DefaultUnit->UnitPortrait)
		{
			// Do not match the real texture size here. Portrait textures are usually huge
			// and will overflow the deployment cell.
			UnitPortraitImage->SetBrushFromTexture(DefaultUnit->UnitPortrait, false);
			UnitPortraitImage->SetBrushSize(FVector2D(RuntimePortraitSize, RuntimePortraitSize));
			UnitPortraitImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			UnitPortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (BlockedOverlay)
	{
		BlockedOverlay->SetVisibility((!bAllowedCell && bShowBlockedOverlay) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (SelectedOverlay)
	{
		SelectedOverlay->SetVisibility(bSelectedCell ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (DeploymentLineOverlay)
	{
		DeploymentLineOverlay->SetVisibility((bAllowedCell && bDeploymentLineCell) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (CellButton)
	{
		CellButton->SetIsEnabled(bAllowedCell);
	}

	SetRenderOpacity(bAllowedCell ? 1.0f : 0.28f);
}

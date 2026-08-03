#include "ArmyDeploymentWidget.h"

#include "ArmyDeploymentCellWidget.h"
#include "ArmyUnitCardWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"

namespace
{
	// v6: photo cells are saved as an exact discrete (column,row) address.
	// This intentionally replaces the old normalized-position encoding, which forced
	// HexGridActor to choose the nearest battle cell and could alter formations.
	constexpr int32 ExactPhotoCellBase = 200000;
	constexpr int32 LegacyPhotoCoordBase = 100000;
	constexpr int32 LegacyPhotoCoordScale = 10000;

	int32 EncodePhotoColumn(int32 ColumnIndex)
	{
		return ExactPhotoCellBase + FMath::Max(0, ColumnIndex);
	}

	int32 EncodePhotoRow(int32 RowIndex)
	{
		return ExactPhotoCellBase + FMath::Max(0, RowIndex);
	}

	bool IsEncodedPhotoCoord(int32 Q, int32 R)
	{
		const bool bExactCell = Q >= ExactPhotoCellBase && Q < ExactPhotoCellBase + 1000
			&& R >= ExactPhotoCellBase && R < ExactPhotoCellBase + 1000;
		const bool bLegacyNormalized = Q >= LegacyPhotoCoordBase && Q <= LegacyPhotoCoordBase + LegacyPhotoCoordScale
			&& R >= LegacyPhotoCoordBase && R <= LegacyPhotoCoordBase + LegacyPhotoCoordScale;
		return bExactCell || bLegacyNormalized;
	}
}

void UArmyDeploymentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	EnsureDeploymentFieldBackground();
	BindButtons();
	RefreshAllVisuals();
}

void UArmyDeploymentWidget::EnsureDeploymentFieldBackground()
{
	if (!DeploymentGridPanel || !WidgetTree)
	{
		return;
	}

	UTexture2D* TextureToUse = DeploymentFieldTexture;
	FSlateBrush DesignerBrush;
	bool bHasDesignerBrush = false;

	if (DeploymentFieldImage)
	{
		DesignerBrush = DeploymentFieldImage->GetBrush();
		bHasDesignerBrush = DesignerBrush.GetResourceObject() != nullptr;

		if (!TextureToUse)
		{
			TextureToUse = Cast<UTexture2D>(DesignerBrush.GetResourceObject());
		}
	}

	if (!RuntimeDeploymentFieldImage)
	{
		RuntimeDeploymentFieldImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("RuntimeDeploymentFieldImage"));
		if (RuntimeDeploymentFieldImage)
		{
			if (UCanvasPanelSlot* BackgroundSlot = DeploymentGridPanel->AddChildToCanvas(RuntimeDeploymentFieldImage))
			{
				BackgroundSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				BackgroundSlot->SetOffsets(FMargin(0.0f));
				BackgroundSlot->SetAlignment(FVector2D::ZeroVector);
				BackgroundSlot->SetAutoSize(false);
				BackgroundSlot->SetZOrder(-100);
			}
		}
	}

	if (!RuntimeDeploymentFieldImage)
	{
		return;
	}

	if (TextureToUse)
	{
		RuntimeDeploymentFieldImage->SetBrushFromTexture(TextureToUse, false);
		RuntimeDeploymentFieldImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UE_LOG(LogTemp, Log, TEXT("Deployment photo background active: %s"), *GetNameSafe(TextureToUse));
	}
	else if (bHasDesignerBrush)
	{
		RuntimeDeploymentFieldImage->SetBrush(DesignerBrush);
		RuntimeDeploymentFieldImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UE_LOG(LogTemp, Log, TEXT("Deployment photo background copied from designer brush."));
	}
	else
	{
		RuntimeDeploymentFieldImage->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogTemp, Error, TEXT("Deployment photo is missing. Set Army Deployment|Photo|Deployment Field Texture or assign a brush to DeploymentFieldImage."));
	}

	// The old designer Image is intentionally hidden after we copied its brush.
	// This prevents a white/empty brush in the nested SizeBox from covering the runtime photo.
	if (DeploymentFieldImage && RuntimeDeploymentFieldImage->GetVisibility() != ESlateVisibility::Collapsed)
	{
		DeploymentFieldImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UArmyDeploymentWidget::InitializeDeployment(UArmyBuilderWidget* InOwnerArmyBuilder, const TArray<TSubclassOf<AHexUnitActor>>& InUnitClasses, const TArray<FArmyBuilderDeploymentSlot>& InSavedDeploymentSlots)
{
	OwnerArmyBuilder = InOwnerArmyBuilder;
	UnitClasses = InUnitClasses;
	UnitClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			return !UnitClass;
		});

	DeploymentSlots.Reset();
	for (const FArmyBuilderDeploymentSlot& DeploymentSlot : InSavedDeploymentSlots)
	{
		if (DeploymentSlot.UnitIndex >= 0
			&& DeploymentSlot.UnitIndex < UnitClasses.Num()
			&& IsCoordAllowed(DeploymentSlot.Q, DeploymentSlot.R)
			&& !IsCoordUsed(DeploymentSlot.Q, DeploymentSlot.R))
		{
			DeploymentSlots.Add(DeploymentSlot);
		}
	}

	SelectedUnitIndex = UnitClasses.IsEmpty() ? INDEX_NONE : 0;

	AutoFillDeploymentIfNeeded();
	BindButtons();
	RebuildUnitCards();
	RebuildGrid();
	RefreshAllVisuals();
}

FArmyBuilderUnitProgress UArmyDeploymentWidget::GetDeploymentUnitProgressAt(int32 UnitIndex) const
{
	if (OwnerArmyBuilder && UnitClasses.IsValidIndex(UnitIndex))
	{
		return OwnerArmyBuilder->GetSelectedUnitProgressAt(UnitIndex);
	}

	return UnitClasses.IsValidIndex(UnitIndex)
		? UArmyBuilderWidget::MakeDefaultUnitProgress(UnitClasses[UnitIndex])
		: FArmyBuilderUnitProgress();
}

void UArmyDeploymentWidget::SelectUnitForDeployment(int32 UnitIndex)
{
	if (!UnitClasses.IsValidIndex(UnitIndex))
	{
		return;
	}

	SelectedUnitIndex = UnitIndex;
	RefreshAllVisuals();
}

void UArmyDeploymentWidget::HandleDeploymentCellClicked(int32 Q, int32 R)
{
	if (!UnitClasses.IsValidIndex(SelectedUnitIndex) || !IsCoordAllowed(Q, R))
	{
		return;
	}

	const int32 SelectedSlotIndex = FindSlotIndexForUnit(SelectedUnitIndex);
	const int32 OtherUnitIndexAtCoord = FindUnitIndexAtCoord(Q, R);

	if (OtherUnitIndexAtCoord == SelectedUnitIndex)
	{
		return;
	}

	if (OtherUnitIndexAtCoord != INDEX_NONE)
	{
		const int32 OtherSlotIndex = FindSlotIndexForUnit(OtherUnitIndexAtCoord);

		if (SelectedSlotIndex != INDEX_NONE && OtherSlotIndex != INDEX_NONE)
		{
			const int32 OldQ = DeploymentSlots[SelectedSlotIndex].Q;
			const int32 OldR = DeploymentSlots[SelectedSlotIndex].R;

			DeploymentSlots[SelectedSlotIndex].Q = Q;
			DeploymentSlots[SelectedSlotIndex].R = R;

			DeploymentSlots[OtherSlotIndex].Q = OldQ;
			DeploymentSlots[OtherSlotIndex].R = OldR;
		}
		else if (OtherSlotIndex != INDEX_NONE)
		{
			DeploymentSlots.RemoveAt(OtherSlotIndex);
			DeploymentSlots.Add(FArmyBuilderDeploymentSlot(SelectedUnitIndex, Q, R));
		}
	}
	else if (SelectedSlotIndex != INDEX_NONE)
	{
		DeploymentSlots[SelectedSlotIndex].Q = Q;
		DeploymentSlots[SelectedSlotIndex].R = R;
	}
	else
	{
		DeploymentSlots.Add(FArmyBuilderDeploymentSlot(SelectedUnitIndex, Q, R));
	}

	for (int32 Index = 0; Index < UnitClasses.Num(); ++Index)
	{
		if (FindSlotIndexForUnit(Index) == INDEX_NONE)
		{
			SelectedUnitIndex = Index;
			RefreshAllVisuals();
			return;
		}
	}

	RefreshAllVisuals();
}

void UArmyDeploymentWidget::HandleSaveClicked()
{
	if (!OwnerArmyBuilder || !HasCompleteDeployment())
	{
		RefreshAllVisuals();
		return;
	}

	if (OwnerArmyBuilder->SaveArmyDeployment(DeploymentSlots))
	{
		RemoveFromParent();
	}
}

void UArmyDeploymentWidget::HandleClearClicked()
{
	DeploymentSlots.Reset();
	SelectedUnitIndex = UnitClasses.IsEmpty() ? INDEX_NONE : 0;
	RefreshAllVisuals();
}

void UArmyDeploymentWidget::HandleCloseClicked()
{
	RemoveFromParent();
}

void UArmyDeploymentWidget::BindButtons()
{
	if (SaveDeploymentButton)
	{
		SaveDeploymentButton->OnClicked.RemoveDynamic(this, &UArmyDeploymentWidget::HandleSaveClicked);
		SaveDeploymentButton->OnClicked.AddDynamic(this, &UArmyDeploymentWidget::HandleSaveClicked);
	}

	if (ClearDeploymentButton)
	{
		ClearDeploymentButton->OnClicked.RemoveDynamic(this, &UArmyDeploymentWidget::HandleClearClicked);
		ClearDeploymentButton->OnClicked.AddDynamic(this, &UArmyDeploymentWidget::HandleClearClicked);
	}

	if (CloseDeploymentButton)
	{
		CloseDeploymentButton->OnClicked.RemoveDynamic(this, &UArmyDeploymentWidget::HandleCloseClicked);
		CloseDeploymentButton->OnClicked.AddDynamic(this, &UArmyDeploymentWidget::HandleCloseClicked);
	}
}

void UArmyDeploymentWidget::RebuildUnitCards()
{
	DeploymentUnitCardWidgets.Reset();

	if (!DeploymentUnitsPanel)
	{
		return;
	}

	DeploymentUnitsPanel->ClearChildren();

	if (!DeploymentUnitCardWidgetClass)
	{
		return;
	}

	for (int32 Index = 0; Index < UnitClasses.Num(); ++Index)
	{
		UArmyUnitCardWidget* CardWidget = CreateWidget<UArmyUnitCardWidget>(this, DeploymentUnitCardWidgetClass);
		if (!CardWidget)
		{
			continue;
		}

		CardWidget->InitializeDeploymentPickCard(this, UnitClasses[Index], Index);

		if (UPanelSlot* PanelSlot = DeploymentUnitsPanel->AddChild(CardWidget))
		{
			if (UVerticalBoxSlot* VerticalBoxSlot = Cast<UVerticalBoxSlot>(PanelSlot))
			{
				VerticalBoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			}
			else if (UScrollBoxSlot* ScrollBoxSlot = Cast<UScrollBoxSlot>(PanelSlot))
			{
				ScrollBoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			}
		}

		DeploymentUnitCardWidgets.Add(CardWidget);
	}
}

void UArmyDeploymentWidget::RebuildGrid()
{
	if (!DeploymentGridPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("DeploymentGridPanel is missing. In WBP_ArmyDeployment it must be a CanvasPanel named DeploymentGridPanel."));
		return;
	}

	for (int32 ChildIndex = DeploymentGridPanel->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
	{
		if (Cast<UArmyDeploymentCellWidget>(DeploymentGridPanel->GetChildAt(ChildIndex)))
		{
			DeploymentGridPanel->RemoveChildAt(ChildIndex);
		}
	}

	EnsureDeploymentFieldBackground();
	CellWidgets.Reset();

	if (!DeploymentCellWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DeploymentCellWidgetClass is not set in WBP_ArmyDeployment."));
		return;
	}

	if (!bUsePhotoAlignedGrid)
	{
		UE_LOG(LogTemp, Warning, TEXT("Legacy deployment grid is disabled in v4. Enable bUsePhotoAlignedGrid."));
		return;
	}

	const int32 Columns = FMath::Max(2, PhotoColumnCount);
	const int32 Rows = FMath::Max(2, PhotoRowCount);
	const float SafeLeft = FMath::Clamp(FMath::Min(PhotoGridLeft, PhotoGridRight), 0.0f, 1.0f);
	const float SafeRight = FMath::Clamp(FMath::Max(PhotoGridLeft, PhotoGridRight), 0.0f, 1.0f);
	const float SafeTop = FMath::Clamp(FMath::Min(PhotoGridTop, PhotoGridBottom), 0.0f, 1.0f);
	const float SafeBottom = FMath::Clamp(FMath::Max(PhotoGridTop, PhotoGridBottom), 0.0f, 1.0f);
	const float StepX = (SafeRight - SafeLeft) / static_cast<float>(Columns - 1);
	const float StepY = (SafeBottom - SafeTop) / static_cast<float>(Rows - 1);

	const float ApproxPanelWidth = 720.0f;
	const float ApproxPanelHeight = 400.0f;
	const float RuntimeWidth = FMath::Max(18.0f, StepX * ApproxPanelWidth * PhotoHitboxScale);
	const float RuntimeHeight = FMath::Max(18.0f, StepY * ApproxPanelHeight * PhotoHitboxScale);

	for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
	{
		const bool bShortRow = bPhotoEvenRowsAreShort ? ((RowIndex % 2) == 0) : ((RowIndex % 2) != 0);
		const int32 CellsThisRow = bShortRow ? Columns - 1 : Columns;
		const float RowShift = bShortRow ? 0.5f : 0.0f;

		for (int32 ColumnIndex = 0; ColumnIndex < CellsThisRow; ++ColumnIndex)
		{
			const float AnchorX = SafeLeft + (static_cast<float>(ColumnIndex) + RowShift) * StepX;
			const float AnchorY = SafeTop + static_cast<float>(RowIndex) * StepY;

			// Store the exact photo-cell address, not a floating/normalized position.
			// Q carries column index, R carries row index.
			const int32 EncodedQ = EncodePhotoColumn(ColumnIndex);
			const int32 EncodedR = EncodePhotoRow(RowIndex);

			UArmyDeploymentCellWidget* CellWidget = CreateWidget<UArmyDeploymentCellWidget>(this, DeploymentCellWidgetClass);
			if (!CellWidget)
			{
				continue;
			}

			CellWidget->SetRuntimeCellSize(RuntimeWidth, RuntimeHeight);
			CellWidget->SetPortraitSize(FMath::Min(RuntimeWidth, RuntimeHeight) * 1.15f);
			CellWidget->InitializeCell(this, EncodedQ, EncodedR, true);
			CellWidget->SetDeploymentLineCell(false);

			if (UCanvasPanelSlot* CanvasSlot = DeploymentGridPanel->AddChildToCanvas(CellWidget))
			{
				CanvasSlot->SetAutoSize(false);
				CanvasSlot->SetSize(FVector2D(RuntimeWidth, RuntimeHeight));
				CanvasSlot->SetAnchors(FAnchors(AnchorX, AnchorY));
				CanvasSlot->SetPosition(FVector2D::ZeroVector);
				CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				CanvasSlot->SetZOrder(20);
			}

			CellWidgets.Add(CellWidget);
		}
	}

	if (EnemyDirectionTextBlock)
	{
		EnemyDirectionTextBlock->SetText(FText::GetEmpty());
	}
	if (PlayerDirectionTextBlock)
	{
		PlayerDirectionTextBlock->SetText(FText::GetEmpty());
	}

	UE_LOG(LogTemp, Log, TEXT("Photo deployment v6 exact-cell grid rebuilt: visible cells=%d. Every photo cell is selectable."), CellWidgets.Num());
}

void UArmyDeploymentWidget::RefreshAllVisuals()
{
	// Rebuild the few visible unit portraits as direct children of DeploymentGridPanel.
	// The legacy cell Blueprint has a very small portrait slot, which is why portraits
	// previously appeared as thin slivers even when BrushSize was increased in C++.
	for (UImage* PortraitImage : RuntimePhotoPortraitImages)
	{
		if (PortraitImage)
		{
			PortraitImage->RemoveFromParent();
		}
	}
	RuntimePhotoPortraitImages.Reset();
	for (UArmyDeploymentCellWidget* CellWidget : CellWidgets)
	{
		if (!CellWidget)
		{
			continue;
		}

		const int32 Q = CellWidget->GetQ();
		const int32 R = CellWidget->GetR();
		const int32 UnitIndex = FindUnitIndexAtCoord(Q, R);

		CellWidget->SetPlacedUnit(UnitClasses.IsValidIndex(UnitIndex) ? UnitClasses[UnitIndex] : nullptr, UnitIndex);

		if (bUsePhotoAlignedGrid && DeploymentGridPanel && WidgetTree && UnitClasses.IsValidIndex(UnitIndex))
		{
			const AHexUnitActor* DefaultUnit = UnitClasses[UnitIndex]
				? UnitClasses[UnitIndex]->GetDefaultObject<AHexUnitActor>()
				: nullptr;

			if (DefaultUnit && DefaultUnit->UnitPortrait)
			{
				if (UCanvasPanelSlot* CellCanvasSlot = Cast<UCanvasPanelSlot>(CellWidget->Slot))
				{
					UImage* PortraitImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
					if (PortraitImage)
					{
						const float PortraitSize = FMath::Max(20.0f, PhotoUnitPortraitSize);
						PortraitImage->SetBrushFromTexture(DefaultUnit->UnitPortrait, false);
						PortraitImage->SetDesiredSizeOverride(FVector2D(PortraitSize, PortraitSize));
						PortraitImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

						if (UCanvasPanelSlot* PortraitSlot = DeploymentGridPanel->AddChildToCanvas(PortraitImage))
						{
							PortraitSlot->SetAutoSize(false);
							PortraitSlot->SetSize(FVector2D(PortraitSize, PortraitSize));
							PortraitSlot->SetAnchors(CellCanvasSlot->GetAnchors());
							PortraitSlot->SetPosition(FVector2D::ZeroVector);
							PortraitSlot->SetAlignment(FVector2D(0.5f, 0.5f));
							PortraitSlot->SetZOrder(30);
						}

						RuntimePhotoPortraitImages.Add(PortraitImage);
					}
				}
			}
		}

		CellWidget->SetSelectedCell(UnitClasses.IsValidIndex(SelectedUnitIndex) && UnitIndex == SelectedUnitIndex);
		CellWidget->SetDeploymentLineCell(IsDeploymentLineCell(Q, R));
	}

	for (int32 Index = 0; Index < DeploymentUnitCardWidgets.Num(); ++Index)
	{
		if (DeploymentUnitCardWidgets[Index])
		{
			DeploymentUnitCardWidgets[Index]->SetDeploymentSelected(Index == SelectedUnitIndex);
		}
	}

	if (SaveDeploymentButton)
	{
		const bool bCanSave = HasCompleteDeployment();
		SaveDeploymentButton->SetIsEnabled(bCanSave);
		SaveDeploymentButton->SetRenderOpacity(bCanSave ? 1.0f : 0.45f);
	}

	if (ClearDeploymentButton)
	{
		ClearDeploymentButton->SetIsEnabled(!DeploymentSlots.IsEmpty());
		ClearDeploymentButton->SetRenderOpacity(DeploymentSlots.IsEmpty() ? 0.45f : 1.0f);
	}

	RefreshHintText();
}

void UArmyDeploymentWidget::RefreshHintText()
{
	if (!DeploymentHintTextBlock)
	{
		return;
	}

	if (!UnitClasses.IsValidIndex(SelectedUnitIndex))
	{
		DeploymentHintTextBlock->SetText(NSLOCTEXT("ArmyDeployment", "NoUnitSelected", "Select a unit, then click a bright hex. Enemy is on the right."));
		return;
	}

	const AHexUnitActor* DefaultUnit = UnitClasses[SelectedUnitIndex]
		? UnitClasses[SelectedUnitIndex]->GetDefaultObject<AHexUnitActor>()
		: nullptr;

	const FString DisplayName = DefaultUnit && !DefaultUnit->UnitDisplayName.IsEmpty()
		? DefaultUnit->UnitDisplayName
		: GetNameSafe(UnitClasses[SelectedUnitIndex].Get());

	DeploymentHintTextBlock->SetText(FText::FromString(FString::Printf(
		TEXT("Selected: %s. Click a bright hex on your side. Enemy is on the right. Placed %d / %d."),
		*DisplayName,
		DeploymentSlots.Num(),
		UnitClasses.Num()
	)));
}

void UArmyDeploymentWidget::AutoFillDeploymentIfNeeded()
{
	if (!bAutoFillEmptyDeployment || !DeploymentSlots.IsEmpty())
	{
		return;
	}

	const FIntPoint PreferredCoords[] =
	{
		// Stock formation on the four left columns of the photo.
		FIntPoint(-6, 0),
		FIntPoint(-6, 2),
		FIntPoint(-5, -1),
		FIntPoint(-5, 2),
		FIntPoint(-4, 0)
	};

	int32 NextUnitIndex = 0;
	for (const FIntPoint& Coord : PreferredCoords)
	{
		if (!UnitClasses.IsValidIndex(NextUnitIndex))
		{
			return;
		}

		if (IsCoordAllowed(Coord.X, Coord.Y) && !IsCoordUsed(Coord.X, Coord.Y))
		{
			DeploymentSlots.Add(FArmyBuilderDeploymentSlot(NextUnitIndex, Coord.X, Coord.Y));
			++NextUnitIndex;
		}
	}

	const TArray<FIntPoint> AllowedCoords = GetAllowedCoordsInDisplayOrder();
	for (const FIntPoint& Coord : AllowedCoords)
	{
		if (!UnitClasses.IsValidIndex(NextUnitIndex))
		{
			return;
		}

		if (!IsCoordUsed(Coord.X, Coord.Y))
		{
			DeploymentSlots.Add(FArmyBuilderDeploymentSlot(NextUnitIndex, Coord.X, Coord.Y));
			++NextUnitIndex;
		}
	}
}

bool UArmyDeploymentWidget::IsCoordAllowed(int32 Q, int32 R) const
{
	if (bUsePhotoAlignedGrid)
	{
		return IsEncodedPhotoCoord(Q, R);
	}

	if (!AllowedDeploymentCoords.IsEmpty())
	{
		for (const FArmyBuilderDeploymentSlot& Coord : AllowedDeploymentCoords)
		{
			if (Coord.Q == Q && Coord.R == R)
			{
				return true;
			}
		}
		return false;
	}

	return Q <= AllowedDeploymentMaxQ;
}

bool UArmyDeploymentWidget::IsCoordUsed(int32 Q, int32 R, int32 IgnoreUnitIndex) const
{
	for (const FArmyBuilderDeploymentSlot& DeploymentSlot : DeploymentSlots)
	{
		if (DeploymentSlot.UnitIndex == IgnoreUnitIndex)
		{
			continue;
		}

		if (DeploymentSlot.Q == Q && DeploymentSlot.R == R)
		{
			return true;
		}
	}

	return false;
}

bool UArmyDeploymentWidget::IsDeploymentLineCell(int32 Q, int32 R) const
{
	if (bUsePhotoAlignedGrid)
	{
		return false;
	}

	if (!IsCoordAllowed(Q, R))
	{
		return false;
	}

	if (AllowedDeploymentCoords.IsEmpty())
	{
		return Q == AllowedDeploymentMaxQ;
	}

	const int32 Step = FMath::Max(1, DisplayCoordStep);
	const FIntPoint Neighbors[6] =
	{
		FIntPoint(Q + Step, R),
		FIntPoint(Q, R + Step),
		FIntPoint(Q - Step, R + Step),
		FIntPoint(Q - Step, R),
		FIntPoint(Q, R - Step),
		FIntPoint(Q + Step, R - Step)
	};

	for (const FIntPoint& Neighbor : Neighbors)
	{
		if (!IsCoordAllowed(Neighbor.X, Neighbor.Y))
		{
			return true;
		}
	}

	return false;
}

int32 UArmyDeploymentWidget::FindSlotIndexForUnit(int32 UnitIndex) const
{
	for (int32 Index = 0; Index < DeploymentSlots.Num(); ++Index)
	{
		if (DeploymentSlots[Index].UnitIndex == UnitIndex)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 UArmyDeploymentWidget::FindUnitIndexAtCoord(int32 Q, int32 R) const
{
	for (const FArmyBuilderDeploymentSlot& DeploymentSlot : DeploymentSlots)
	{
		if (DeploymentSlot.Q == Q && DeploymentSlot.R == R)
		{
			return DeploymentSlot.UnitIndex;
		}
	}

	return INDEX_NONE;
}

bool UArmyDeploymentWidget::HasCompleteDeployment() const
{
	if (UnitClasses.IsEmpty() || DeploymentSlots.Num() != UnitClasses.Num())
	{
		return false;
	}

	TSet<int32> UsedUnitIndexes;
	TSet<FIntPoint> UsedCoords;

	for (const FArmyBuilderDeploymentSlot& DeploymentSlot : DeploymentSlots)
	{
		if (!UnitClasses.IsValidIndex(DeploymentSlot.UnitIndex) || !IsCoordAllowed(DeploymentSlot.Q, DeploymentSlot.R))
		{
			return false;
		}

		const FIntPoint Coord(DeploymentSlot.Q, DeploymentSlot.R);
		if (UsedUnitIndexes.Contains(DeploymentSlot.UnitIndex) || UsedCoords.Contains(Coord))
		{
			return false;
		}

		UsedUnitIndexes.Add(DeploymentSlot.UnitIndex);
		UsedCoords.Add(Coord);
	}

	return UsedUnitIndexes.Num() == UnitClasses.Num();
}

TArray<FIntPoint> UArmyDeploymentWidget::GetAllowedCoordsInDisplayOrder() const
{
	TArray<FIntPoint> Result;

	if (bUsePhotoAlignedGrid)
	{
		const int32 Columns = FMath::Max(2, PhotoColumnCount);
		const int32 Rows = FMath::Max(2, PhotoRowCount);
		for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
		{
			const bool bShortRow = bPhotoEvenRowsAreShort ? ((RowIndex % 2) == 0) : ((RowIndex % 2) != 0);
			const int32 CellsThisRow = bShortRow ? Columns - 1 : Columns;
			for (int32 ColumnIndex = 0; ColumnIndex < CellsThisRow; ++ColumnIndex)
			{
				Result.Add(FIntPoint(EncodePhotoColumn(ColumnIndex), EncodePhotoRow(RowIndex)));
			}
		}
		return Result;
	}

	const int32 Step = FMath::Max(1, DisplayCoordStep);
	for (int32 R = DisplayRMin; R <= DisplayRMax; R += Step)
	{
		for (int32 Q = DisplayQMin; Q <= DisplayQMax; Q += Step)
		{
			if (IsCoordAllowed(Q, R))
			{
				Result.Add(FIntPoint(Q, R));
			}
		}
	}
	return Result;
}

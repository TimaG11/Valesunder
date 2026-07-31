#include "ArmyDeploymentWidget.h"

#include "GameLoadingGameInstance.h"
#include "ArmyDeploymentCellWidget.h"
#include "ArmyUnitCardWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"

void UArmyDeploymentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(this);
	}

	BindButtons();
	RefreshAllVisuals();
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

		if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
		{
			LoadingGameInstance->BindButtonClickSounds(CardWidget);
		}

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
	CellWidgets.Reset();

	if (!DeploymentGridPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("DeploymentGridPanel is missing. In WBP_ArmyDeployment it must be a CanvasPanel named DeploymentGridPanel."));
		return;
	}

	DeploymentGridPanel->ClearChildren();

	if (!DeploymentCellWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DeploymentCellWidgetClass is not set in WBP_ArmyDeployment."));
		return;
	}

	struct FPreviewCellData
	{
		int32 Q = 0;
		int32 R = 0;
		FVector2D RawPosition = FVector2D::ZeroVector;
	};

	TArray<FPreviewCellData> PreviewCells;

	const int32 Step = FMath::Max(1, DisplayCoordStep);

	float MinX = TNumericLimits<float>::Max();
	float MinY = TNumericLimits<float>::Max();

	for (int32 Q = DisplayQMin; Q <= DisplayQMax; Q += Step)
	{
		const int32 ColumnIndex = (Q - DisplayQMin) / Step;

		for (int32 R = DisplayRMin; R <= DisplayRMax; R += Step)
		{
			const int32 RowIndex = (R - DisplayRMin) / Step;

			float X = 0.0f;
			float Y = 0.0f;

			if (bUseStaggeredPreviewGrid)
			{
				// UI layout, not world projection: q grows to the right, each next q-column is shifted down.
				// This makes the deployment screen readable and avoids the broken rectangular/overlapped look.
				X = static_cast<float>(ColumnIndex) * HexHorizontalSpacing;
				Y = static_cast<float>(RowIndex) * HexVerticalSpacing + ((ColumnIndex % 2) != 0 ? HexVerticalSpacing * 0.5f : 0.0f);
			}
			else
			{
				const float HexQ = static_cast<float>(Q) / static_cast<float>(Step);
				const float HexR = static_cast<float>(R) / static_cast<float>(Step);

				// Axial hex preview layout fallback.
				X = (HexQ + HexR * 0.5f) * HexHorizontalSpacing;
				Y = HexR * HexVerticalSpacing;
			}

			FPreviewCellData& NewCell = PreviewCells.AddDefaulted_GetRef();
			NewCell.Q = Q;
			NewCell.R = R;
			NewCell.RawPosition = FVector2D(X, Y);

			MinX = FMath::Min(MinX, X);
			MinY = FMath::Min(MinY, Y);
		}
	}

	if (PreviewCells.IsEmpty())
	{
		return;
	}

	const FVector2D GlobalOffset(GridPaddingX - MinX, GridPaddingY - MinY);

	for (const FPreviewCellData& CellData : PreviewCells)
	{
		UArmyDeploymentCellWidget* CellWidget = CreateWidget<UArmyDeploymentCellWidget>(this, DeploymentCellWidgetClass);
		if (!CellWidget)
		{
			continue;
		}

		CellWidget->SetRuntimeCellSize(HexCellWidth, HexCellHeight);
		CellWidget->SetPortraitSize(GridUnitPortraitSize);

		const bool bAllowedCell = IsCoordAllowed(CellData.Q, CellData.R);
		CellWidget->InitializeCell(this, CellData.Q, CellData.R, bAllowedCell);

		if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
		{
			LoadingGameInstance->BindButtonClickSounds(CellWidget);
		}
		CellWidget->SetDeploymentLineCell(IsDeploymentLineCell(CellData.Q, CellData.R));

		if (UCanvasPanelSlot* CanvasSlot = DeploymentGridPanel->AddChildToCanvas(CellWidget))
		{
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetSize(FVector2D(HexCellWidth, HexCellHeight));
			CanvasSlot->SetPosition(CellData.RawPosition + GlobalOffset);
			CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
			CanvasSlot->SetZOrder(bAllowedCell ? 2 : 1);
		}

		CellWidgets.Add(CellWidget);
	}

	if (EnemyDirectionTextBlock)
	{
		EnemyDirectionTextBlock->SetText(NSLOCTEXT("ArmyDeployment", "EnemyDirection", "ENEMY SIDE  ->"));
	}

	if (PlayerDirectionTextBlock)
	{
		PlayerDirectionTextBlock->SetText(NSLOCTEXT("ArmyDeployment", "PlayerDirection", "<-  YOUR DEPLOYMENT"));
	}
}

void UArmyDeploymentWidget::RefreshAllVisuals()
{
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

	// Prefer the same deployment pattern that HexGridActor uses by default.
	// This stops the preview from putting all units into one ugly top row.
	const FIntPoint PreferredCoords[] =
	{
		FIntPoint(-4, 0),
		FIntPoint(-6, 2),
		FIntPoint(-4, -2),
		FIntPoint(-2, -4),
		FIntPoint(-6, 4)
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

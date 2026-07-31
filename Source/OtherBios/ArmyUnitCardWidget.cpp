#include "ArmyUnitCardWidget.h"

#include "ArmyBuilderWidget.h"
#include "ArmyDeploymentWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"

void UArmyUnitCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BindButton();
	BindUpgradeButton();
	RefreshCardVisuals();
}

void UArmyUnitCardWidget::InitializeCard(UArmyBuilderWidget* InOwnerArmyBuilder, TSubclassOf<AHexUnitActor> InUnitClass)
{
	OwnerArmyBuilder = InOwnerArmyBuilder;
	OwnerDeploymentWidget = nullptr;
	UnitClass = InUnitClass;

	bRemoveFromArmyOnClick = false;
	bSelectForDeploymentOnClick = false;
	bDeploymentSelected = false;
	bUpgradePreviewActive = false;

	SelectedArmyIndex = INDEX_NONE;
	DeploymentUnitIndex = INDEX_NONE;
	bCanBeAdded = true;

	BindButton();
	BindUpgradeButton();
	RefreshCardVisuals();
}

void UArmyUnitCardWidget::InitializeSelectedCard(UArmyBuilderWidget* InOwnerArmyBuilder, TSubclassOf<AHexUnitActor> InUnitClass, int32 InSelectedArmyIndex)
{
	OwnerArmyBuilder = InOwnerArmyBuilder;
	OwnerDeploymentWidget = nullptr;
	UnitClass = InUnitClass;

	bRemoveFromArmyOnClick = true;
	bSelectForDeploymentOnClick = false;
	bDeploymentSelected = false;
	bUpgradePreviewActive = false;

	SelectedArmyIndex = InSelectedArmyIndex;
	DeploymentUnitIndex = INDEX_NONE;
	bCanBeAdded = true;

	BindButton();
	BindUpgradeButton();
	RefreshCardVisuals();
}

void UArmyUnitCardWidget::InitializeDeploymentPickCard(UArmyDeploymentWidget* InOwnerDeploymentWidget, TSubclassOf<AHexUnitActor> InUnitClass, int32 InDeploymentUnitIndex)
{
	OwnerArmyBuilder = nullptr;
	OwnerDeploymentWidget = InOwnerDeploymentWidget;
	UnitClass = InUnitClass;

	bRemoveFromArmyOnClick = false;
	bSelectForDeploymentOnClick = true;
	bDeploymentSelected = false;
	bUpgradePreviewActive = false;

	SelectedArmyIndex = INDEX_NONE;
	DeploymentUnitIndex = InDeploymentUnitIndex;
	bCanBeAdded = true;

	BindButton();
	BindUpgradeButton();
	RefreshCardVisuals();
}

void UArmyUnitCardWidget::SetCanBeAdded(bool bInCanBeAdded)
{
	bCanBeAdded = bInCanBeAdded;
	UpdateInteractionVisuals();
}

void UArmyUnitCardWidget::SetDeploymentSelected(bool bInDeploymentSelected)
{
	bDeploymentSelected = bInDeploymentSelected;
	UpdateInteractionVisuals();
}

TSubclassOf<AHexUnitActor> UArmyUnitCardWidget::GetUnitClass() const
{
	return UnitClass;
}

void UArmyUnitCardWidget::HandleCardClicked()
{
	if (!bRemoveFromArmyOnClick && !bSelectForDeploymentOnClick && !bCanBeAdded)
	{
		return;
	}

	if (!UnitClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Army unit card click blocked: UnitClass is null."));
		return;
	}

	if (bSelectForDeploymentOnClick)
	{
		if (!OwnerDeploymentWidget)
		{
			UE_LOG(LogTemp, Warning, TEXT("Army deployment card click blocked: OwnerDeploymentWidget is null."));
			return;
		}

		OwnerDeploymentWidget->SelectUnitForDeployment(DeploymentUnitIndex);
		return;
	}

	if (!OwnerArmyBuilder)
	{
		UE_LOG(LogTemp, Warning, TEXT("Army unit card click blocked: OwnerArmyBuilder is null."));
		return;
	}

	if (bRemoveFromArmyOnClick)
	{
		OwnerArmyBuilder->RemoveSelectedUnitAt(SelectedArmyIndex);
		return;
	}

	OwnerArmyBuilder->TryAddUnitClass(UnitClass);
}

void UArmyUnitCardWidget::BindButton()
{
	if (!RootButton)
	{
		return;
	}

	RootButton->OnClicked.RemoveDynamic(this, &UArmyUnitCardWidget::HandleCardClicked);
	RootButton->OnClicked.AddDynamic(this, &UArmyUnitCardWidget::HandleCardClicked);

	UpdateInteractionVisuals();
}

void UArmyUnitCardWidget::BindUpgradeButton()
{
	if (!UpgradeButton)
	{
		return;
	}

	UpgradeButton->OnClicked.RemoveDynamic(this, &UArmyUnitCardWidget::HandleUpgradeClicked);
	UpgradeButton->OnClicked.AddDynamic(this, &UArmyUnitCardWidget::HandleUpgradeClicked);
}

void UArmyUnitCardWidget::HandleUpgradeClicked()
{
	if (!OwnerArmyBuilder || !UnitClass || bSelectForDeploymentOnClick)
	{
		return;
	}

	// First click only previews the next-level stats. No XP, coins or saved data change here.
	if (!bUpgradePreviewActive)
	{
		if (!OwnerArmyBuilder->CanUpgradeUnitClass(UnitClass))
		{
			RefreshCardVisuals();
			return;
		}

		bUpgradePreviewActive = true;
		RefreshCardVisuals();
		return;
	}

	// Second click confirms the purchase. The builder validates everything again.
	bUpgradePreviewActive = false;
	if (!OwnerArmyBuilder->TryUpgradeUnitClass(UnitClass))
	{
		RefreshCardVisuals();
	}
}

void UArmyUnitCardWidget::RefreshCardVisuals()
{
	const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	if (!DefaultUnit)
	{
		if (UnitNameTextBlock)
		{
			UnitNameTextBlock->SetText(NSLOCTEXT("ArmyUnitCard", "MissingUnit", "Missing Unit"));
		}

		if (UnitPortraitImage)
		{
			UnitPortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UpgradeButton)
		{
			UpgradeButton->SetVisibility(ESlateVisibility::Collapsed);
		}

		UpdateInteractionVisuals();
		return;
	}

	int32 DisplayLevel = 1;
	int32 DisplayExperience = 0;
	GetCardProgressionState(DisplayLevel, DisplayExperience);

	const bool bMaxLevel = DisplayLevel >= AHexUnitActor::GetMaxProgressionLevel();
	const bool bCanStillPreviewUpgrade = !bMaxLevel
		&& OwnerArmyBuilder
		&& OwnerArmyBuilder->CanUpgradeUnitClass(UnitClass);

	if (bUpgradePreviewActive && !bCanStillPreviewUpgrade)
	{
		bUpgradePreviewActive = false;
	}

	const int32 PreviewLevel = bUpgradePreviewActive ? DisplayLevel + 1 : DisplayLevel;
	const int32 CurrentHealth = DefaultUnit->GetScaledMaxHealthForLevel(DisplayLevel);
	const int32 CurrentAttack = DefaultUnit->GetScaledAttackDamageForLevel(DisplayLevel);
	const int32 CurrentPower = DefaultUnit->GetArmyPowerValueForLevel(DisplayLevel);
	const int32 PreviewHealth = DefaultUnit->GetScaledMaxHealthForLevel(PreviewLevel);
	const int32 PreviewAttack = DefaultUnit->GetScaledAttackDamageForLevel(PreviewLevel);
	const int32 PreviewPower = DefaultUnit->GetArmyPowerValueForLevel(PreviewLevel);
	const int32 RequiredExperience = AHexUnitActor::GetExperienceToNextLevelForLevel(DisplayLevel);

	if (UnitNameTextBlock)
	{
		const FString DisplayName = DefaultUnit->UnitDisplayName.IsEmpty()
			? GetNameSafe(UnitClass.Get())
			: DefaultUnit->UnitDisplayName;

		UnitNameTextBlock->SetText(FText::FromString(DisplayName));
	}

	if (HealthTextBlock)
	{
		const int32 HealthIncrease = FMath::Max(0, PreviewHealth - CurrentHealth);
		HealthTextBlock->SetText(bUpgradePreviewActive
			? FText::FromString(FString::Printf(TEXT("HP %d + %d"), CurrentHealth, HealthIncrease))
			: FText::Format(NSLOCTEXT("ArmyUnitCard", "HealthFormat", "HP {0}"), FText::AsNumber(CurrentHealth)));
	}

	if (AttackTextBlock)
	{
		const int32 AttackIncrease = FMath::Max(0, PreviewAttack - CurrentAttack);
		AttackTextBlock->SetText(bUpgradePreviewActive
			? FText::FromString(FString::Printf(TEXT("ATK %d + %d"), CurrentAttack, AttackIncrease))
			: FText::Format(NSLOCTEXT("ArmyUnitCard", "AttackFormat", "ATK {0}"), FText::AsNumber(CurrentAttack)));
	}

	if (MovementRangeTextBlock)
	{
		MovementRangeTextBlock->SetText(FText::Format(
			NSLOCTEXT("ArmyUnitCard", "MovementRangeFormat", "MOVE {0}"),
			FText::AsNumber(FMath::Max(0, DefaultUnit->MovementRange))
		));
	}

	if (TypeTextBlock)
	{
		TypeTextBlock->SetText(GetUnitTypeText(DefaultUnit->UnitType));
	}

	if (FactionTextBlock)
	{
		FactionTextBlock->SetText(GetFactionText(DefaultUnit->Faction));
	}

	if (SlotCostTextBlock)
	{
		SlotCostTextBlock->SetText(FText::AsNumber(FMath::Max(0, DefaultUnit->OccupiedSlots)));
	}

	if (PowerBadgeOverlay)
	{
		PowerBadgeOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (PowerBadgeImage)
	{
		PowerBadgeImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (PowerTextBlock)
	{
		PowerTextBlock->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		const int32 PowerIncrease = FMath::Max(0, PreviewPower - CurrentPower);
		PowerTextBlock->SetText(bUpgradePreviewActive
			? FText::FromString(FString::Printf(TEXT("%d+%d"), CurrentPower, PowerIncrease))
			: FText::AsNumber(CurrentPower));
	}

	if (LevelBadgeOverlay)
	{
		LevelBadgeOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (LevelBadgeImage)
	{
		LevelBadgeImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (LevelTextBlock)
	{
		LevelTextBlock->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		LevelTextBlock->SetText(FText::AsNumber(DisplayLevel));
	}

	if (ExperienceTextBlock)
	{
		if (bMaxLevel)
		{
			ExperienceTextBlock->SetText(NSLOCTEXT("ArmyUnitCard", "ExperienceMax", "EXP MAX"));
		}
		else
		{
			ExperienceTextBlock->SetText(FText::FromString(FString::Printf(
				TEXT("EXP %d / %d"),
				FMath::Max(0, DisplayExperience),
				RequiredExperience
			)));
		}
	}

	if (ExperienceProgressBar)
	{
		const float ProgressPercent = bMaxLevel || RequiredExperience <= 0
			? 1.0f
			: FMath::Clamp(static_cast<float>(DisplayExperience) / static_cast<float>(RequiredExperience), 0.0f, 1.0f);

		ExperienceProgressBar->SetPercent(ProgressPercent);
	}

	if (UnitPortraitImage)
	{
		if (DefaultUnit->UnitPortrait)
		{
			UnitPortraitImage->SetBrushFromTexture(DefaultUnit->UnitPortrait, true);
			UnitPortraitImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			UnitPortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	RefreshUpgradeVisuals(DisplayLevel, DisplayExperience, RequiredExperience, bMaxLevel);
	ApplyCardLayout();
	UpdateInteractionVisuals();
}

void UArmyUnitCardWidget::RefreshUpgradeVisuals(int32 DisplayLevel, int32 DisplayExperience, int32 RequiredExperience, bool bMaxLevel)
{
	const bool bShowUpgrade = OwnerArmyBuilder != nullptr && UnitClass != nullptr && !bSelectForDeploymentOnClick;
	if (UpgradeButton)
	{
		UpgradeButton->SetVisibility(bShowUpgrade ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (!bShowUpgrade)
	{
		return;
	}

	const int32 UpgradeCost = AHexUnitActor::GetUpgradeCoinCostForLevel(DisplayLevel);
	const bool bHasEnoughExperience = !bMaxLevel && RequiredExperience > 0 && DisplayExperience >= RequiredExperience;
	const bool bHasEnoughCoins = !bMaxLevel && UpgradeCost > 0 && UArmyBuilderWidget::GetSavedCoins() >= UpgradeCost;
	const bool bCanUpgrade = bHasEnoughExperience && bHasEnoughCoins;

	if (UpgradeTextBlock)
	{
		UpgradeTextBlock->SetText(bMaxLevel
			? NSLOCTEXT("ArmyUnitCard", "MaximumLevel", "MAX LEVEL")
			: (bUpgradePreviewActive
				? NSLOCTEXT("ArmyUnitCard", "ConfirmUpgrade", "CONFIRM")
				: NSLOCTEXT("ArmyUnitCard", "Upgrade", "UPGRADE")));
		UpgradeTextBlock->SetColorAndOpacity(FSlateColor(UpgradeTextColor));
	}

	if (UpgradePriceTextBlock)
	{
		UpgradePriceTextBlock->SetText(bMaxLevel ? FText::GetEmpty() : FText::AsNumber(UpgradeCost));
		UpgradePriceTextBlock->SetColorAndOpacity(FSlateColor(
			!bMaxLevel && !bHasEnoughCoins ? UpgradeMissingCoinsColor : UpgradeTextColor
		));
		UpgradePriceTextBlock->SetVisibility(bMaxLevel ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (UpgradeCoinImage)
	{
		UpgradeCoinImage->SetVisibility(bMaxLevel ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (UpgradeButton)
	{
		UpgradeButton->SetIsEnabled(bCanUpgrade);
		UpgradeButton->SetBackgroundColor(bCanUpgrade ? UpgradeReadyColor : UpgradeLockedColor);
		UpgradeButton->SetRenderOpacity(bMaxLevel ? 0.72f : (bCanUpgrade ? 1.0f : 0.62f));

		if (bMaxLevel)
		{
			UpgradeButton->SetToolTipText(NSLOCTEXT("ArmyUnitCard", "MaximumLevelTooltip", "Maximum level reached"));
		}
		else if (!bHasEnoughExperience)
		{
			const int32 MissingExperience = FMath::Max(0, RequiredExperience - DisplayExperience);
			UpgradeButton->SetToolTipText(FText::Format(
				NSLOCTEXT("ArmyUnitCard", "MissingExperienceTooltip", "Need {0} more EXP"),
				FText::AsNumber(MissingExperience)
			));
		}
		else if (!bHasEnoughCoins)
		{
			const int32 MissingCoins = FMath::Max(0, UpgradeCost - UArmyBuilderWidget::GetSavedCoins());
			UpgradeButton->SetToolTipText(FText::Format(
				NSLOCTEXT("ArmyUnitCard", "MissingCoinsTooltip", "Need {0} more coins"),
				FText::AsNumber(MissingCoins)
			));
		}
		else
		{
			UpgradeButton->SetToolTipText(bUpgradePreviewActive
				? NSLOCTEXT("ArmyUnitCard", "ConfirmUpgradeTooltip", "Click again to confirm the upgrade")
				: FText::Format(
					NSLOCTEXT("ArmyUnitCard", "UpgradeTooltip", "Preview upgrade to level {0}"),
					FText::AsNumber(DisplayLevel + 1)
				));
		}
	}
}

void UArmyUnitCardWidget::UpdateInteractionVisuals()
{
	const bool bInteractive = bRemoveFromArmyOnClick || bSelectForDeploymentOnClick || bCanBeAdded;

	if (RootButton)
	{
		RootButton->SetIsEnabled(bInteractive);
	}

	if (DeploymentSelectedOverlay)
	{
		DeploymentSelectedOverlay->SetVisibility(bDeploymentSelected ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bDeploymentSelected)
	{
		SetRenderOpacity(1.0f);
		SetRenderScale(FVector2D(1.035f, 1.035f));
	}
	else
	{
		SetRenderOpacity(bInteractive ? 1.0f : 0.45f);
		SetRenderScale(FVector2D(1.0f, 1.0f));
	}
}

void UArmyUnitCardWidget::ApplyCardLayout()
{
	if (bRemoveFromArmyOnClick)
	{
		ApplySelectedArmyCardLayout();
	}
	else
	{
		ApplyAvailableCardLayout();
	}
}

void UArmyUnitCardWidget::ApplyAvailableCardLayout()
{
	// В окне расстановки карточка остаётся компактной,
	// а кнопка улучшения там не показывается.
	if (bSelectForDeploymentOnClick || !OwnerArmyBuilder)
	{
		return;
	}

	constexpr float UpgradeGap = 3.0f;
	constexpr float UpgradeButtonHeight = 34.0f;
	constexpr float BottomPadding = 5.0f;

	// Растягиваем фон на всю новую высоту карточки.
	StretchCanvasSlotToParent(CardBackgroundImage, 0);

	if (const UCanvasPanelSlot* LeftStatsSlot =
		LeftStatsBox ? Cast<UCanvasPanelSlot>(LeftStatsBox->Slot) : nullptr)
	{
		const FVector2D LeftPosition = LeftStatsSlot->GetPosition();
		const FVector2D LeftSize = LeftStatsSlot->GetSize();

		const float UpgradeButtonY =
			LeftPosition.Y +
			LeftSize.Y +
			UpgradeGap;

		SetCanvasSlotPositionAndSize(
			UpgradeButton,
			FVector2D(
				LeftPosition.X,
				UpgradeButtonY
			),
			FVector2D(
				LeftSize.X,
				UpgradeButtonHeight
			),
			30
		);

		// Высота карточки рассчитывается по фактическому положению кнопки.
		if (RootSizeBox)
		{
			RootSizeBox->SetHeightOverride(
				UpgradeButtonY +
				UpgradeButtonHeight +
				BottomPadding
			);
		}
	}
}

void UArmyUnitCardWidget::ApplySelectedArmyCardLayout()
{
	// Левая часть: Your Army.
	// Ширину не фиксируем — карточку растягивает родительский VerticalBox/ScrollBox.
	if (RootSizeBox)
	{
		RootSizeBox->ClearWidthOverride();
		RootSizeBox->SetHeightOverride(195.0f);
	}

	// Задний фон растягивается на всю карточку.
	StretchCanvasSlotToParent(CardBackgroundImage, 0);

	// Портрет.
	SetCanvasSlotPositionAndSize(
		UnitPortraitImage,
		FVector2D(10.0f, 12.0f),
		FVector2D(140.0f, 105.0f),
		1
	);

	// Значок силы.
	if (PowerBadgeOverlay)
	{
		SetCanvasSlotPositionAndSize(
			PowerBadgeOverlay,
			FVector2D(18.0f, 96.0f),
			FVector2D(42.0f, 20.0f),
			20
		);
	}
	else
	{
		SetCanvasSlotPositionAndSize(
			PowerBadgeImage,
			FVector2D(16.0f, 96.0f),
			FVector2D(42.0f, 20.0f),
			20
		);

		SetCanvasSlotPositionAndSize(
			PowerTextBlock,
			FVector2D(16.0f, 96.0f),
			FVector2D(42.0f, 20.0f),
			21
		);
	}

	// Значок уровня.
	if (LevelBadgeOverlay)
	{
		SetCanvasSlotPositionAndSize(
			LevelBadgeOverlay,
			FVector2D(121.0f, 96.0f),
			FVector2D(24.0f, 20.0f),
			20
		);
	}
	else
	{
		SetCanvasSlotPositionAndSize(
			LevelBadgeImage,
			FVector2D(125.0f, 96.0f),
			FVector2D(24.0f, 20.0f),
			20
		);

		SetCanvasSlotPositionAndSize(
			LevelTextBlock,
			FVector2D(125.0f, 96.0f),
			FVector2D(24.0f, 20.0f),
			21
		);
	}

	// Опыт.
	SetCanvasSlotPositionAndSize(
		ExperienceProgressBar,
		FVector2D(10.0f, 124.0f),
		FVector2D(140.0f, 6.0f),
		10
	);

	SetCanvasSlotPositionAndSize(
		ExperienceTextBlock,
		FVector2D(10.0f, 132.0f),
		FVector2D(140.0f, 12.0f),
		10
	);

	// Имя персонажа.
	SetCanvasSlotPositionAndSize(
		UnitNameTextBlock,
		FVector2D(160.0f, 18.0f),
		FVector2D(108.0f, 28.0f),
		10
	);

	// Левый столбец: HP, ATK, Slots.
	SetCanvasSlotPositionAndSize(
		LeftStatsBox,
		FVector2D(160.0f, 58.0f),
		FVector2D(55.0f, 80.0f),
		10
	);

	// Правый столбец: Faction, Type.
	// Сдвинут левее и имеет ту же ширину, что и левый столбец.
	SetCanvasSlotPositionAndSize(
		RightStatsBox,
		FVector2D(215.0f, 58.0f),
		FVector2D(55.0f, 80.0f),
		10
	);

	// Кнопка улучшения под левым столбцом.
	// Место под правым остаётся свободным для будущей кнопки подробностей.
	SetCanvasSlotPositionAndSize(
		UpgradeButton,
		FVector2D(160.0f, 143.0f),
		FVector2D(95.0f, 42.0f),
		30
	);
}

void UArmyUnitCardWidget::SetCanvasSlotPositionAndSize(UWidget* Widget, const FVector2D& Position, const FVector2D& Size, int32 ZOrder) const
{
	if (!Widget)
	{
		return;
	}

	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!CanvasSlot)
	{
		return;
	}

	CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
	CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetPosition(Position);
	CanvasSlot->SetSize(Size);
	CanvasSlot->SetZOrder(ZOrder);
}

void UArmyUnitCardWidget::StretchCanvasSlotToParent(UWidget* Widget, int32 ZOrder) const
{
	if (!Widget)
	{
		return;
	}

	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!CanvasSlot)
	{
		return;
	}

	CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
	CanvasSlot->SetZOrder(ZOrder);
}

void UArmyUnitCardWidget::GetCardProgressionState(int32& OutLevel, int32& OutCurrentExperience) const
{
	OutLevel = 1;
	OutCurrentExperience = 0;

	if (bRemoveFromArmyOnClick && OwnerArmyBuilder && SelectedArmyIndex != INDEX_NONE)
	{
		const FArmyBuilderUnitProgress Progress = OwnerArmyBuilder->GetSelectedUnitProgressAt(SelectedArmyIndex);
		OutLevel = FMath::Clamp(Progress.Level, 1, AHexUnitActor::GetMaxProgressionLevel());
		OutCurrentExperience = FMath::Max(0, Progress.CurrentExperience);
		return;
	}

	if (OwnerArmyBuilder && UnitClass)
	{
		const FArmyBuilderUnitProgress Progress = OwnerArmyBuilder->GetKnownUnitProgressForClass(UnitClass);
		OutLevel = FMath::Clamp(Progress.Level, 1, AHexUnitActor::GetMaxProgressionLevel());
		OutCurrentExperience = FMath::Max(0, Progress.CurrentExperience);
		return;
	}

	if (const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr)
	{
		OutLevel = 1;
		OutCurrentExperience = 0;
	}
}

FText UArmyUnitCardWidget::GetUnitTypeText(EHexUnitType UnitType) const
{
	switch (UnitType)
	{
	case EHexUnitType::Champion:
		return NSLOCTEXT("ArmyUnitCard", "TypeChampion", "Champion");

	case EHexUnitType::Ram:
		return NSLOCTEXT("ArmyUnitCard", "TypeRam", "Ram");

	case EHexUnitType::Skirmisher:
		return NSLOCTEXT("ArmyUnitCard", "TypeSkirmisher", "Skirmisher");

	case EHexUnitType::Support:
		return NSLOCTEXT("ArmyUnitCard", "TypeSupport", "Support");

	case EHexUnitType::Healer:
		return NSLOCTEXT("ArmyUnitCard", "TypeHealer", "Healer");

	default:
		return NSLOCTEXT("ArmyUnitCard", "TypeUnknown", "Unknown");
	}
}

FText UArmyUnitCardWidget::GetFactionText(EHexUnitFaction Faction) const
{
	switch (Faction)
	{
	case EHexUnitFaction::Kingdom:
		return NSLOCTEXT("ArmyUnitCard", "FactionKingdom", "Kingdom");

	case EHexUnitFaction::Animal:
		return NSLOCTEXT("ArmyUnitCard", "FactionAnimals", "Animals");

	case EHexUnitFaction::Soul:
		return NSLOCTEXT("ArmyUnitCard", "FactionSouls", "Souls");

	case EHexUnitFaction::Bandits:
		return NSLOCTEXT("ArmyUnitCard", "FactionBandits", "Bandits");

	default:
		return NSLOCTEXT("ArmyUnitCard", "FactionUnknown", "Unknown");
	}
}
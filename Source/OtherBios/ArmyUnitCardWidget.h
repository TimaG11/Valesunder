#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HexUnitActor.h"
#include "ArmyUnitCardWidget.generated.h"

class UArmyBuilderWidget;
class UArmyDeploymentWidget;
class UButton;
class UCanvasPanel;
class UImage;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UWidget;

UCLASS()
class OTHERBIOS_API UArmyUnitCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Army Unit Card")
	void InitializeCard(UArmyBuilderWidget* InOwnerArmyBuilder, TSubclassOf<AHexUnitActor> InUnitClass);

	UFUNCTION(BlueprintCallable, Category = "Army Unit Card")
	void InitializeSelectedCard(UArmyBuilderWidget* InOwnerArmyBuilder, TSubclassOf<AHexUnitActor> InUnitClass, int32 InSelectedArmyIndex);

	UFUNCTION(BlueprintCallable, Category = "Army Unit Card")
	void InitializeDeploymentPickCard(UArmyDeploymentWidget* InOwnerDeploymentWidget, TSubclassOf<AHexUnitActor> InUnitClass, int32 InDeploymentUnitIndex);

	UFUNCTION(BlueprintCallable, Category = "Army Unit Card")
	void SetCanBeAdded(bool bInCanBeAdded);

	UFUNCTION(BlueprintCallable, Category = "Army Unit Card")
	void SetDeploymentSelected(bool bInDeploymentSelected);

	UFUNCTION(BlueprintPure, Category = "Army Unit Card")
	TSubclassOf<AHexUnitActor> GetUnitClass() const;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	USizeBox* RootSizeBox = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* RootButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCanvasPanel* CardCanvas = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UImage* CardBackgroundImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UImage* UnitPortraitImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* UnitNameTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* HealthTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* AttackTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* MovementRangeTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TypeTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* FactionTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* SlotCostTextBlock = nullptr;

	// Overlay for power badge. In WBP it should contain PowerBadgeImage + PowerTextBlock.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PowerBadgeOverlay = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PowerBadgeImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* PowerTextBlock = nullptr;

	// Overlay for level badge. In WBP it should contain LevelBadgeImage + LevelTextBlock.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* LevelBadgeOverlay = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* LevelBadgeImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LevelTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* ExperienceProgressBar = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ExperienceTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* LeftStatsBox = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* RightStatsBox = nullptr;

	// Put this button in a separate InteractionCanvas layered above RootButton.
	// Its content should be a VerticalBox: UPGRADE on the first line, coin + price below.
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* UpgradeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* UpgradeTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UImage* UpgradeCoinImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* UpgradePriceTextBlock = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Unit Card|Upgrade")
	FLinearColor UpgradeReadyColor = FLinearColor(1.0f, 0.72f, 0.04f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Unit Card|Upgrade")
	FLinearColor UpgradeLockedColor = FLinearColor(0.30f, 0.30f, 0.30f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Unit Card|Upgrade")
	FLinearColor UpgradeTextColor = FLinearColor(0.06f, 0.04f, 0.01f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Unit Card|Upgrade")
	FLinearColor UpgradeMissingCoinsColor = FLinearColor(1.0f, 0.18f, 0.12f, 1.0f);

	// Optional in WBP_ArmyUnitCard. Shows which unit is currently selected in deployment mode.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* DeploymentSelectedOverlay = nullptr;

private:
	UPROPERTY()
	UArmyBuilderWidget* OwnerArmyBuilder = nullptr;

	UPROPERTY()
	UArmyDeploymentWidget* OwnerDeploymentWidget = nullptr;

	UPROPERTY()
	TSubclassOf<AHexUnitActor> UnitClass;

	bool bCanBeAdded = true;
	bool bRemoveFromArmyOnClick = false;
	bool bSelectForDeploymentOnClick = false;
	bool bDeploymentSelected = false;
	bool bUpgradePreviewActive = false;

	int32 SelectedArmyIndex = INDEX_NONE;
	int32 DeploymentUnitIndex = INDEX_NONE;

	UFUNCTION()
	void HandleCardClicked();

	UFUNCTION()
	void HandleUpgradeClicked();

	void BindButton();
	void BindUpgradeButton();
	void RefreshCardVisuals();
	void RefreshUpgradeVisuals(int32 DisplayLevel, int32 DisplayExperience, int32 RequiredExperience, bool bMaxLevel);
	void UpdateInteractionVisuals();
	void ApplyCardLayout();

	// Right-side available cards use WBP layout.
	void ApplyAvailableCardLayout();

	// Left-side selected army cards use C++ wide layout.
	void ApplySelectedArmyCardLayout();

	void SetCanvasSlotPositionAndSize(UWidget* Widget, const FVector2D& Position, const FVector2D& Size, int32 ZOrder = 1) const;
	void StretchCanvasSlotToParent(UWidget* Widget, int32 ZOrder = 0) const;

	void GetCardProgressionState(int32& OutLevel, int32& OutCurrentExperience) const;

	FText GetUnitTypeText(EHexUnitType UnitType) const;
	FText GetFactionText(EHexUnitFaction Faction) const;
};
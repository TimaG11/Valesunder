#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UImage;
class UProgressBar;
class UTextBlock;
class UWidget;
class UGameModeSelectWidget;
class UArmyBuilderWidget;

UCLASS()
class OTHERBIOS_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ReturnFromGameModeSelect();
	void ReturnFromArmyBuilder();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* PlayButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ArmyButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* HeroesButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ShopButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SettingsButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* HeroesButtonText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ShopButtonText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* SettingsButtonText = nullptr;

	// Top profile panel.
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* AvatarImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* PlayerNameText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* PlayerLevelText = nullptr;

	// Optional. Add ProgressBar to WBP and name it exactly PlayerLevelProgressBar if you want it controlled from C++.
	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* PlayerLevelProgressBar = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* FakePowerText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* FakeGoldText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* FakeCrystalText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* FakeMailIcon = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* FakeCupIcon = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* FakeSettingIcon = nullptr;

	// Old overlay is kept optional so the current main menu WBP does not break.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* LoadingOverlayRoot = nullptr;

	// Assign WBP_GameModeSelect here in the main menu blueprint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Navigation")
	TSubclassOf<UGameModeSelectWidget> GameModeSelectWidgetClass;

	// Assign WBP_ArmyBuilder here in the main menu blueprint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Navigation")
	TSubclassOf<UArmyBuilderWidget> ArmyBuilderWidgetClass;

	// This level name is passed to WBP_GameModeSelect. The main menu no longer opens battle directly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Navigation")
	FName BattleLevelName = TEXT("L_Battle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Loading", meta = (ClampMin = "0.0"))
	float OpenLevelDelayAfterPlay = 0.15f;

	UFUNCTION()
	void HandlePlayClicked();

	UFUNCTION()
	void HandleArmyClicked();

	UFUNCTION()
	void HandleExitClicked();

	void DisableMenuButtons();
	void RestoreMenuButtons();
	void RefreshTopPanelData();
	void HideUnusedTopPanelWidgets();

private:
	UPROPERTY()
	UGameModeSelectWidget* GameModeSelectWidget = nullptr;

	UPROPERTY()
	UArmyBuilderWidget* ArmyBuilderWidget = nullptr;
};

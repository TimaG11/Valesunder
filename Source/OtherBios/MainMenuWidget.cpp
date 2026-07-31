#include "MainMenuWidget.h"

#include "GameLoadingGameInstance.h"
#include "ArmyBuilderWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"
#include "GameModeSelectWidget.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/KismetSystemLibrary.h"

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(this);
	}

	if (HeroesButtonText)
	{
		HeroesButtonText->SetText(FText::FromString(TEXT("Book of Heroes")));
	}

	if (ShopButtonText)
	{
		ShopButtonText->SetText(FText::FromString(TEXT("Settings")));
	}

	if (SettingsButtonText)
	{
		SettingsButtonText->SetText(FText::FromString(TEXT("Quit Game")));
	}

	RefreshTopPanelData();
	HideUnusedTopPanelWidgets();

	if (PlayButton)
	{
		PlayButton->OnClicked.RemoveDynamic(this, &UMainMenuWidget::HandlePlayClicked);
		PlayButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandlePlayClicked);
		PlayButton->SetIsEnabled(true);
	}

	if (ArmyButton)
	{
		ArmyButton->OnClicked.RemoveDynamic(this, &UMainMenuWidget::HandleArmyClicked);
		ArmyButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleArmyClicked);
		ArmyButton->SetIsEnabled(true);
	}

	if (HeroesButton)
	{
		HeroesButton->SetIsEnabled(false);
	}

	if (ShopButton)
	{
		// Settings screen is not implemented yet, so this renamed button stays disabled for now.
		ShopButton->SetIsEnabled(false);
	}

	if (SettingsButton)
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &UMainMenuWidget::HandleExitClicked);
		SettingsButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleExitClicked);
		SettingsButton->SetIsEnabled(true);
	}

	if (LoadingOverlayRoot)
	{
		LoadingOverlayRoot->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMainMenuWidget::RefreshTopPanelData()
{
	if (AvatarImage)
	{
		// Keep the avatar slot, but make the image itself empty for now.
		AvatarImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
	}

	if (PlayerNameText)
	{
		PlayerNameText->SetText(FText::GetEmpty());
	}

	if (PlayerLevelText)
	{
		PlayerLevelText->SetText(FText::FromString(TEXT("Level 1")));
	}

	if (PlayerLevelProgressBar)
	{
		PlayerLevelProgressBar->SetPercent(0.0f);
	}

	const int32 ArmyPowerValue = UArmyBuilderWidget::GetLastPreviewArmyPower();

	if (FakePowerText)
	{
		FakePowerText->SetText(FText::Format(
			NSLOCTEXT("MainMenu", "ArmyPowerFormat", "Army Power {0}"),
			FText::AsNumber(ArmyPowerValue)
		));
	}

	if (FakeGoldText)
	{
		FakeGoldText->SetText(FText::Format(
			NSLOCTEXT("MainMenu", "CoinsFormat", "Coins {0}"),
			FText::AsNumber(UArmyBuilderWidget::GetSavedCoins())
		));
	}

	if (FakeCrystalText)
	{
		FakeCrystalText->SetText(NSLOCTEXT("MainMenu", "CrystalsZero", "Crystals 0"));
	}
}

void UMainMenuWidget::HideUnusedTopPanelWidgets()
{
	if (FakeMailIcon)
	{
		FakeMailIcon->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (FakeCupIcon)
	{
		FakeCupIcon->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (FakeSettingIcon)
	{
		FakeSettingIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMainMenuWidget::HandlePlayClicked()
{
	if (!GameModeSelectWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu play failed: GameModeSelectWidgetClass is not set."));
		return;
	}

	DisableMenuButtons();

	UWorld* World = GetWorld();
	if (!World)
	{
		RestoreMenuButtons();
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		PlayerController = World->GetFirstPlayerController();
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu play failed: PlayerController is null."));
		RestoreMenuButtons();
		return;
	}

	GameModeSelectWidget = CreateWidget<UGameModeSelectWidget>(PlayerController, GameModeSelectWidgetClass);
	if (!GameModeSelectWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu play failed: failed to create GameModeSelectWidget."));
		RestoreMenuButtons();
		return;
	}

	GameModeSelectWidget->SetParentMainMenu(this);
	GameModeSelectWidget->SetBattleLevelName(BattleLevelName);
	GameModeSelectWidget->SetOpenBattleLevelDelay(OpenLevelDelayAfterPlay);
	GameModeSelectWidget->AddToViewport(1);

	SetVisibility(ESlateVisibility::Collapsed);

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(GameModeSelectWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}

void UMainMenuWidget::HandleArmyClicked()
{
	if (!ArmyBuilderWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu army failed: ArmyBuilderWidgetClass is not set."));
		return;
	}

	DisableMenuButtons();

	UWorld* World = GetWorld();
	if (!World)
	{
		RestoreMenuButtons();
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		PlayerController = World->GetFirstPlayerController();
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu army failed: PlayerController is null."));
		RestoreMenuButtons();
		return;
	}

	ArmyBuilderWidget = CreateWidget<UArmyBuilderWidget>(PlayerController, ArmyBuilderWidgetClass);
	if (!ArmyBuilderWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu army failed: failed to create ArmyBuilderWidget."));
		RestoreMenuButtons();
		return;
	}

	ArmyBuilderWidget->SetParentMainMenu(this);
	ArmyBuilderWidget->AddToViewport(1);

	SetVisibility(ESlateVisibility::Collapsed);

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ArmyBuilderWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}

void UMainMenuWidget::HandleExitClicked()
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		if (UWorld* World = GetWorld())
		{
			PlayerController = World->GetFirstPlayerController();
		}
	}

	if (PlayerController)
	{
		UKismetSystemLibrary::QuitGame(this, PlayerController, EQuitPreference::Quit, false);
		return;
	}

	// Fallback. Useful if the widget somehow has no owning player.
	FGenericPlatformMisc::RequestExit(false);
}

void UMainMenuWidget::ReturnFromGameModeSelect()
{
	GameModeSelectWidget = nullptr;

	SetVisibility(ESlateVisibility::Visible);
	RestoreMenuButtons();
	RefreshTopPanelData();
	HideUnusedTopPanelWidgets();

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		if (UWorld* World = GetWorld())
		{
			PlayerController = World->GetFirstPlayerController();
		}
	}

	if (!PlayerController)
	{
		return;
	}

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}

void UMainMenuWidget::ReturnFromArmyBuilder()
{
	ArmyBuilderWidget = nullptr;

	SetVisibility(ESlateVisibility::Visible);
	RestoreMenuButtons();
	RefreshTopPanelData();
	HideUnusedTopPanelWidgets();

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		if (UWorld* World = GetWorld())
		{
			PlayerController = World->GetFirstPlayerController();
		}
	}

	if (!PlayerController)
	{
		return;
	}

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}

void UMainMenuWidget::DisableMenuButtons()
{
	if (PlayButton)
	{
		PlayButton->SetIsEnabled(false);
	}

	if (ArmyButton)
	{
		ArmyButton->SetIsEnabled(false);
	}

	if (HeroesButton)
	{
		HeroesButton->SetIsEnabled(false);
	}

	if (ShopButton)
	{
		ShopButton->SetIsEnabled(false);
	}

	if (SettingsButton)
	{
		SettingsButton->SetIsEnabled(false);
	}
}

void UMainMenuWidget::RestoreMenuButtons()
{
	if (PlayButton)
	{
		PlayButton->SetIsEnabled(true);
	}

	if (ArmyButton)
	{
		ArmyButton->SetIsEnabled(true);
	}

	// These buttons are placeholders for now, so they stay disabled after returning.
	if (HeroesButton)
	{
		HeroesButton->SetIsEnabled(false);
	}

	if (ShopButton)
	{
		ShopButton->SetIsEnabled(false);
	}

	if (SettingsButton)
	{
		SettingsButton->SetIsEnabled(true);
	}
}

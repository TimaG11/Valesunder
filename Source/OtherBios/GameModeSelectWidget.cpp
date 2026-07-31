#include "GameModeSelectWidget.h"

#include "ArmyBuilderWidget.h"
#include "Components/Button.h"
#include "GameLoadingGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "MainMenuPlayerController.h"
#include "MainMenuWidget.h"

EGameModeDifficulty UGameModeSelectWidget::SavedSelectedDifficulty = EGameModeDifficulty::None;

void UGameModeSelectWidget::SetBattleLevelName(FName InBattleLevelName)
{
	BattleLevelName = InBattleLevelName;
}

void UGameModeSelectWidget::SetOpenBattleLevelDelay(float InOpenBattleLevelDelay)
{
	OpenBattleLevelDelay = FMath::Max(0.0f, InOpenBattleLevelDelay);
}

void UGameModeSelectWidget::SetParentMainMenu(UMainMenuWidget* InParentMainMenu)
{
	ParentMainMenu = InParentMainMenu;
}

void UGameModeSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(this);
	}

	SelectedDifficulty = SavedSelectedDifficulty;

	if (BackButton)
	{
		BackButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandleBackClicked);
		BackButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandleBackClicked);
		BackButton->SetIsEnabled(true);
	}

	if (WarmUpButton)
	{
		WarmUpButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandleWarmUpClicked);
		WarmUpButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandleWarmUpClicked);
		WarmUpButton->SetIsEnabled(true);
	}

	if (ChallengeButton)
	{
		ChallengeButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandleChallengeClicked);
		ChallengeButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandleChallengeClicked);
		ChallengeButton->SetIsEnabled(true);
	}

	if (OrdealButton)
	{
		OrdealButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandleOrdealClicked);
		OrdealButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandleOrdealClicked);
		OrdealButton->SetIsEnabled(true);
	}

	if (NightmareButton)
	{
		NightmareButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandleNightmareClicked);
		NightmareButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandleNightmareClicked);
		NightmareButton->SetIsEnabled(true);
	}

	if (PveBattleButton)
	{
		PveBattleButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandlePveBattleClicked);
		PveBattleButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandlePveBattleClicked);
	}

	if (PvpBattleButton)
	{
		PvpBattleButton->OnClicked.RemoveDynamic(this, &UGameModeSelectWidget::HandlePvpBattleClicked);
		PvpBattleButton->OnClicked.AddDynamic(this, &UGameModeSelectWidget::HandlePvpBattleClicked);
		PvpBattleButton->SetIsEnabled(true);
	}

	UpdateDifficultyVisuals();
}

void UGameModeSelectWidget::HandleBackClicked()
{
	UMainMenuWidget* MenuToReturnTo = ParentMainMenu;
	RemoveFromParent();

	if (MenuToReturnTo)
	{
		MenuToReturnTo->ReturnFromGameModeSelect();
	}
}

void UGameModeSelectWidget::HandleWarmUpClicked()
{
	SelectDifficulty(EGameModeDifficulty::WarmUp);
}

void UGameModeSelectWidget::HandleChallengeClicked()
{
	SelectDifficulty(EGameModeDifficulty::Challenge);
}

void UGameModeSelectWidget::HandleOrdealClicked()
{
	SelectDifficulty(EGameModeDifficulty::Ordeal);
}

void UGameModeSelectWidget::HandleNightmareClicked()
{
	SelectDifficulty(EGameModeDifficulty::Nightmare);
}

void UGameModeSelectWidget::HandlePveBattleClicked()
{
	if (SelectedDifficulty == EGameModeDifficulty::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("PVE battle blocked: difficulty is not selected."));
		UpdateDifficultyVisuals();
		return;
	}

	if (!UArmyBuilderWidget::IsSavedPlayerArmyReadyForBattle())
	{
		UE_LOG(LogTemp, Warning, TEXT("PVE battle blocked: save a ready army in Army Builder first."));
		UpdateDifficultyVisuals();
		return;
	}

	PendingBattleLevelName = GetRandomEnabledBotBattleMapLevelName();
	if (PendingBattleLevelName.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("PVE battle failed: no battle map is selected. Set BattleLevelName or add enabled maps to BotBattleMaps."));
		return;
	}

	DisableAllModeButtons();

	if (AMainMenuPlayerController* MenuPlayerController = Cast<AMainMenuPlayerController>(GetOwningPlayer()))
	{
		MenuPlayerController->StopMenuMusic(0.5f);
	}

	// No fake timer here. OpenLevel starts immediately; the GameInstance displays
	// a real MoviePlayer loading screen for the whole blocking map load.
	OpenBattleLevel();
}

void UGameModeSelectWidget::HandlePvpBattleClicked()
{
	UE_LOG(LogTemp, Log, TEXT("PVP battle is not implemented yet."));
}

void UGameModeSelectWidget::SelectDifficulty(EGameModeDifficulty NewDifficulty)
{
	if (SelectedDifficulty == NewDifficulty)
	{
		SelectedDifficulty = EGameModeDifficulty::None;
	}
	else
	{
		SelectedDifficulty = NewDifficulty;
	}

	SavedSelectedDifficulty = SelectedDifficulty;
	UpdateDifficultyVisuals();
}

void UGameModeSelectWidget::UpdateDifficultyVisuals()
{
	const auto ApplyDifficultyButtonState = [this](UButton* Button, EGameModeDifficulty Difficulty)
		{
			if (!Button)
			{
				return;
			}

			const bool bSelected = SelectedDifficulty == Difficulty;
			Button->SetRenderOpacity(bSelected ? SelectedDifficultyOpacity : NormalDifficultyOpacity);
			Button->SetIsEnabled(true);
		};

	ApplyDifficultyButtonState(WarmUpButton, EGameModeDifficulty::WarmUp);
	ApplyDifficultyButtonState(ChallengeButton, EGameModeDifficulty::Challenge);
	ApplyDifficultyButtonState(OrdealButton, EGameModeDifficulty::Ordeal);
	ApplyDifficultyButtonState(NightmareButton, EGameModeDifficulty::Nightmare);

	if (PveBattleButton)
	{
		const bool bHasSelectedDifficulty = SelectedDifficulty != EGameModeDifficulty::None;
		const bool bHasBattleReadyArmy = UArmyBuilderWidget::IsSavedPlayerArmyReadyForBattle();
		const bool bCanStartBattle = bHasSelectedDifficulty && bHasBattleReadyArmy;

		PveBattleButton->SetIsEnabled(bCanStartBattle);
		PveBattleButton->SetRenderOpacity(bCanStartBattle ? 1.0f : 0.45f);
	}
}

void UGameModeSelectWidget::DisableAllModeButtons()
{
	if (BackButton)
	{
		BackButton->SetIsEnabled(false);
	}

	if (WarmUpButton)
	{
		WarmUpButton->SetIsEnabled(false);
	}

	if (ChallengeButton)
	{
		ChallengeButton->SetIsEnabled(false);
	}

	if (OrdealButton)
	{
		OrdealButton->SetIsEnabled(false);
	}

	if (NightmareButton)
	{
		NightmareButton->SetIsEnabled(false);
	}

	if (PveBattleButton)
	{
		PveBattleButton->SetIsEnabled(false);
	}

	if (PvpBattleButton)
	{
		PvpBattleButton->SetIsEnabled(false);
	}
}

FString UGameModeSelectWidget::GetSelectedDifficultyOptionValue() const
{
	switch (SelectedDifficulty)
	{
	case EGameModeDifficulty::WarmUp:
		return TEXT("WarmUp");
	case EGameModeDifficulty::Challenge:
		return TEXT("Challenge");
	case EGameModeDifficulty::Ordeal:
		return TEXT("Ordeal");
	case EGameModeDifficulty::Nightmare:
		return TEXT("Nightmare");
	case EGameModeDifficulty::None:
	default:
		return TEXT("None");
	}
}

FName UGameModeSelectWidget::GetRandomEnabledBotBattleMapLevelName() const
{
	TArray<FName> EnabledMapLevelNames;

	for (const FBotBattleMapSelection& MapSelection : BotBattleMaps)
	{
		if (!MapSelection.bUseForBotBattle || MapSelection.MapLevel.IsNull())
		{
			continue;
		}

		const FString LongPackageName = MapSelection.MapLevel.ToSoftObjectPath().GetLongPackageName();
		if (LongPackageName.IsEmpty())
		{
			continue;
		}

		EnabledMapLevelNames.Add(FName(*LongPackageName));
	}

	if (EnabledMapLevelNames.Num() <= 0)
	{
		return BattleLevelName;
	}

	const int32 SelectedIndex = FMath::RandRange(0, EnabledMapLevelNames.Num() - 1);
	return EnabledMapLevelNames[SelectedIndex];
}

void UGameModeSelectWidget::OpenBattleLevel()
{
	const FName LevelToOpen = PendingBattleLevelName.IsNone() ? GetRandomEnabledBotBattleMapLevelName() : PendingBattleLevelName;
	PendingBattleLevelName = NAME_None;

	if (LevelToOpen.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("OpenBattleLevel failed: LevelToOpen is None."));
		return;
	}

	const FString DifficultyOption = FString::Printf(
		TEXT("BotDifficulty=%s"),
		*GetSelectedDifficultyOptionValue()
	);

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		// Keep all selected Blueprint unit classes strongly referenced while OpenLevel
		// destroys the menu world. Otherwise the old static Army Builder cache is not
		// visible to Unreal GC and can become invalid before HexGrid BeginPlay.
		LoadingGameInstance->CaptureBattleArmySnapshot();
		LoadingGameInstance->PrepareBattleLoadingScreen();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Battle loading screen is not active: Game Instance Class must inherit GameLoadingGameInstance."));
	}

	UE_LOG(LogTemp, Log, TEXT("Opening PVE battle map: %s, options: %s"), *LevelToOpen.ToString(), *DifficultyOption);
	UGameplayStatics::OpenLevel(this, LevelToOpen, true, DifficultyOption);
}

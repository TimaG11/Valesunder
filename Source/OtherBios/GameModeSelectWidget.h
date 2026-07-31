#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameModeSelectWidget.generated.h"

class UButton;
class UMainMenuWidget;
class UWorld;

UENUM(BlueprintType)
enum class EGameModeDifficulty : uint8
{
	None      UMETA(DisplayName = "None"),
	WarmUp    UMETA(DisplayName = "Warm Up"),
	Challenge UMETA(DisplayName = "Challenge"),
	Ordeal    UMETA(DisplayName = "Ordeal"),
	Nightmare UMETA(DisplayName = "Nightmare")
};

// One element in this array = one battle map that can be used for PVE bot matches.
// In WBP_GameModeSelect you can add/remove elements, pick a level asset, and enable/disable it.
USTRUCT(BlueprintType)
struct FBotBattleMapSelection
{
	GENERATED_BODY()

	// Pick your map/level asset here, for example /Game/Maps/L_Battle_Forest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bot Battle Map")
	TSoftObjectPtr<UWorld> MapLevel;

	// If false, this map stays in the list but will not be selected when entering PVE battle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bot Battle Map")
	bool bUseForBotBattle = true;
};

UCLASS()
class OTHERBIOS_API UGameModeSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetBattleLevelName(FName InBattleLevelName);
	void SetOpenBattleLevelDelay(float InOpenBattleLevelDelay);
	void SetParentMainMenu(UMainMenuWidget* InParentMainMenu);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BackButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* WarmUpButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ChallengeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* OrdealButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* NightmareButton = nullptr;

	// Left side Battle button. Disabled until the player selects a difficulty.
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* PveBattleButton = nullptr;

	// Right side Battle button. Clickable, but intentionally does nothing for now.
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* PvpBattleButton = nullptr;

	// Fallback level. Used if BotBattleMaps is empty or all maps in BotBattleMaps are disabled/not set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode Select|Navigation")
	FName BattleLevelName = TEXT("L_Battle");

	// Legacy setting kept so existing Blueprints do not lose serialized data.
	// It is intentionally ignored: real loading starts immediately and remains until the map is ready.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode Select|Navigation", meta = (ClampMin = "0.0"))
	float OpenBattleLevelDelay = 0.0f;

	// Add/remove maps here in WBP_GameModeSelect.
	// At PVE battle start one enabled map will be selected randomly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode Select|Bot Battle Maps")
	TArray<FBotBattleMapSelection> BotBattleMaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode Select|Visual")
	float SelectedDifficultyOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode Select|Visual")
	float NormalDifficultyOpacity = 0.55f;

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleWarmUpClicked();

	UFUNCTION()
	void HandleChallengeClicked();

	UFUNCTION()
	void HandleOrdealClicked();

	UFUNCTION()
	void HandleNightmareClicked();

	UFUNCTION()
	void HandlePveBattleClicked();

	UFUNCTION()
	void HandlePvpBattleClicked();

	void SelectDifficulty(EGameModeDifficulty NewDifficulty);
	void UpdateDifficultyVisuals();
	void DisableAllModeButtons();
	FString GetSelectedDifficultyOptionValue() const;
	FName GetRandomEnabledBotBattleMapLevelName() const;
	void OpenBattleLevel();

private:
	static EGameModeDifficulty SavedSelectedDifficulty;

	UPROPERTY()
	EGameModeDifficulty SelectedDifficulty = EGameModeDifficulty::None;

	UPROPERTY()
	UMainMenuWidget* ParentMainMenu = nullptr;

	FName PendingBattleLevelName = NAME_None;
};

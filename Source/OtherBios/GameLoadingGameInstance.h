#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "GameLoadingScreenWidget.h"
#include "ArmyBuilderWidget.h"
#include "GameLoadingGameInstance.generated.h"

class SWidget;
class UButton;
class UAccountSaveGame;
class UGameLoadingScreenWidget;
class UUserWidget;
class USoundBase;
class UWorld;

/**
 * Global loading-screen manager.
 *
 * Phase 1: MoviePlayer covers the blocking OpenLevel call.
 * Phase 2: after the world begins play, a normal viewport UMG overlay remains
 * visible while editor shader/asset jobs and runtime PSO precaching finish.
 *
 * Keeping MoviePlayer alive after OpenLevel is intentionally avoided because
 * UE 5.5 can stop ticking the code that is supposed to close it.
 */
UCLASS()
class OTHERBIOS_API UGameLoadingGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	UFUNCTION(BlueprintCallable, Category = "Loading Screen")
	void PrepareBattleLoadingScreen();

	UFUNCTION(BlueprintCallable, Category = "Loading Screen")
	void PrepareNextLoadingScreen(
		EGameLoadingScreenType InScreenType,
		FText InTitleOverride,
		FText InStatusOverride
	);

	// Captures the Army Builder state before OpenLevel. These UPROPERTY arrays live
	// in GameInstance and therefore keep Blueprint unit classes alive during map travel.
	void CaptureBattleArmySnapshot();
	bool HasBattleArmySnapshot() const;
	const TArray<TSubclassOf<AHexUnitActor>>& GetBattlePlayerArmyClasses() const { return BattlePlayerArmyClasses; }
	const TArray<TSubclassOf<AHexUnitActor>>& GetBattleAvailableUnitClasses() const { return BattleAvailableUnitClasses; }
	const TArray<FArmyBuilderDeploymentSlot>& GetBattleDeploymentSlots() const { return BattleDeploymentSlots; }
	const TArray<FArmyBuilderUnitProgress>& GetBattlePlayerProgress() const { return BattlePlayerProgress; }

	// Disk-backed account progression. Safe to call after every battle; when the
	// feature toggle is disabled these functions intentionally perform no disk I/O.
	UFUNCTION(BlueprintCallable, Category = "Account Save")
	bool SaveAccountProgression();

	UFUNCTION(BlueprintCallable, Category = "Account Save")
	bool LoadAccountProgression();

	UFUNCTION(BlueprintPure, Category = "Account Save")
	bool IsPersistentAccountProgressionEnabled() const { return bEnablePersistentAccountProgression; }

	// Binds one shared click sound to every UButton inside the supplied widget tree.
	// Safe to call repeatedly: duplicate bindings are removed first.
	UFUNCTION(BlueprintCallable, Category = "UI|Audio")
	void BindButtonClickSounds(UUserWidget* RootWidget);

	// Removes the shared click sound from one specific button.
	// Use this for frequently pressed gameplay controls such as Move, Attack and Ability.
	UFUNCTION(BlueprintCallable, Category = "UI|Audio")
	void UnbindButtonClickSound(UButton* Button);

	UFUNCTION(BlueprintCallable, Category = "UI|Audio")
	void PlayUIButtonClickSound();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Widgets")
	TSubclassOf<UGameLoadingScreenWidget> GeneralLoadingScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Widgets")
	TSubclassOf<UGameLoadingScreenWidget> MainMenuLoadingScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Widgets")
	TSubclassOf<UGameLoadingScreenWidget> BattleLoadingScreenWidgetClass;

	// Assign your imported click SoundWave or SoundCue in BP_GameLoadingGameInstance.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Audio")
	USoundBase* UIButtonClickSound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Audio", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float UIButtonClickVolume = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Maps")
	FName MainMenuMapName = TEXT("L_MainMenu");

	// Master switch for disk persistence. Turn this off in BP_GameLoadingGameInstance
	// for clean test sessions. The existing save file is left untouched.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Account Save")
	bool bEnablePersistentAccountProgression = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Account Save")
	FString AccountSaveSlot = TEXT("AccountData");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Account Save", meta = (ClampMin = "0"))
	int32 AccountSaveUserIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen", meta = (ClampMin = "0.0"))
	float MinimumLoadingScreenDisplayTime = 0.20f;

	// New setting with a new name so old Blueprint values from the broken implementation
	// cannot silently disable the post-load shader warmup.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Post Load")
	bool bUsePostLoadShaderWarmup = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Post Load", meta = (ClampMin = "0.01"))
	float PostLoadPollInterval = 0.05f;

	// Time limit applies to the asynchronous PSO wait. FinishAllCompilation itself is a
	// blocking engine call and intentionally completes editor shader/asset jobs first.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Post Load", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float MaximumPostLoadWarmupTime = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Post Load", meta = (ClampMin = "1", ClampMax = "30"))
	int32 RequiredStableZeroPolls = 5;

	// Gives the normal viewport overlay enough frames to be painted before the game
	// thread is blocked by FinishAllCompilation.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen|Post Load", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float OverlayPaintDelay = 0.15f;

private:
	UFUNCTION()
	void HandleUIButtonClicked();

	// Strong GC-tracked references for the battle that is about to open. The old
	// ArmyBuilderWidget static arrays are not UPROPERTY and can lose Blueprint classes
	// while the main-menu world is being unloaded.
	UPROPERTY(Transient)
	TArray<TSubclassOf<AHexUnitActor>> BattlePlayerArmyClasses;

	UPROPERTY(Transient)
	TArray<TSubclassOf<AHexUnitActor>> BattleAvailableUnitClasses;

	UPROPERTY(Transient)
	TArray<FArmyBuilderDeploymentSlot> BattleDeploymentSlots;

	UPROPERTY(Transient)
	TArray<FArmyBuilderUnitProgress> BattlePlayerProgress;

	// Strong GC-tracked references for every unit class loaded from AccountData.sav.
	// The ArmyBuilder session cache is static and therefore cannot use UPROPERTY itself.
	// Without this owner, Blueprint classes loaded from soft paths may be garbage-collected
	// after the main menu has already calculated its cached army power.
	UPROPERTY(Transient)
	TArray<TSubclassOf<AHexUnitActor>> PersistentAccountUnitClassKeepAlive;

	UPROPERTY(Transient)
	UGameLoadingScreenWidget* MovieLoadingWidget = nullptr;

	UPROPERTY(Transient)
	UGameLoadingScreenWidget* PostLoadLoadingWidget = nullptr;

	EGameLoadingScreenType PendingScreenType = EGameLoadingScreenType::Automatic;
	FText PendingTitleOverride;
	FText PendingStatusOverride;

	EGameLoadingScreenType CurrentScreenType = EGameLoadingScreenType::General;
	FString CurrentMapName;
	FText CurrentTitleOverride;
	FText CurrentStatusOverride;

	TWeakObjectPtr<UWorld> PostLoadWorld;
	FTSTicker::FDelegateHandle PostLoadTickerHandle;

	double PostLoadWarmupStartSeconds = 0.0;
	double OverlayShownSeconds = 0.0;
	double LastPostLoadPollSeconds = 0.0;
	uint32 HighestObservedPSOCount = 0;
	int32 HighestObservedAssetCount = 0;
	int32 ConsecutiveZeroPolls = 0;
	int32 LastLoggedCountdownSecond = INDEX_NONE;
	bool bPostLoadOverlayShown = false;
	bool bAssetCompilationFinishedSynchronously = false;
	bool bWorldWasPausedBeforeWarmup = false;
	bool bAccountProgressionInitialized = false;

	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);

	void StartPostLoadWarmup(UWorld* LoadedWorld);
	bool TickPostLoadWarmup(float DeltaTime);
	void FinishPostLoadWarmup(bool bTimedOut, const TCHAR* Reason);
	void CancelPostLoadWarmup();
	bool ShowPostLoadOverlay();
	void ForceFinishAssetCompilation();
	void RefreshWorldRenderState() const;
	void ApplySafePSOConsoleSettings() const;
	void RebuildPersistentAccountClassKeepAlive(const UAccountSaveGame* AccountSave);

	EGameLoadingScreenType ResolveScreenType(const FString& ShortMapName) const;
	TSubclassOf<UGameLoadingScreenWidget> ResolveWidgetClass(EGameLoadingScreenType ScreenType) const;
	TSharedRef<SWidget> BuildFallbackSlateWidget(EGameLoadingScreenType ScreenType) const;

	FText GetFallbackTitle(EGameLoadingScreenType ScreenType) const;
	FText GetFallbackStatus(EGameLoadingScreenType ScreenType) const;
};

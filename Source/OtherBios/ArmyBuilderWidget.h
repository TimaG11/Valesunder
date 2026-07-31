#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HexUnitActor.h"
#include "AccountSaveGame.h"
#include "ArmyBuilderWidget.generated.h"

class UButton;
class UImage;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWrapBox;
class UPanelWidget;
class UTexture2D;
class UMainMenuWidget;
class UArmyUnitCardWidget;
class UArmyDeploymentWidget;


USTRUCT(BlueprintType)
struct FArmyBuilderDeploymentSlot
{
	GENERATED_BODY()

	// Index in SelectedArmyUnitClasses. Do not store class only: duplicates are allowed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Deployment")
	int32 UnitIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Deployment")
	int32 Q = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Deployment")
	int32 R = 0;

	FArmyBuilderDeploymentSlot()
	{
	}

	FArmyBuilderDeploymentSlot(int32 InUnitIndex, int32 InQ, int32 InR)
		: UnitIndex(InUnitIndex), Q(InQ), R(InR)
	{
	}
};

UENUM(BlueprintType)
enum class EArmyUnitTypeFilter : uint8
{
	All        UMETA(DisplayName = "All"),
	Ram        UMETA(DisplayName = "Ram"),
	Champion   UMETA(DisplayName = "Champion"),
	Skirmisher UMETA(DisplayName = "Skirmisher"),
	Support    UMETA(DisplayName = "Support"),
	Healer     UMETA(DisplayName = "Healer")
};

UENUM(BlueprintType)
enum class EArmyBattleExperienceOutcome : uint8
{
	Victory   UMETA(DisplayName = "Victory"),
	Defeat    UMETA(DisplayName = "Defeat"),
	Surrender UMETA(DisplayName = "Surrender")
};


UENUM(BlueprintType)
enum class EArmyFactionEffectRole : uint8
{
	Ranged UMETA(DisplayName = "Ranged"),
	Tank   UMETA(DisplayName = "Tank"),
	Melee  UMETA(DisplayName = "Melee"),
	Healer UMETA(DisplayName = "Healer")
};

USTRUCT(BlueprintType)
struct FArmyFactionEffectIconConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction Effects")
	EArmyFactionEffectRole Role = EArmyFactionEffectRole::Melee;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction Effects")
	UTexture2D* IconTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction Effects")
	FText ToolTipText = FText::GetEmpty();
};

USTRUCT(BlueprintType)
struct FArmyFactionEffectFactionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction Effects")
	EHexUnitFaction Faction = EHexUnitFaction::Kingdom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction Effects")
	TArray<FArmyFactionEffectIconConfig> Icons;
};

USTRUCT(BlueprintType)
struct FArmyBuilderUnitProgress
{
	GENERATED_BODY()

	// Parallel to SelectedArmyUnitClasses. UnitClass is stored to keep duplicate unit slots stable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Progression")
	TSubclassOf<AHexUnitActor> UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Progression", meta = (ClampMin = "1", ClampMax = "15"))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Progression", meta = (ClampMin = "0"))
	int32 CurrentExperience = 0;

	FArmyBuilderUnitProgress()
	{
	}

	FArmyBuilderUnitProgress(TSubclassOf<AHexUnitActor> InUnitClass, int32 InLevel, int32 InCurrentExperience)
		: UnitClass(InUnitClass), Level(InLevel), CurrentExperience(InCurrentExperience)
	{
	}
};

UCLASS()
class OTHERBIOS_API UArmyBuilderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetParentMainMenu(UMainMenuWidget* InParentMainMenu);

	// Card buttons call this. The class stays available in the right panel,
	// so the same unit class can be added more than once.
	UFUNCTION(BlueprintCallable, Category = "Army Builder")
	bool TryAddUnitClass(TSubclassOf<AHexUnitActor> UnitClass);

	UFUNCTION(BlueprintCallable, Category = "Army Builder")
	bool RemoveSelectedUnitAt(int32 SelectedIndex);

	UFUNCTION(BlueprintCallable, Category = "Army Builder")
	void ClearArmy();

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	int32 GetUsedArmySlots() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	int32 GetFreeArmySlots() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	int32 GetSelectedUnitCount() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	int32 GetEffectiveMaxArmySlots() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	bool IsSelectedArmyReadyForBattle() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	bool CanAddUnitClassToArmy(TSubclassOf<AHexUnitActor> UnitClass) const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	int32 GetSelectedChampionCount() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	int32 GetCurrentArmyPower() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder|Progression")
	FArmyBuilderUnitProgress GetSelectedUnitProgressAt(int32 SelectedIndex) const;

	UFUNCTION(BlueprintPure, Category = "Army Builder|Progression")
	int32 GetSelectedUnitLevelAt(int32 SelectedIndex) const;

	UFUNCTION(BlueprintPure, Category = "Army Builder|Progression")
	int32 GetSelectedUnitExperienceAt(int32 SelectedIndex) const;

	// Progression memory for the available/right-side roster card.
	// This keeps earned XP when a unit is removed from the selected army and added again later.
	UFUNCTION(BlueprintPure, Category = "Army Builder|Progression")
	FArmyBuilderUnitProgress GetKnownUnitProgressForClass(TSubclassOf<AHexUnitActor> UnitClass) const;

	UFUNCTION(BlueprintPure, Category = "Army Builder|Progression")
	int32 GetUnitUpgradeCostForClass(TSubclassOf<AHexUnitActor> UnitClass) const;

	UFUNCTION(BlueprintPure, Category = "Army Builder|Progression")
	bool CanUpgradeUnitClass(TSubclassOf<AHexUnitActor> UnitClass) const;

	// Buys exactly one level. Excess experience stays banked for the next level.
	// Progress is class-based, so every duplicate of the same unit class is updated.
	UFUNCTION(BlueprintCallable, Category = "Army Builder|Progression")
	bool TryUpgradeUnitClass(TSubclassOf<AHexUnitActor> UnitClass);

	UFUNCTION(BlueprintCallable, Category = "Army Builder|Deployment")
	bool SaveArmyDeployment(const TArray<FArmyBuilderDeploymentSlot>& NewDeploymentSlots);

	UFUNCTION(BlueprintPure, Category = "Army Builder|Deployment")
	TArray<FArmyBuilderDeploymentSlot> GetCurrentSavedDeploymentSlotsForSelectedArmy() const;

	UFUNCTION(BlueprintPure, Category = "Army Builder")
	bool DoesUnitPassCurrentFilters(TSubclassOf<AHexUnitActor> UnitClass) const;

	static bool HasSavedPlayerArmy();
	static bool IsSavedPlayerArmyReadyForBattle();
	static TArray<TSubclassOf<AHexUnitActor>> GetSavedPlayerArmyUnitClasses();
	static TArray<TSubclassOf<AHexUnitActor>> GetSavedAvailableUnitClasses();
	static bool HasSavedPlayerArmyDeployment();
	static TArray<FArmyBuilderDeploymentSlot> GetSavedPlayerArmyDeploymentSlots();
	static TArray<FArmyBuilderUnitProgress> GetSavedPlayerArmyUnitProgressList();
	static FArmyBuilderUnitProgress MakeDefaultUnitProgress(TSubclassOf<AHexUnitActor> UnitClass);
	static void ApplyBattleExperienceToSavedArmy(const TMap<int32, int32>& RawExperienceByUnitIndex, EArmyBattleExperienceOutcome Outcome);
	static void ClearSavedPlayerArmy();
	static int32 GetLastPreviewArmyPower();
	static int32 GetSavedCoins();
	static void AddSavedCoins(int32 Amount);

	// Persistent account import/export. The actual disk I/O is owned by GameInstance;
	// ArmyBuilder remains responsible for validating gameplay data.
	static void ExportPersistentUnitProgress(TArray<FAccountUnitProgressRecord>& OutRecords);
	static void ImportPersistentUnitProgress(const TArray<FAccountUnitProgressRecord>& Records);
	static void ExportPersistentArmyAndCoins(TArray<TSoftClassPtr<AHexUnitActor>>& OutArmyClasses, int32& OutCoins);
	static void ImportPersistentArmyAndCoins(const TArray<TSoftClassPtr<AHexUnitActor>>& ArmyClasses, int32 Coins);
	static void ResetSessionAccountState();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* MainMenuButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ClearArmyButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SaveArmyButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ShowDeploymentButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* KingdomFactionButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* AnimalFactionButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SoulFactionButton = nullptr;

	// Optional Bandits faction filter button in WBP_ArmyBuilder.
	// The widget button must be named exactly BanditsFactionButton.
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BanditsFactionButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* AllTypeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* RamTypeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ChampionTypeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SkirmisherTypeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SupportTypeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* HealerTypeButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ArmySlotsTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ArmyPowerTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* ActiveFilterTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* AvailableCountTextBlock = nullptr;

	// Optional text/image shown in the left panel when the army is empty.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* EmptySelectedArmyText = nullptr;

	// Left panel container for selected unit cards.
	// In WBP_ArmyBuilderWidget create a VerticalBox inside the selected army ScrollBox
	// and name it exactly SelectedArmyPanel.
	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* SelectedArmyPanel = nullptr;

	// Decorative background image inside FactionEffectsSizeBox.
	// In WBP_ArmyBuilder the Image widget must be named exactly FactionEffectsBackgroundImage.
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* FactionEffectsBackgroundImage = nullptr;

	// Vertical scrolling area for future faction-effect icons.
	// In WBP_ArmyBuilder the ScrollBox must be named exactly FactionEffectsScrollBox.
	UPROPERTY(meta = (BindWidgetOptional))
	UScrollBox* FactionEffectsScrollBox = nullptr;

	// Future faction-effect widgets will be added to this container.
	// In WBP_ArmyBuilder the VerticalBox must be named exactly FactionEffectsVerticalBox.
	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* FactionEffectsVerticalBox = nullptr;


	// One element per faction. Add icon entries inside it and assign the role + imported texture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects")
	TArray<FArmyFactionEffectFactionConfig> FactionEffectConfigs;

	// Shared threshold for every faction and every icon.
	// An icon is shown only when the selected army contains at least this many matching units.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects", meta = (ClampMin = "1"))
	int32 MinUnitsForFactionEffectIcon = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects|Visual", meta = (ClampMin = "16.0"))
	float FactionEffectIconSize = 48.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects|Visual", meta = (ClampMin = "0.0"))
	float FactionEffectIconBottomPadding = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects|Visual", meta = (ClampMin = "8", ClampMax = "40"))
	int32 FactionEffectCountFontSize = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects|Visual")
	FLinearColor FactionEffectCountColor = FLinearColor::White;

	// Size and offset of the number area placed over the circle baked into the icon texture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects|Visual")
	FVector2D FactionEffectCountBoxSize = FVector2D(18.0f, 18.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Faction Effects|Visual")
	FMargin FactionEffectCountPadding = FMargin(0.0f, 0.0f, 1.0f, 1.0f);

	// Right panel container for available unit cards.
	// In WBP_ArmyBuilderWidget create WrapBox and name it exactly AvailableUnitsWrapBox.
	UPROPERTY(meta = (BindWidgetOptional))
	UWrapBox* AvailableUnitsWrapBox = nullptr;

	// Optional placeholder text like "Cards will be added later".
	// Rename it to EmptyAvailableUnitsText in WBP if you want it to hide automatically.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* EmptyAvailableUnitsText = nullptr;

	// Fill this in BP_ArmyBuilderWidget with all unit BP classes that should be shown on the right.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Roster")
	TArray<TSubclassOf<AHexUnitActor>> AvailableUnitClasses;

	// Set this in BP_ArmyBuilderWidget to WBP_ArmyUnitCard.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Cards")
	TSubclassOf<UArmyUnitCardWidget> UnitCardWidgetClass;

	// Set this in BP_ArmyBuilderWidget to WBP_ArmyDeployment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Deployment")
	TSubclassOf<UArmyDeploymentWidget> DeploymentWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Cards")
	FMargin UnitCardPadding = FMargin(0.0f, 0.0f, 14.0f, 14.0f);

	// Fallback slot limit. Used only if bUseUnitCountBasedSlotLimit is disabled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "1"))
	int32 MaxArmySlots = 10;

	// Minimum count required before the saved army is considered ready for battle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "1", ClampMax = "5"))
	int32 MinSelectedArmyUnits = 3;

	// Maximum count the player can add. For now keep it between 3 and 5.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "3", ClampMax = "5"))
	int32 MaxSelectedArmyUnits = 5;

	// Only one champion is allowed in the selected army. Keep this at 1 for the current rules.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "0", ClampMax = "1"))
	int32 MaxChampionUnits = 1;

	// If true: max units 3 -> 10 slots, max units 4 -> 12 slots, max units 5 -> 15 slots.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules")
	bool bUseUnitCountBasedSlotLimit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "1"))
	int32 SlotLimitFor3Units = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "1"))
	int32 SlotLimitFor4Units = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Rules", meta = (ClampMin = "1"))
	int32 SlotLimitFor5Units = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SelectedFilterOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Builder|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalFilterOpacity = 0.55f;

	UFUNCTION()
	void HandleMainMenuClicked();

	UFUNCTION()
	void HandleClearArmyClicked();

	UFUNCTION()
	void HandleSaveArmyClicked();

	UFUNCTION()
	void HandleShowDeploymentClicked();

	UFUNCTION()
	void HandleKingdomFactionClicked();

	UFUNCTION()
	void HandleAnimalFactionClicked();

	UFUNCTION()
	void HandleSoulFactionClicked();

	UFUNCTION()
	void HandleBanditsFactionClicked();

	UFUNCTION()
	void HandleAllTypeClicked();

	UFUNCTION()
	void HandleRamTypeClicked();

	UFUNCTION()
	void HandleChampionTypeClicked();

	UFUNCTION()
	void HandleSkirmisherTypeClicked();

	UFUNCTION()
	void HandleSupportTypeClicked();

	UFUNCTION()
	void HandleHealerTypeClicked();

private:
	UPROPERTY()
	UMainMenuWidget* ParentMainMenu = nullptr;

	UPROPERTY()
	TArray<TSubclassOf<AHexUnitActor>> SelectedArmyUnitClasses;

	UPROPERTY()
	TArray<FArmyBuilderUnitProgress> SelectedArmyUnitProgress;

	static TArray<TSubclassOf<AHexUnitActor>> SavedPlayerArmyUnitClasses;
	static TArray<TSubclassOf<AHexUnitActor>> SavedAvailableUnitClasses;
	static TArray<FArmyBuilderDeploymentSlot> SavedPlayerArmyDeploymentSlots;
	static TArray<FArmyBuilderUnitProgress> SavedPlayerArmyUnitProgress;

	// Session-only progression memory by unit class.
	// This is intentionally separate from SavedPlayerArmyUnitProgress, because selected army slots
	// disappear when the player removes a card from the left panel.
	static TMap<UClass*, FArmyBuilderUnitProgress> SavedRosterUnitProgress;

	static int32 CachedPreviewArmyPower;
	static int32 SavedCoins;

	UPROPERTY()
	UArmyDeploymentWidget* ActiveDeploymentWidget = nullptr;

	// Empty array means all factions are allowed.
	UPROPERTY()
	TArray<EHexUnitFaction> ActiveFactionFilters;

	// Empty array means all unit types are allowed.
	UPROPERTY()
	TArray<EArmyUnitTypeFilter> ActiveTypeFilters;

	void SetFactionFilter(EHexUnitFaction NewFactionFilter);
	void SetTypeFilter(EArmyUnitTypeFilter NewTypeFilter);
	void UpdateAllVisuals();
	void UpdateTexts();
	void UpdateButtonStates();
	void UpdateFilterVisuals();
	void RefreshFactionEffectIcons();
	int32 CountSelectedUnitsForFactionEffect(EHexUnitFaction Faction, EArmyFactionEffectRole Role) const;
	bool DoesUnitMatchFactionEffectRole(const AHexUnitActor* Unit, EArmyFactionEffectRole Role) const;
	void RefreshAvailableUnitList();
	void RefreshSelectedArmyList();
	void SaveSelectedArmyForBattle();
	void RequestPersistentAccountSave() const;
	void CacheAvailableUnitClassesForBattle();
	void NormalizeSelectedProgressForCurrentArmy();

	static bool IsArmyClassListReadyForBattle(const TArray<TSubclassOf<AHexUnitActor>>& ArmyClasses);
	static void NormalizeProgressListForArmy(const TArray<TSubclassOf<AHexUnitActor>>& ArmyClasses, TArray<FArmyBuilderUnitProgress>& ProgressList);
	static void AddExperienceToProgress(FArmyBuilderUnitProgress& Progress, int32 ExperienceToAdd);
	static float GetOutcomeExperienceMultiplier(EArmyBattleExperienceOutcome Outcome);

	static UClass* GetProgressStorageKey(TSubclassOf<AHexUnitActor> UnitClass);
	static FArmyBuilderUnitProgress SanitizeProgressForClass(TSubclassOf<AHexUnitActor> UnitClass, const FArmyBuilderUnitProgress& Progress);
	static bool IsProgressBetterOrEqual(const FArmyBuilderUnitProgress& NewProgress, const FArmyBuilderUnitProgress& ExistingProgress);
	static void StoreKnownProgressForClass(TSubclassOf<AHexUnitActor> UnitClass, const FArmyBuilderUnitProgress& Progress);
	static void StoreProgressListInRoster(const TArray<TSubclassOf<AHexUnitActor>>& UnitClasses, const TArray<FArmyBuilderUnitProgress>& ProgressList);
	static FArmyBuilderUnitProgress MakeProgressForAddedUnit(TSubclassOf<AHexUnitActor> UnitClass);

	bool IsUnitChampion(TSubclassOf<AHexUnitActor> UnitClass) const;
	bool IsDeploymentSlotListValidForArmy(const TArray<FArmyBuilderDeploymentSlot>& DeploymentSlots, int32 UnitCount) const;

	int32 GetUnitSlotCost(TSubclassOf<AHexUnitActor> UnitClass) const;
	int32 GetUnitPowerValue(TSubclassOf<AHexUnitActor> UnitClass) const;
	int32 CalculateArmyPowerForList(const TArray<TSubclassOf<AHexUnitActor>>& UnitClasses) const;
	int32 GetSlotLimitForUnitCount(int32 UnitCountLimit) const;
	int32 GetFilteredAvailableUnitCount() const;
	FString GetFactionFilterText() const;
	FString GetTypeFilterText() const;
	bool ConvertTypeFilterToUnitType(EArmyUnitTypeFilter TypeFilter, EHexUnitType& OutUnitType) const;
	void SetButtonSelectedVisual(UButton* Button, bool bSelected) const;
};
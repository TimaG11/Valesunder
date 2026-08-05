#include "ArmyBuilderWidget.h"

#include "ArmyUnitCardWidget.h"
#include "ArmyDeploymentWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "GameLoadingGameInstance.h"
#include "MainMenuWidget.h"

TArray<TSubclassOf<AHexUnitActor>> UArmyBuilderWidget::SavedPlayerArmyUnitClasses;
TArray<TSubclassOf<AHexUnitActor>> UArmyBuilderWidget::SavedAvailableUnitClasses;
TArray<FArmyBuilderDeploymentSlot> UArmyBuilderWidget::SavedPlayerArmyDeploymentSlots;
TArray<FArmyBuilderUnitProgress> UArmyBuilderWidget::SavedPlayerArmyUnitProgress;
TMap<UClass*, FArmyBuilderUnitProgress> UArmyBuilderWidget::SavedRosterUnitProgress;
int32 UArmyBuilderWidget::CachedPreviewArmyPower = 0;
int32 UArmyBuilderWidget::SavedCoins = 0;
TArray<FAccountFactionEffectRecord> UArmyBuilderWidget::SavedFactionEffectRecords;
TArray<FAccountDeploymentSlotRecord> UArmyBuilderWidget::PendingPersistentDeploymentRecords;
TArray<FAccountArmyPresetRecord> UArmyBuilderWidget::SavedArmyPresetRecords;
int32 UArmyBuilderWidget::ActiveArmyPresetIndex = 0;

int32 UArmyBuilderWidget::GetSavedCoins()
{
	return FMath::Max(0, SavedCoins);
}

void UArmyBuilderWidget::AddSavedCoins(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	SavedCoins = FMath::Max(0, SavedCoins + Amount);
}

void UArmyBuilderWidget::ExportPersistentUnitProgress(TArray<FAccountUnitProgressRecord>& OutRecords)
{
	OutRecords.Reset();
	OutRecords.Reserve(SavedRosterUnitProgress.Num());

	for (const TPair<UClass*, FArmyBuilderUnitProgress>& Pair : SavedRosterUnitProgress)
	{
		UClass* UnitClassObject = Pair.Key;
		if (!IsValid(UnitClassObject) || !UnitClassObject->IsChildOf(AHexUnitActor::StaticClass()))
		{
			continue;
		}

		const TSubclassOf<AHexUnitActor> UnitClass = UnitClassObject;
		const FArmyBuilderUnitProgress Progress = SanitizeProgressForClass(UnitClass, Pair.Value);

		FAccountUnitProgressRecord& Record = OutRecords.AddDefaulted_GetRef();
		Record.UnitClass = TSoftClassPtr<AHexUnitActor>(UnitClassObject);
		Record.Level = Progress.Level;
		Record.CurrentExperience = Progress.CurrentExperience;
	}

	OutRecords.Sort([](const FAccountUnitProgressRecord& Left, const FAccountUnitProgressRecord& Right)
		{
			return Left.UnitClass.ToSoftObjectPath().ToString() < Right.UnitClass.ToSoftObjectPath().ToString();
		});
}

void UArmyBuilderWidget::ImportPersistentUnitProgress(const TArray<FAccountUnitProgressRecord>& Records)
{
	SavedRosterUnitProgress.Reset();

	int32 LoadedCount = 0;
	for (const FAccountUnitProgressRecord& Record : Records)
	{
		UClass* LoadedClass = Record.UnitClass.LoadSynchronous();
		if (!IsValid(LoadedClass) || !LoadedClass->IsChildOf(AHexUnitActor::StaticClass()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Account progression skipped missing unit class: %s"),
				*Record.UnitClass.ToSoftObjectPath().ToString());
			continue;
		}

		const TSubclassOf<AHexUnitActor> UnitClass = LoadedClass;
		StoreKnownProgressForClass(
			UnitClass,
			FArmyBuilderUnitProgress(UnitClass, Record.Level, Record.CurrentExperience)
		);
		++LoadedCount;
	}

	// If an army already exists in this process (for example after editor travel),
	// refresh its parallel progress list from the persistent class-based roster.
	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);
	for (int32 Index = 0; Index < SavedPlayerArmyUnitClasses.Num(); ++Index)
	{
		if (!SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			continue;
		}

		const FArmyBuilderUnitProgress KnownProgress = MakeProgressForAddedUnit(SavedPlayerArmyUnitClasses[Index]);
		if (IsProgressBetterOrEqual(KnownProgress, SavedPlayerArmyUnitProgress[Index]))
		{
			SavedPlayerArmyUnitProgress[Index] = KnownProgress;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Persistent account progression imported: Records=%d Loaded=%d."),
		Records.Num(),
		LoadedCount
	);
}

void UArmyBuilderWidget::ExportPersistentArmyAndCoins(TArray<TSoftClassPtr<AHexUnitActor>>& OutArmyClasses, int32& OutCoins)
{
	StoreActiveRuntimeArmyInPreset();

	OutArmyClasses.Reset();
	OutArmyClasses.Reserve(SavedPlayerArmyUnitClasses.Num());

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SavedPlayerArmyUnitClasses)
	{
		if (UnitClass)
		{
			OutArmyClasses.Add(TSoftClassPtr<AHexUnitActor>(UnitClass.Get()));
		}
	}

	OutCoins = FMath::Max(0, SavedCoins);
}

void UArmyBuilderWidget::ExportPersistentFactionEffects(TArray<FAccountFactionEffectRecord>& OutRecords)
{
	// Always refresh from the saved army before serializing. This prevents stale
	// icon counts if role/faction rules change in a later build.
	RebuildSavedFactionEffectRecordsFromArmy(SavedPlayerArmyUnitClasses);
	OutRecords = SavedFactionEffectRecords;
}

void UArmyBuilderWidget::ImportPersistentFactionEffects(const TArray<FAccountFactionEffectRecord>& Records)
{
	SavedFactionEffectRecords.Reset();

	for (const FAccountFactionEffectRecord& Record : Records)
	{
		if (Record.UnitCount < 2 || Record.UnitCount > 5)
		{
			continue;
		}

		if (Record.FactionValue > static_cast<uint8>(EHexUnitFaction::Bandits) ||
			Record.RoleValue > static_cast<uint8>(EArmyFactionEffectRole::Healer))
		{
			continue;
		}

		SavedFactionEffectRecords.Add(Record);
	}

	UE_LOG(LogTemp, Log, TEXT("Persistent faction effects imported: Records=%d."),
		SavedFactionEffectRecords.Num());
}


void UArmyBuilderWidget::ExportPersistentDeployment(TArray<FAccountDeploymentSlotRecord>& OutRecords)
{
	OutRecords.Reset();
	OutRecords.Reserve(SavedPlayerArmyDeploymentSlots.Num());

	for (const FArmyBuilderDeploymentSlot& DeploymentSlot : SavedPlayerArmyDeploymentSlots)
	{
		FAccountDeploymentSlotRecord& Record = OutRecords.AddDefaulted_GetRef();
		Record.UnitIndex = DeploymentSlot.UnitIndex;
		Record.Q = DeploymentSlot.Q;
		Record.R = DeploymentSlot.R;
	}
}

void UArmyBuilderWidget::ImportPersistentDeployment(const TArray<FAccountDeploymentSlotRecord>& Records)
{
	PendingPersistentDeploymentRecords = Records;
}

void UArmyBuilderWidget::ExportPersistentArmyPresets(TArray<FAccountArmyPresetRecord>& OutPresets, int32& OutActivePresetIndex)
{
	StoreActiveRuntimeArmyInPreset();
	EnsureArmyPresetStorage();

	OutPresets = SavedArmyPresetRecords;
	OutActivePresetIndex = FMath::Clamp(ActiveArmyPresetIndex, 0, 4);
}

void UArmyBuilderWidget::ImportPersistentArmyPresets(const TArray<FAccountArmyPresetRecord>& Presets, int32 InActivePresetIndex)
{
	SavedArmyPresetRecords.Reset();

	const int32 PresetsToCopy = FMath::Min(Presets.Num(), 5);
	for (int32 PresetIndex = 0; PresetIndex < PresetsToCopy; ++PresetIndex)
	{
		const FAccountArmyPresetRecord& SourcePreset = Presets[PresetIndex];
		FAccountArmyPresetRecord& TargetPreset = SavedArmyPresetRecords.AddDefaulted_GetRef();

		for (const TSoftClassPtr<AHexUnitActor>& SoftUnitClass : SourcePreset.UnitClasses)
		{
			if (TargetPreset.UnitClasses.Num() >= 5)
			{
				break;
			}

			if (!SoftUnitClass.IsNull())
			{
				TargetPreset.UnitClasses.Add(SoftUnitClass);
			}
		}

		TargetPreset.DeploymentSlots = SourcePreset.DeploymentSlots;

		for (const FAccountFactionEffectRecord& EffectRecord : SourcePreset.FactionEffects)
		{
			if (EffectRecord.UnitCount < 2 || EffectRecord.UnitCount > 5)
			{
				continue;
			}

			if (EffectRecord.FactionValue > static_cast<uint8>(EHexUnitFaction::Bandits) ||
				EffectRecord.RoleValue > static_cast<uint8>(EArmyFactionEffectRole::Healer))
			{
				continue;
			}

			TargetPreset.FactionEffects.Add(EffectRecord);
		}
	}

	EnsureArmyPresetStorage();
	ActiveArmyPresetIndex = FMath::Clamp(InActivePresetIndex, 0, 4);

	// Do not load Blueprint classes while the SaveGame object itself is still being
	// deserialized. The existing GameInstance imports the active legacy army after
	// LoadGameFromSlot returns; inactive templates are loaded only when selected.
	UE_LOG(LogTemp, Log, TEXT("Persistent army presets imported: Presets=%d Active=%d."),
		SavedArmyPresetRecords.Num(),
		ActiveArmyPresetIndex + 1
	);
}

int32 UArmyBuilderWidget::GetActiveArmyPresetIndex()
{
	return FMath::Clamp(ActiveArmyPresetIndex, 0, 4);
}

void UArmyBuilderWidget::ImportPersistentArmyAndCoins(const TArray<TSoftClassPtr<AHexUnitActor>>& ArmyClasses, int32 Coins)
{
	SavedPlayerArmyUnitClasses.Reset();
	SavedPlayerArmyDeploymentSlots.Reset();
	SavedPlayerArmyUnitProgress.Reset();

	for (const TSoftClassPtr<AHexUnitActor>& SoftUnitClass : ArmyClasses)
	{
		if (SavedPlayerArmyUnitClasses.Num() >= 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("Persistent army contains more than 5 slots. Extra entries were ignored."));
			break;
		}

		UClass* LoadedClass = SoftUnitClass.LoadSynchronous();
		if (!IsValid(LoadedClass) || !LoadedClass->IsChildOf(AHexUnitActor::StaticClass()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Persistent army skipped missing unit class: %s"),
				*SoftUnitClass.ToSoftObjectPath().ToString());
			continue;
		}

		SavedPlayerArmyUnitClasses.Add(LoadedClass);
	}

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SavedPlayerArmyUnitClasses)
	{
		SavedPlayerArmyUnitProgress.Add(MakeProgressForAddedUnit(UnitClass));
	}
	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);

	if (PendingPersistentDeploymentRecords.Num() == SavedPlayerArmyUnitClasses.Num())
	{
		TSet<int32> UsedUnitIndexes;
		TSet<FIntPoint> UsedCoords;
		bool bValidDeployment = true;

		for (const FAccountDeploymentSlotRecord& Record : PendingPersistentDeploymentRecords)
		{
			const FIntPoint Coord(Record.Q, Record.R);
			if (Record.UnitIndex < 0 || Record.UnitIndex >= SavedPlayerArmyUnitClasses.Num() ||
				UsedUnitIndexes.Contains(Record.UnitIndex) || UsedCoords.Contains(Coord))
			{
				bValidDeployment = false;
				break;
			}

			UsedUnitIndexes.Add(Record.UnitIndex);
			UsedCoords.Add(Coord);
			SavedPlayerArmyDeploymentSlots.Add(FArmyBuilderDeploymentSlot(Record.UnitIndex, Record.Q, Record.R));
		}

		if (!bValidDeployment || SavedPlayerArmyDeploymentSlots.Num() != SavedPlayerArmyUnitClasses.Num())
		{
			SavedPlayerArmyDeploymentSlots.Reset();
		}
	}
	PendingPersistentDeploymentRecords.Reset();

	// The saved icon snapshot is restored by AccountSaveGame::Serialize, but the
	// composition is authoritative. Rebuild now so old saves (without records) and
	// future rule changes still produce correct active synergies immediately.
	RebuildSavedFactionEffectRecordsFromArmy(SavedPlayerArmyUnitClasses);

	SavedCoins = FMath::Max(0, Coins);
	CachedPreviewArmyPower = 0;
	for (int32 Index = 0; Index < SavedPlayerArmyUnitClasses.Num(); ++Index)
	{
		const AHexUnitActor* DefaultUnit = SavedPlayerArmyUnitClasses[Index]
			? SavedPlayerArmyUnitClasses[Index]->GetDefaultObject<AHexUnitActor>()
			: nullptr;
		if (DefaultUnit && SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			CachedPreviewArmyPower += DefaultUnit->GetArmyPowerValueForLevel(SavedPlayerArmyUnitProgress[Index].Level);
		}
	}

	StoreActiveRuntimeArmyInPreset();

	UE_LOG(LogTemp, Log, TEXT("Persistent army and coins imported: ArmyUnits=%d Coins=%d ActivePreset=%d."),
		SavedPlayerArmyUnitClasses.Num(),
		SavedCoins,
		ActiveArmyPresetIndex + 1
	);
}

void UArmyBuilderWidget::ResetSessionAccountState()
{
	SavedPlayerArmyUnitClasses.Reset();
	SavedAvailableUnitClasses.Reset();
	SavedPlayerArmyDeploymentSlots.Reset();
	SavedPlayerArmyUnitProgress.Reset();
	SavedRosterUnitProgress.Reset();
	SavedFactionEffectRecords.Reset();
	PendingPersistentDeploymentRecords.Reset();
	SavedArmyPresetRecords.Reset();
	ActiveArmyPresetIndex = 0;
	SavedCoins = 0;
	CachedPreviewArmyPower = 0;
}

void UArmyBuilderWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(this);
	}

	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleMainMenuClicked);
		MainMenuButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleMainMenuClicked);
		MainMenuButton->SetIsEnabled(true);
	}

	if (ClearArmyButton)
	{
		ClearArmyButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleClearArmyClicked);
		ClearArmyButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleClearArmyClicked);
	}

	if (SaveArmyButton)
	{
		SaveArmyButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleSaveArmyClicked);
		SaveArmyButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleSaveArmyClicked);
	}

	if (ShowDeploymentButton)
	{
		ShowDeploymentButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleShowDeploymentClicked);
		ShowDeploymentButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleShowDeploymentClicked);
	}

	if (ArmyPreset1Button)
	{
		ArmyPreset1Button->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleArmyPreset1Clicked);
		ArmyPreset1Button->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleArmyPreset1Clicked);
	}

	if (ArmyPreset2Button)
	{
		ArmyPreset2Button->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleArmyPreset2Clicked);
		ArmyPreset2Button->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleArmyPreset2Clicked);
	}

	if (ArmyPreset3Button)
	{
		ArmyPreset3Button->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleArmyPreset3Clicked);
		ArmyPreset3Button->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleArmyPreset3Clicked);
	}

	if (ArmyPreset4Button)
	{
		ArmyPreset4Button->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleArmyPreset4Clicked);
		ArmyPreset4Button->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleArmyPreset4Clicked);
	}

	if (ArmyPreset5Button)
	{
		ArmyPreset5Button->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleArmyPreset5Clicked);
		ArmyPreset5Button->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleArmyPreset5Clicked);
	}

	if (KingdomFactionButton)
	{
		KingdomFactionButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleKingdomFactionClicked);
		KingdomFactionButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleKingdomFactionClicked);
	}

	if (AnimalFactionButton)
	{
		AnimalFactionButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleAnimalFactionClicked);
		AnimalFactionButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleAnimalFactionClicked);
	}

	if (SoulFactionButton)
	{
		SoulFactionButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleSoulFactionClicked);
		SoulFactionButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleSoulFactionClicked);
	}

	if (BanditsFactionButton)
	{
		BanditsFactionButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleBanditsFactionClicked);
		BanditsFactionButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleBanditsFactionClicked);
	}

	if (AllTypeButton)
	{
		AllTypeButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleAllTypeClicked);
		AllTypeButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleAllTypeClicked);
	}

	if (RamTypeButton)
	{
		RamTypeButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleRamTypeClicked);
		RamTypeButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleRamTypeClicked);
	}

	if (ChampionTypeButton)
	{
		ChampionTypeButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleChampionTypeClicked);
		ChampionTypeButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleChampionTypeClicked);
	}

	if (SkirmisherTypeButton)
	{
		SkirmisherTypeButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleSkirmisherTypeClicked);
		SkirmisherTypeButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleSkirmisherTypeClicked);
	}

	if (SupportTypeButton)
	{
		SupportTypeButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleSupportTypeClicked);
		SupportTypeButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleSupportTypeClicked);
	}

	if (HealerTypeButton)
	{
		HealerTypeButton->OnClicked.RemoveDynamic(this, &UArmyBuilderWidget::HandleHealerTypeClicked);
		HealerTypeButton->OnClicked.AddDynamic(this, &UArmyBuilderWidget::HandleHealerTypeClicked);
	}

	ActiveFactionFilters.Reset();
	ActiveTypeFilters.Reset();

	CacheAvailableUnitClassesForBattle();
	EnsureArmyPresetStorage();
	LoadPresetIntoRuntime(ActiveArmyPresetIndex);

	SelectedArmyUnitClasses = SavedPlayerArmyUnitClasses;
	SelectedArmyUnitClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			UClass* UnitClassObject = UnitClass.Get();
			return !IsValid(UnitClassObject) || !UnitClassObject->IsChildOf(AHexUnitActor::StaticClass());
		});

	UE_LOG(LogTemp, Log, TEXT("Army Builder opened: SavedArmy=%d ValidArmy=%d SavedProgress=%d CachedPower=%d."),
		SavedPlayerArmyUnitClasses.Num(),
		SelectedArmyUnitClasses.Num(),
		SavedPlayerArmyUnitProgress.Num(),
		CachedPreviewArmyPower
	);

	SelectedArmyUnitProgress = SavedPlayerArmyUnitProgress;
	NormalizeSelectedProgressForCurrentArmy();
	StoreProgressListInRoster(SelectedArmyUnitClasses, SelectedArmyUnitProgress);

	// Prepare the narrow faction-effects panel. The actual effects will be added later.
	if (FactionEffectsBackgroundImage)
	{
		FactionEffectsBackgroundImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (FactionEffectsScrollBox)
	{
		FactionEffectsScrollBox->SetVisibility(ESlateVisibility::Visible);
	}

	if (FactionEffectsVerticalBox)
	{
		FactionEffectsVerticalBox->ClearChildren();
	}

	UpdateAllVisuals();
}

void UArmyBuilderWidget::SetParentMainMenu(UMainMenuWidget* InParentMainMenu)
{
	ParentMainMenu = InParentMainMenu;
}

bool UArmyBuilderWidget::TryAddUnitClass(TSubclassOf<AHexUnitActor> UnitClass)
{
	if (!UnitClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Army add blocked: UnitClass is null."));
		return false;
	}

	if (!CanAddUnitClassToArmy(UnitClass))
	{
		const AHexUnitActor* DefaultUnit = UnitClass->GetDefaultObject<AHexUnitActor>();
		if (DefaultUnit && DefaultUnit->UnitType == EHexUnitType::Champion && GetSelectedChampionCount() >= FMath::Max(0, MaxChampionUnits))
		{
			UE_LOG(LogTemp, Warning, TEXT("Army add blocked: only %d champion is allowed."), FMath::Max(0, MaxChampionUnits));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Army add blocked: %s does not fit current army rules."), *GetNameSafe(UnitClass.Get()));
		}

		UpdateAllVisuals();
		return false;
	}

	SelectedArmyUnitClasses.Add(UnitClass);
	SelectedArmyUnitProgress.Add(MakeProgressForAddedUnit(UnitClass));
	NormalizeSelectedProgressForCurrentArmy();
	UpdateAllVisuals();
	return true;
}

bool UArmyBuilderWidget::RemoveSelectedUnitAt(int32 SelectedIndex)
{
	if (!SelectedArmyUnitClasses.IsValidIndex(SelectedIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("Army remove blocked: invalid selected index %d."), SelectedIndex);
		return false;
	}

	const TSubclassOf<AHexUnitActor> RemovedUnitClass = SelectedArmyUnitClasses[SelectedIndex];
	const FArmyBuilderUnitProgress RemovedProgress = SelectedArmyUnitProgress.IsValidIndex(SelectedIndex)
		? SelectedArmyUnitProgress[SelectedIndex]
		: MakeDefaultUnitProgress(RemovedUnitClass);

	StoreKnownProgressForClass(RemovedUnitClass, RemovedProgress);

	SelectedArmyUnitClasses.RemoveAt(SelectedIndex);
	if (SelectedArmyUnitProgress.IsValidIndex(SelectedIndex))
	{
		SelectedArmyUnitProgress.RemoveAt(SelectedIndex);
	}
	NormalizeSelectedProgressForCurrentArmy();
	UpdateAllVisuals();
	return true;
}

void UArmyBuilderWidget::ClearArmy()
{
	StoreProgressListInRoster(SelectedArmyUnitClasses, SelectedArmyUnitProgress);

	SelectedArmyUnitClasses.Reset();
	SelectedArmyUnitProgress.Reset();
	ClearSavedPlayerArmy();
	RequestPersistentAccountSave();
	UpdateAllVisuals();
}

int32 UArmyBuilderWidget::GetUsedArmySlots() const
{
	int32 UsedSlots = 0;

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SelectedArmyUnitClasses)
	{
		UsedSlots += GetUnitSlotCost(UnitClass);
	}

	return UsedSlots;
}

int32 UArmyBuilderWidget::GetFreeArmySlots() const
{
	return FMath::Max(0, GetEffectiveMaxArmySlots() - GetUsedArmySlots());
}

int32 UArmyBuilderWidget::GetSelectedUnitCount() const
{
	return SelectedArmyUnitClasses.Num();
}

int32 UArmyBuilderWidget::GetEffectiveMaxArmySlots() const
{
	if (!bUseUnitCountBasedSlotLimit)
	{
		return FMath::Max(1, MaxArmySlots);
	}

	return GetSlotLimitForUnitCount(MaxSelectedArmyUnits);
}

bool UArmyBuilderWidget::IsSelectedArmyReadyForBattle() const
{
	const int32 UnitCount = SelectedArmyUnitClasses.Num();
	const int32 EffectiveMinUnitCount = FMath::Clamp(MinSelectedArmyUnits, 1, 5);
	const int32 EffectiveMaxUnitCount = FMath::Clamp(MaxSelectedArmyUnits, 3, 5);

	return UnitCount >= EffectiveMinUnitCount &&
		UnitCount <= EffectiveMaxUnitCount &&
		GetUsedArmySlots() <= GetEffectiveMaxArmySlots() &&
		GetSelectedChampionCount() <= FMath::Max(0, MaxChampionUnits);
}

bool UArmyBuilderWidget::CanAddUnitClassToArmy(TSubclassOf<AHexUnitActor> UnitClass) const
{
	const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	if (!DefaultUnit)
	{
		return false;
	}

	const int32 EffectiveMaxUnitCount = FMath::Clamp(MaxSelectedArmyUnits, 3, 5);
	if (SelectedArmyUnitClasses.Num() >= EffectiveMaxUnitCount)
	{
		return false;
	}

	if (DefaultUnit->UnitType == EHexUnitType::Champion && GetSelectedChampionCount() >= FMath::Max(0, MaxChampionUnits))
	{
		return false;
	}

	const int32 UnitSlotCost = GetUnitSlotCost(UnitClass);
	return GetUsedArmySlots() + UnitSlotCost <= GetEffectiveMaxArmySlots();
}

int32 UArmyBuilderWidget::GetSelectedChampionCount() const
{
	int32 ChampionCount = 0;

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SelectedArmyUnitClasses)
	{
		if (IsUnitChampion(UnitClass))
		{
			++ChampionCount;
		}
	}

	return ChampionCount;
}

int32 UArmyBuilderWidget::GetCurrentArmyPower() const
{
	return CalculateArmyPowerForList(SelectedArmyUnitClasses);
}

FArmyFactionEffectBonuses UArmyBuilderWidget::CalculateFactionEffectBonusesForArmyUnit(
	const TArray<TSubclassOf<AHexUnitActor>>& ArmyClasses,
	TSubclassOf<AHexUnitActor> UnitClass
)
{
	FArmyFactionEffectBonuses Bonuses;

	const AHexUnitActor* Unit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	if (!Unit || Unit->UnitType == EHexUnitType::Champion)
	{
		return Bonuses;
	}

	EArmyFactionEffectRole Role = EArmyFactionEffectRole::Melee;
	if (DoesUnitMatchFactionEffectRole(Unit, EArmyFactionEffectRole::Tank))
	{
		Role = EArmyFactionEffectRole::Tank;
	}
	else if (DoesUnitMatchFactionEffectRole(Unit, EArmyFactionEffectRole::Healer))
	{
		Role = EArmyFactionEffectRole::Healer;
	}
	else if (DoesUnitMatchFactionEffectRole(Unit, EArmyFactionEffectRole::Ranged))
	{
		Role = EArmyFactionEffectRole::Ranged;
	}

	int32 MatchingCount = 0;
	for (const TSubclassOf<AHexUnitActor>& CandidateClass : ArmyClasses)
	{
		const AHexUnitActor* Candidate = CandidateClass ? CandidateClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (!Candidate || Candidate->Faction != Unit->Faction)
		{
			continue;
		}

		if (DoesUnitMatchFactionEffectRole(Candidate, Role))
		{
			++MatchingCount;
		}
	}

	Bonuses.MatchingRoleCount = MatchingCount;
	if (MatchingCount < 2)
	{
		return Bonuses;
	}

	const int32 Tier = FMath::Clamp(MatchingCount, 2, 5);
	switch (Role)
	{
	case EArmyFactionEffectRole::Tank:
		// 2/3/4/5 Tanks: +3/+5/+7/+10% HP.
		Bonuses.MaxHealthMultiplier = 1.0f + (Tier == 2 ? 0.03f : Tier == 3 ? 0.05f : Tier == 4 ? 0.07f : 0.10f);
		break;

	case EArmyFactionEffectRole::Ranged:
		// 2/3/4/5 Ranged: +2/+4/+6/+8% ATK.
		Bonuses.AttackDamageMultiplier = 1.0f + (Tier == 2 ? 0.02f : Tier == 3 ? 0.04f : Tier == 4 ? 0.06f : 0.08f);
		break;

	case EArmyFactionEffectRole::Healer:
		// 2/3/4/5 Healers: +4/+7/+10/+15% healing.
		Bonuses.HealAmountMultiplier = 1.0f + (Tier == 2 ? 0.04f : Tier == 3 ? 0.07f : Tier == 4 ? 0.10f : 0.15f);
		break;

	case EArmyFactionEffectRole::Melee:
	default:
		// 2/3/4/5 Melee: +3/+5/+7/+10% ATK.
		Bonuses.AttackDamageMultiplier = 1.0f + (Tier == 2 ? 0.03f : Tier == 3 ? 0.05f : Tier == 4 ? 0.07f : 0.10f);
		break;
	}

	Bonuses.MovementRangeBonus = MatchingCount >= 5 ? 1 : 0;
	return Bonuses;
}

FArmyFactionEffectBonuses UArmyBuilderWidget::GetSelectedUnitFactionEffectBonusesAt(int32 SelectedIndex) const
{
	if (!SelectedArmyUnitClasses.IsValidIndex(SelectedIndex))
	{
		return FArmyFactionEffectBonuses();
	}

	return CalculateFactionEffectBonusesForArmyUnit(SelectedArmyUnitClasses, SelectedArmyUnitClasses[SelectedIndex]);
}

FArmyBuilderUnitProgress UArmyBuilderWidget::GetSelectedUnitProgressAt(int32 SelectedIndex) const
{
	if (!SelectedArmyUnitClasses.IsValidIndex(SelectedIndex))
	{
		return FArmyBuilderUnitProgress();
	}

	if (SelectedArmyUnitProgress.IsValidIndex(SelectedIndex))
	{
		FArmyBuilderUnitProgress Progress = SelectedArmyUnitProgress[SelectedIndex];
		Progress.UnitClass = SelectedArmyUnitClasses[SelectedIndex];
		Progress.Level = FMath::Clamp(Progress.Level, 1, AHexUnitActor::GetMaxProgressionLevel());

		if (Progress.Level >= AHexUnitActor::GetMaxProgressionLevel())
		{
			Progress.CurrentExperience = 0;
		}
		else
		{
			Progress.CurrentExperience = FMath::Max(0, Progress.CurrentExperience);
		}

		return Progress;
	}

	return MakeDefaultUnitProgress(SelectedArmyUnitClasses[SelectedIndex]);
}

int32 UArmyBuilderWidget::GetSelectedUnitLevelAt(int32 SelectedIndex) const
{
	return GetSelectedUnitProgressAt(SelectedIndex).Level;
}

int32 UArmyBuilderWidget::GetSelectedUnitExperienceAt(int32 SelectedIndex) const
{
	return GetSelectedUnitProgressAt(SelectedIndex).CurrentExperience;
}

FArmyBuilderUnitProgress UArmyBuilderWidget::GetKnownUnitProgressForClass(TSubclassOf<AHexUnitActor> UnitClass) const
{
	return MakeProgressForAddedUnit(UnitClass);
}

int32 UArmyBuilderWidget::GetUnitUpgradeCostForClass(TSubclassOf<AHexUnitActor> UnitClass) const
{
	if (!UnitClass)
	{
		return 0;
	}

	const FArmyBuilderUnitProgress Progress = MakeProgressForAddedUnit(UnitClass);
	return AHexUnitActor::GetUpgradeCoinCostForLevel(Progress.Level);
}

bool UArmyBuilderWidget::CanUpgradeUnitClass(TSubclassOf<AHexUnitActor> UnitClass) const
{
	if (!UnitClass)
	{
		return false;
	}

	const FArmyBuilderUnitProgress Progress = MakeProgressForAddedUnit(UnitClass);
	if (Progress.Level >= AHexUnitActor::GetMaxProgressionLevel())
	{
		return false;
	}

	const int32 RequiredExperience = AHexUnitActor::GetExperienceToNextLevelForLevel(Progress.Level);
	const int32 UpgradeCost = AHexUnitActor::GetUpgradeCoinCostForLevel(Progress.Level);

	return RequiredExperience > 0
		&& UpgradeCost > 0
		&& Progress.CurrentExperience >= RequiredExperience
		&& SavedCoins >= UpgradeCost;
}

bool UArmyBuilderWidget::TryUpgradeUnitClass(TSubclassOf<AHexUnitActor> UnitClass)
{
	if (!UnitClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unit upgrade blocked: UnitClass is null."));
		return false;
	}

	FArmyBuilderUnitProgress Progress = MakeProgressForAddedUnit(UnitClass);
	const int32 MaxLevel = AHexUnitActor::GetMaxProgressionLevel();
	if (Progress.Level >= MaxLevel)
	{
		UE_LOG(LogTemp, Log, TEXT("Unit upgrade blocked: maximum level reached. Unit=%s Level=%d"),
			*GetNameSafe(UnitClass.Get()),
			Progress.Level
		);
		return false;
	}

	const int32 RequiredExperience = AHexUnitActor::GetExperienceToNextLevelForLevel(Progress.Level);
	const int32 UpgradeCost = AHexUnitActor::GetUpgradeCoinCostForLevel(Progress.Level);
	if (RequiredExperience <= 0 || Progress.CurrentExperience < RequiredExperience)
	{
		UE_LOG(LogTemp, Log, TEXT("Unit upgrade blocked: not enough experience. Unit=%s EXP=%d Required=%d"),
			*GetNameSafe(UnitClass.Get()),
			Progress.CurrentExperience,
			RequiredExperience
		);
		return false;
	}

	if (UpgradeCost <= 0 || SavedCoins < UpgradeCost)
	{
		UE_LOG(LogTemp, Log, TEXT("Unit upgrade blocked: not enough coins. Unit=%s Coins=%d Cost=%d"),
			*GetNameSafe(UnitClass.Get()),
			SavedCoins,
			UpgradeCost
		);
		return false;
	}

	const int32 OldLevel = Progress.Level;
	Progress.CurrentExperience -= RequiredExperience;
	++Progress.Level;
	if (Progress.Level >= MaxLevel)
	{
		Progress.Level = MaxLevel;
		Progress.CurrentExperience = 0;
	}
	Progress = SanitizeProgressForClass(UnitClass, Progress);

	SavedCoins = FMath::Max(0, SavedCoins - UpgradeCost);
	StoreKnownProgressForClass(UnitClass, Progress);

	NormalizeSelectedProgressForCurrentArmy();
	for (int32 Index = 0; Index < SelectedArmyUnitClasses.Num(); ++Index)
	{
		if (SelectedArmyUnitClasses[Index] == UnitClass && SelectedArmyUnitProgress.IsValidIndex(Index))
		{
			SelectedArmyUnitProgress[Index] = Progress;
			SelectedArmyUnitProgress[Index].UnitClass = UnitClass;
		}
	}

	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);
	for (int32 Index = 0; Index < SavedPlayerArmyUnitClasses.Num(); ++Index)
	{
		if (SavedPlayerArmyUnitClasses[Index] == UnitClass && SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			SavedPlayerArmyUnitProgress[Index] = Progress;
			SavedPlayerArmyUnitProgress[Index].UnitClass = UnitClass;
		}
	}

	CachedPreviewArmyPower = 0;
	for (int32 Index = 0; Index < SavedPlayerArmyUnitClasses.Num(); ++Index)
	{
		const AHexUnitActor* DefaultUnit = SavedPlayerArmyUnitClasses[Index]
			? SavedPlayerArmyUnitClasses[Index]->GetDefaultObject<AHexUnitActor>()
			: nullptr;
		if (DefaultUnit && SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			CachedPreviewArmyPower += DefaultUnit->GetArmyPowerValueForLevel(SavedPlayerArmyUnitProgress[Index].Level);
		}
	}

	RequestPersistentAccountSave();
	UpdateAllVisuals();

	UE_LOG(LogTemp, Log, TEXT("Unit upgraded. Unit=%s Level=%d->%d Cost=%d RemainingCoins=%d RemainingXP=%d"),
		*GetNameSafe(UnitClass.Get()),
		OldLevel,
		Progress.Level,
		UpgradeCost,
		SavedCoins,
		Progress.CurrentExperience
	);
	return true;
}

bool UArmyBuilderWidget::SaveArmyDeployment(const TArray<FArmyBuilderDeploymentSlot>& NewDeploymentSlots)
{
	if (!IsSelectedArmyReadyForBattle())
	{
		UE_LOG(LogTemp, Warning, TEXT("Deployment save blocked: selected army is not ready."));
		UpdateAllVisuals();
		return false;
	}

	if (!IsDeploymentSlotListValidForArmy(NewDeploymentSlots, SelectedArmyUnitClasses.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Deployment save blocked: every selected unit must have one unique deployment cell."));
		return false;
	}

	// Save the composition without triggering an intermediate disk write, then attach
	// this deployment to the same active template and persist once.
	CommitSelectedArmyToActivePreset(false);
	SavedPlayerArmyDeploymentSlots = NewDeploymentSlots;
	StoreActiveRuntimeArmyInPreset();
	RequestPersistentAccountSave();

	UE_LOG(LogTemp, Log, TEXT("Army deployment saved: Units=%d Preset=%d."),
		SavedPlayerArmyDeploymentSlots.Num(),
		ActiveArmyPresetIndex + 1
	);
	UpdateAllVisuals();
	return true;
}

TArray<FArmyBuilderDeploymentSlot> UArmyBuilderWidget::GetCurrentSavedDeploymentSlotsForSelectedArmy() const
{
	// Unit indexes are meaningful only for the exact same ordered composition.
	// A different draft with the same unit count must not inherit another template's formation.
	if (SelectedArmyUnitClasses == SavedPlayerArmyUnitClasses &&
		IsDeploymentSlotListValidForArmy(SavedPlayerArmyDeploymentSlots, SelectedArmyUnitClasses.Num()))
	{
		return SavedPlayerArmyDeploymentSlots;
	}

	return TArray<FArmyBuilderDeploymentSlot>();
}

bool UArmyBuilderWidget::HasSavedPlayerArmy()
{
	return IsSavedPlayerArmyReadyForBattle();
}

bool UArmyBuilderWidget::IsSavedPlayerArmyReadyForBattle()
{
	return IsArmyClassListReadyForBattle(SavedPlayerArmyUnitClasses);
}

TArray<TSubclassOf<AHexUnitActor>> UArmyBuilderWidget::GetSavedPlayerArmyUnitClasses()
{
	if (!IsSavedPlayerArmyReadyForBattle())
	{
		return TArray<TSubclassOf<AHexUnitActor>>();
	}

	return SavedPlayerArmyUnitClasses;
}

TArray<TSubclassOf<AHexUnitActor>> UArmyBuilderWidget::GetSavedAvailableUnitClasses()
{
	TArray<TSubclassOf<AHexUnitActor>> Result = SavedAvailableUnitClasses;
	Result.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			UClass* UnitClassObject = UnitClass.Get();
			return !IsValid(UnitClassObject) || !UnitClassObject->IsChildOf(AHexUnitActor::StaticClass());
		});
	return Result;
}

bool UArmyBuilderWidget::HasSavedPlayerArmyDeployment()
{
	return IsArmyClassListReadyForBattle(SavedPlayerArmyUnitClasses) &&
		SavedPlayerArmyDeploymentSlots.Num() == SavedPlayerArmyUnitClasses.Num();
}

TArray<FArmyBuilderDeploymentSlot> UArmyBuilderWidget::GetSavedPlayerArmyDeploymentSlots()
{
	if (!HasSavedPlayerArmyDeployment())
	{
		return TArray<FArmyBuilderDeploymentSlot>();
	}

	return SavedPlayerArmyDeploymentSlots;
}

TArray<FArmyBuilderUnitProgress> UArmyBuilderWidget::GetSavedPlayerArmyUnitProgressList()
{
	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);
	return SavedPlayerArmyUnitProgress;
}

FArmyBuilderUnitProgress UArmyBuilderWidget::MakeDefaultUnitProgress(TSubclassOf<AHexUnitActor> UnitClass)
{
	return FArmyBuilderUnitProgress(UnitClass, 1, 0);
}

UClass* UArmyBuilderWidget::GetProgressStorageKey(TSubclassOf<AHexUnitActor> UnitClass)
{
	return UnitClass ? UnitClass.Get() : nullptr;
}

FArmyBuilderUnitProgress UArmyBuilderWidget::SanitizeProgressForClass(TSubclassOf<AHexUnitActor> UnitClass, const FArmyBuilderUnitProgress& Progress)
{
	FArmyBuilderUnitProgress Result = Progress;
	Result.UnitClass = UnitClass;
	Result.Level = FMath::Clamp(Result.Level, 1, AHexUnitActor::GetMaxProgressionLevel());

	if (Result.Level >= AHexUnitActor::GetMaxProgressionLevel())
	{
		Result.CurrentExperience = 0;
	}
	else
	{
		// Experience is allowed to stay above the threshold until the player
		// explicitly purchases one level with coins.
		Result.CurrentExperience = FMath::Max(0, Result.CurrentExperience);
	}

	return Result;
}

bool UArmyBuilderWidget::IsProgressBetterOrEqual(const FArmyBuilderUnitProgress& NewProgress, const FArmyBuilderUnitProgress& ExistingProgress)
{
	if (NewProgress.Level != ExistingProgress.Level)
	{
		return NewProgress.Level > ExistingProgress.Level;
	}

	return NewProgress.CurrentExperience >= ExistingProgress.CurrentExperience;
}

void UArmyBuilderWidget::StoreKnownProgressForClass(TSubclassOf<AHexUnitActor> UnitClass, const FArmyBuilderUnitProgress& Progress)
{
	UClass* ProgressKey = GetProgressStorageKey(UnitClass);
	if (!ProgressKey)
	{
		return;
	}

	const FArmyBuilderUnitProgress NewProgress = SanitizeProgressForClass(UnitClass, Progress);
	if (const FArmyBuilderUnitProgress* ExistingProgress = SavedRosterUnitProgress.Find(ProgressKey))
	{
		const FArmyBuilderUnitProgress ExistingSanitizedProgress = SanitizeProgressForClass(UnitClass, *ExistingProgress);
		if (!IsProgressBetterOrEqual(NewProgress, ExistingSanitizedProgress))
		{
			return;
		}
	}

	SavedRosterUnitProgress.Add(ProgressKey, NewProgress);
}

void UArmyBuilderWidget::StoreProgressListInRoster(const TArray<TSubclassOf<AHexUnitActor>>& UnitClasses, const TArray<FArmyBuilderUnitProgress>& ProgressList)
{
	for (int32 Index = 0; Index < UnitClasses.Num(); ++Index)
	{
		const FArmyBuilderUnitProgress Progress = ProgressList.IsValidIndex(Index)
			? ProgressList[Index]
			: MakeDefaultUnitProgress(UnitClasses[Index]);

		StoreKnownProgressForClass(UnitClasses[Index], Progress);
	}
}

FArmyBuilderUnitProgress UArmyBuilderWidget::MakeProgressForAddedUnit(TSubclassOf<AHexUnitActor> UnitClass)
{
	UClass* ProgressKey = GetProgressStorageKey(UnitClass);
	if (ProgressKey)
	{
		if (const FArmyBuilderUnitProgress* FoundProgress = SavedRosterUnitProgress.Find(ProgressKey))
		{
			return SanitizeProgressForClass(UnitClass, *FoundProgress);
		}
	}

	return MakeDefaultUnitProgress(UnitClass);
}

void UArmyBuilderWidget::ApplyBattleExperienceToSavedArmy(const TMap<int32, int32>& RawExperienceByUnitIndex, EArmyBattleExperienceOutcome Outcome)
{
	if (SavedPlayerArmyUnitClasses.IsEmpty() || RawExperienceByUnitIndex.IsEmpty())
	{
		return;
	}

	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);

	const float Multiplier = GetOutcomeExperienceMultiplier(Outcome);
	int32 TotalAppliedExperience = 0;

	for (const TPair<int32, int32>& Pair : RawExperienceByUnitIndex)
	{
		const int32 UnitIndex = Pair.Key;
		const int32 RawExperience = FMath::Max(0, Pair.Value);

		if (!SavedPlayerArmyUnitClasses.IsValidIndex(UnitIndex) ||
			!SavedPlayerArmyUnitProgress.IsValidIndex(UnitIndex) ||
			RawExperience <= 0)
		{
			continue;
		}

		const int32 AppliedExperience = FMath::Max(0, FMath::RoundToInt(static_cast<float>(RawExperience) * Multiplier));
		if (AppliedExperience <= 0)
		{
			continue;
		}

		SavedPlayerArmyUnitProgress[UnitIndex].UnitClass = SavedPlayerArmyUnitClasses[UnitIndex];
		AddExperienceToProgress(SavedPlayerArmyUnitProgress[UnitIndex], AppliedExperience);
		StoreKnownProgressForClass(SavedPlayerArmyUnitClasses[UnitIndex], SavedPlayerArmyUnitProgress[UnitIndex]);
		TotalAppliedExperience += AppliedExperience;
	}

	StoreProgressListInRoster(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);

	CachedPreviewArmyPower = 0;
	for (int32 Index = 0; Index < SavedPlayerArmyUnitClasses.Num(); ++Index)
	{
		const AHexUnitActor* DefaultUnit = SavedPlayerArmyUnitClasses[Index] ? SavedPlayerArmyUnitClasses[Index]->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (DefaultUnit && SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			CachedPreviewArmyPower += DefaultUnit->GetArmyPowerValueForLevel(SavedPlayerArmyUnitProgress[Index].Level);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Army progression saved after battle. Outcome=%d TotalAppliedXP=%d."),
		static_cast<int32>(Outcome),
		TotalAppliedExperience
	);
}

void UArmyBuilderWidget::ClearSavedPlayerArmy()
{
	SavedPlayerArmyUnitClasses.Reset();
	SavedPlayerArmyDeploymentSlots.Reset();
	SavedPlayerArmyUnitProgress.Reset();
	SavedFactionEffectRecords.Reset();
	CachedPreviewArmyPower = 0;
	StoreActiveRuntimeArmyInPreset();
}

int32 UArmyBuilderWidget::GetLastPreviewArmyPower()
{
	return CachedPreviewArmyPower;
}

bool UArmyBuilderWidget::DoesUnitPassCurrentFilters(TSubclassOf<AHexUnitActor> UnitClass) const
{
	const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	if (!DefaultUnit)
	{
		return false;
	}

	if (!ActiveFactionFilters.IsEmpty() && !ActiveFactionFilters.Contains(DefaultUnit->Faction))
	{
		return false;
	}

	if (!ActiveTypeFilters.IsEmpty())
	{
		bool bMatchesAnyTypeFilter = false;

		for (const EArmyUnitTypeFilter TypeFilter : ActiveTypeFilters)
		{
			EHexUnitType RequiredUnitType = EHexUnitType::Skirmisher;
			if (ConvertTypeFilterToUnitType(TypeFilter, RequiredUnitType) && DefaultUnit->UnitType == RequiredUnitType)
			{
				bMatchesAnyTypeFilter = true;
				break;
			}
		}

		if (!bMatchesAnyTypeFilter)
		{
			return false;
		}
	}

	return true;
}

void UArmyBuilderWidget::HandleMainMenuClicked()
{
	UMainMenuWidget* MenuToShow = ParentMainMenu;
	ParentMainMenu = nullptr;

	RemoveFromParent();

	if (MenuToShow)
	{
		MenuToShow->ReturnFromArmyBuilder();
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (PlayerController)
	{
		PlayerController->bShowMouseCursor = true;
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;

		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
	}
}

void UArmyBuilderWidget::HandleClearArmyClicked()
{
	ClearArmy();
}

void UArmyBuilderWidget::HandleSaveArmyClicked()
{
	// Saving and battle readiness are separate concerns.
	// The player may intentionally save an incomplete or empty army and return to the main menu.
	SaveSelectedArmyForBattle();

	// Rebuild cards after Save so any unconfirmed first-click upgrade preview is cancelled.
	UpdateAllVisuals();

	UE_LOG(LogTemp, Log, TEXT("Army template %d saved: Units=%d Slots=%d/%d ReadyForBattle=%s."),
		ActiveArmyPresetIndex + 1,
		SelectedArmyUnitClasses.Num(),
		GetUsedArmySlots(),
		GetEffectiveMaxArmySlots(),
		IsSelectedArmyReadyForBattle() ? TEXT("true") : TEXT("false")
	);
}

void UArmyBuilderWidget::HandleShowDeploymentClicked()
{
	if (!IsSelectedArmyReadyForBattle())
	{
		UE_LOG(LogTemp, Warning, TEXT("Show deployment blocked: save/select a ready army first."));
		UpdateAllVisuals();
		return;
	}

	if (!DeploymentWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Show deployment blocked: DeploymentWidgetClass is not set in BP_ArmyBuilderWidget."));
		return;
	}

	if (ActiveDeploymentWidget && ActiveDeploymentWidget->IsInViewport())
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	ActiveDeploymentWidget = CreateWidget<UArmyDeploymentWidget>(PlayerController ? PlayerController : GetOwningPlayer(), DeploymentWidgetClass);
	if (!ActiveDeploymentWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("Show deployment blocked: failed to create deployment widget."));
		return;
	}

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(ActiveDeploymentWidget);
	}

	ActiveDeploymentWidget->InitializeDeployment(this, SelectedArmyUnitClasses, GetCurrentSavedDeploymentSlotsForSelectedArmy());
	ActiveDeploymentWidget->AddToViewport(10);
}

void UArmyBuilderWidget::HandleArmyPreset1Clicked()
{
	SelectArmyPreset(0);
}

void UArmyBuilderWidget::HandleArmyPreset2Clicked()
{
	SelectArmyPreset(1);
}

void UArmyBuilderWidget::HandleArmyPreset3Clicked()
{
	SelectArmyPreset(2);
}

void UArmyBuilderWidget::HandleArmyPreset4Clicked()
{
	SelectArmyPreset(3);
}

void UArmyBuilderWidget::HandleArmyPreset5Clicked()
{
	SelectArmyPreset(4);
}

void UArmyBuilderWidget::HandleKingdomFactionClicked()
{
	SetFactionFilter(EHexUnitFaction::Kingdom);
}

void UArmyBuilderWidget::HandleAnimalFactionClicked()
{
	SetFactionFilter(EHexUnitFaction::Animal);
}

void UArmyBuilderWidget::HandleSoulFactionClicked()
{
	SetFactionFilter(EHexUnitFaction::Soul);
}

void UArmyBuilderWidget::HandleBanditsFactionClicked()
{
	SetFactionFilter(EHexUnitFaction::Bandits);
}

void UArmyBuilderWidget::HandleAllTypeClicked()
{
	SetTypeFilter(EArmyUnitTypeFilter::All);
}

void UArmyBuilderWidget::HandleRamTypeClicked()
{
	SetTypeFilter(EArmyUnitTypeFilter::Ram);
}

void UArmyBuilderWidget::HandleChampionTypeClicked()
{
	SetTypeFilter(EArmyUnitTypeFilter::Champion);
}

void UArmyBuilderWidget::HandleSkirmisherTypeClicked()
{
	SetTypeFilter(EArmyUnitTypeFilter::Skirmisher);
}

void UArmyBuilderWidget::HandleSupportTypeClicked()
{
	SetTypeFilter(EArmyUnitTypeFilter::Support);
}

void UArmyBuilderWidget::HandleHealerTypeClicked()
{
	SetTypeFilter(EArmyUnitTypeFilter::Healer);
}

void UArmyBuilderWidget::SetFactionFilter(EHexUnitFaction NewFactionFilter)
{
	if (ActiveFactionFilters.Contains(NewFactionFilter))
	{
		ActiveFactionFilters.Remove(NewFactionFilter);
	}
	else
	{
		ActiveFactionFilters.Add(NewFactionFilter);
	}

	UpdateAllVisuals();
}

void UArmyBuilderWidget::SetTypeFilter(EArmyUnitTypeFilter NewTypeFilter)
{
	if (NewTypeFilter == EArmyUnitTypeFilter::All)
	{
		ActiveTypeFilters.Reset();
		UpdateAllVisuals();
		return;
	}

	if (ActiveTypeFilters.Contains(NewTypeFilter))
	{
		ActiveTypeFilters.Remove(NewTypeFilter);
	}
	else
	{
		ActiveTypeFilters.Add(NewTypeFilter);
	}

	UpdateAllVisuals();
}

void UArmyBuilderWidget::UpdateAllVisuals()
{
	RefreshSelectedArmyList();
	RefreshAvailableUnitList();
	RefreshFactionEffectIcons();
	UpdateTexts();
	UpdateButtonStates();
	UpdateFilterVisuals();
	UpdateArmyPresetVisuals();
}


bool UArmyBuilderWidget::DoesUnitMatchFactionEffectRole(const AHexUnitActor* Unit, EArmyFactionEffectRole Role)
{
	if (!Unit)
	{
		return false;
	}

	// Champions never contribute to faction-effect role icons.
	if (Unit->UnitType == EHexUnitType::Champion)
	{
		return false;
	}

	// Every non-champion belongs to exactly one faction-effect role.
	// This prevents Ram/Healer units from also being counted as Melee/Ranged.
	switch (Role)
	{
	case EArmyFactionEffectRole::Tank:
		return Unit->UnitType == EHexUnitType::Ram;

	case EArmyFactionEffectRole::Healer:
		return Unit->UnitType == EHexUnitType::Healer;

	case EArmyFactionEffectRole::Ranged:
		return Unit->UnitType != EHexUnitType::Ram
			&& Unit->UnitType != EHexUnitType::Healer
			&& Unit->AttackRange > 1;

	case EArmyFactionEffectRole::Melee:
	default:
		return Unit->UnitType != EHexUnitType::Ram
			&& Unit->UnitType != EHexUnitType::Healer
			&& Unit->AttackRange <= 1;
	}
}

void UArmyBuilderWidget::RebuildSavedFactionEffectRecordsFromArmy(const TArray<TSubclassOf<AHexUnitActor>>& ArmyClasses)
{
	SavedFactionEffectRecords.Reset();

	for (uint8 FactionValue = 0; FactionValue <= static_cast<uint8>(EHexUnitFaction::Bandits); ++FactionValue)
	{
		const EHexUnitFaction Faction = static_cast<EHexUnitFaction>(FactionValue);

		for (uint8 RoleValue = 0; RoleValue <= static_cast<uint8>(EArmyFactionEffectRole::Healer); ++RoleValue)
		{
			const EArmyFactionEffectRole Role = static_cast<EArmyFactionEffectRole>(RoleValue);
			int32 Count = 0;

			for (const TSubclassOf<AHexUnitActor>& UnitClass : ArmyClasses)
			{
				const AHexUnitActor* Unit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
				if (!Unit || Unit->Faction != Faction || Unit->UnitType == EHexUnitType::Champion)
				{
					continue;
				}

				if (DoesUnitMatchFactionEffectRole(Unit, Role))
				{
					++Count;
				}
			}

			if (Count >= 2)
			{
				FAccountFactionEffectRecord& Record = SavedFactionEffectRecords.AddDefaulted_GetRef();
				Record.FactionValue = FactionValue;
				Record.RoleValue = RoleValue;
				Record.UnitCount = FMath::Clamp(Count, 2, 5);
			}
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("Faction effect snapshot rebuilt from army: ArmyUnits=%d Effects=%d."),
		ArmyClasses.Num(),
		SavedFactionEffectRecords.Num());
}

int32 UArmyBuilderWidget::CountSelectedUnitsForFactionEffect(EHexUnitFaction Faction, EArmyFactionEffectRole Role) const
{
	int32 Count = 0;

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SelectedArmyUnitClasses)
	{
		const AHexUnitActor* DefaultUnit = UnitClass
			? UnitClass->GetDefaultObject<AHexUnitActor>()
			: nullptr;

		if (!DefaultUnit || DefaultUnit->Faction != Faction)
		{
			continue;
		}

		if (DoesUnitMatchFactionEffectRole(DefaultUnit, Role))
		{
			++Count;
		}
	}

	return Count;
}

void UArmyBuilderWidget::RefreshFactionEffectIcons()
{
	if (!FactionEffectsVerticalBox || !WidgetTree)
	{
		return;
	}

	FactionEffectsVerticalBox->ClearChildren();

	const int32 RequiredCount = FMath::Max(1, MinUnitsForFactionEffectIcon);
	const float SafeIconSize = FMath::Max(16.0f, FactionEffectIconSize);
	const FVector2D SafeCountBoxSize(
		FMath::Max(1.0f, FactionEffectCountBoxSize.X),
		FMath::Max(1.0f, FactionEffectCountBoxSize.Y)
	);

	for (const FArmyFactionEffectFactionConfig& FactionConfig : FactionEffectConfigs)
	{
		for (const FArmyFactionEffectIconConfig& IconConfig : FactionConfig.Icons)
		{
			if (!IconConfig.IconTexture)
			{
				continue;
			}

			const int32 MatchingUnitCount = CountSelectedUnitsForFactionEffect(
				FactionConfig.Faction,
				IconConfig.Role
			);

			if (MatchingUnitCount < RequiredCount)
			{
				continue;
			}

			USizeBox* IconRootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			UOverlay* IconOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
			UImage* IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			USizeBox* CountSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			UTextBlock* CountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

			if (!IconRootSizeBox || !IconOverlay || !IconImage || !CountSizeBox || !CountText)
			{
				continue;
			}

			IconRootSizeBox->SetWidthOverride(SafeIconSize);
			IconRootSizeBox->SetHeightOverride(SafeIconSize);

			// Build the tooltip from the exact same runtime bonus calculation used by
			// selected-army cards and battle spawning. This prevents tooltip numbers
			// from drifting away from the real gameplay effect when balance changes.
			TSubclassOf<AHexUnitActor> RepresentativeUnitClass = nullptr;
			for (const TSubclassOf<AHexUnitActor>& SelectedClass : SelectedArmyUnitClasses)
			{
				const AHexUnitActor* SelectedUnit = SelectedClass
					? SelectedClass->GetDefaultObject<AHexUnitActor>()
					: nullptr;

				if (SelectedUnit
					&& SelectedUnit->Faction == FactionConfig.Faction
					&& DoesUnitMatchFactionEffectRole(SelectedUnit, IconConfig.Role))
				{
					RepresentativeUnitClass = SelectedClass;
					break;
				}
			}

			const FArmyFactionEffectBonuses ExactBonuses = CalculateFactionEffectBonusesForArmyUnit(
				SelectedArmyUnitClasses,
				RepresentativeUnitClass
			);

			const auto GetFactionName = [](EHexUnitFaction Faction) -> FString
			{
				switch (Faction)
				{
				case EHexUnitFaction::Kingdom: return TEXT("Kingdom");
				case EHexUnitFaction::Soul: return TEXT("Souls");
				case EHexUnitFaction::Animal: return TEXT("Animals");
				case EHexUnitFaction::Bandits: return TEXT("Bandits");
				default: return TEXT("Faction");
				}
			};

			FString RoleName;
			FString RequirementDescription;
			FString BonusDescription;

			switch (IconConfig.Role)
			{
			case EArmyFactionEffectRole::Tank:
				RoleName = TEXT("Tank");
				RequirementDescription = TEXT("Ram units");
				BonusDescription = FString::Printf(
					TEXT("+%d%% Max HP"),
					FMath::RoundToInt((ExactBonuses.MaxHealthMultiplier - 1.0f) * 100.0f)
				);
				break;

			case EArmyFactionEffectRole::Ranged:
				RoleName = TEXT("Ranged");
				RequirementDescription = TEXT("ranged non-Champion units");
				BonusDescription = FString::Printf(
					TEXT("+%d%% Attack"),
					FMath::RoundToInt((ExactBonuses.AttackDamageMultiplier - 1.0f) * 100.0f)
				);
				break;

			case EArmyFactionEffectRole::Healer:
				RoleName = TEXT("Healer");
				RequirementDescription = TEXT("Healer units");
				BonusDescription = FString::Printf(
					TEXT("+%d%% Healing"),
					FMath::RoundToInt((ExactBonuses.HealAmountMultiplier - 1.0f) * 100.0f)
				);
				break;

			case EArmyFactionEffectRole::Melee:
			default:
				RoleName = TEXT("Melee");
				RequirementDescription = TEXT("melee non-Champion units");
				BonusDescription = FString::Printf(
					TEXT("+%d%% Attack"),
					FMath::RoundToInt((ExactBonuses.AttackDamageMultiplier - 1.0f) * 100.0f)
				);
				break;
			}

			if (ExactBonuses.MovementRangeBonus > 0)
			{
				BonusDescription += TEXT(", +1 MOVE");
			}

			FString TooltipString = FString::Printf(
				TEXT("%s %s Synergy\nBonus: %s\nActive because your army has %d %s of this faction.\nRequires at least %d. Champions do not count."),
				*GetFactionName(FactionConfig.Faction),
				*RoleName,
				*BonusDescription,
				MatchingUnitCount,
				*RequirementDescription,
				RequiredCount
			);

			if (!IconConfig.ToolTipText.IsEmpty())
			{
				TooltipString += TEXT("\n");
				TooltipString += IconConfig.ToolTipText.ToString();
			}

			IconRootSizeBox->SetToolTipText(FText::FromString(TooltipString));
			IconRootSizeBox->SetContent(IconOverlay);

			IconImage->SetBrushFromTexture(IconConfig.IconTexture, true);
			IconImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			if (UOverlaySlot* ImageSlot = IconOverlay->AddChildToOverlay(IconImage))
			{
				ImageSlot->SetHorizontalAlignment(HAlign_Fill);
				ImageSlot->SetVerticalAlignment(VAlign_Fill);
			}

			CountSizeBox->SetWidthOverride(SafeCountBoxSize.X);
			CountSizeBox->SetHeightOverride(SafeCountBoxSize.Y);
			CountSizeBox->SetContent(CountText);

			CountText->SetText(FText::AsNumber(MatchingUnitCount));
			CountText->SetJustification(ETextJustify::Center);
			CountText->SetColorAndOpacity(FSlateColor(FactionEffectCountColor));
			CountText->SetShadowOffset(FVector2D(1.0f, 1.0f));
			CountText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));

			FSlateFontInfo CountFont = FCoreStyle::GetDefaultFontStyle(
				"Bold",
				FMath::Clamp(FactionEffectCountFontSize, 8, 40)
			);
			CountText->SetFont(CountFont);

			if (UOverlaySlot* CountSlot = IconOverlay->AddChildToOverlay(CountSizeBox))
			{
				CountSlot->SetHorizontalAlignment(HAlign_Right);
				CountSlot->SetVerticalAlignment(VAlign_Bottom);
				CountSlot->SetPadding(FactionEffectCountPadding);
			}

			if (UVerticalBoxSlot* EffectSlot = FactionEffectsVerticalBox->AddChildToVerticalBox(IconRootSizeBox))
			{
				EffectSlot->SetHorizontalAlignment(HAlign_Center);
				EffectSlot->SetVerticalAlignment(VAlign_Top);
				EffectSlot->SetPadding(FMargin(
					0.0f,
					0.0f,
					0.0f,
					FMath::Max(0.0f, FactionEffectIconBottomPadding)
				));
			}
		}
	}
}

void UArmyBuilderWidget::UpdateTexts()
{
	if (ArmySlotsTextBlock)
	{
		ArmySlotsTextBlock->SetText(FText::Format(
			NSLOCTEXT("ArmyBuilder", "ArmySlotsFormat", "Army Slots {0} / {1}    Units {2} / {3}"),
			FText::AsNumber(GetUsedArmySlots()),
			FText::AsNumber(GetEffectiveMaxArmySlots()),
			FText::AsNumber(SelectedArmyUnitClasses.Num()),
			FText::AsNumber(FMath::Clamp(MaxSelectedArmyUnits, 3, 5))
		));
	}

	const int32 CurrentArmyPower = GetCurrentArmyPower();
	CachedPreviewArmyPower = CurrentArmyPower;

	if (ArmyPowerTextBlock)
	{
		ArmyPowerTextBlock->SetText(FText::Format(
			NSLOCTEXT("ArmyBuilder", "ArmyPowerFormat", "Army Power {0}"),
			FText::AsNumber(CurrentArmyPower)
		));
	}

	if (ActiveFilterTextBlock)
	{
		ActiveFilterTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("Filter: %s / %s"),
			*GetFactionFilterText(),
			*GetTypeFilterText()
		)));
	}

	if (AvailableCountTextBlock)
	{
		AvailableCountTextBlock->SetText(FText::Format(
			NSLOCTEXT("ArmyBuilder", "AvailableCountFormat", "Available Units: {0}"),
			FText::AsNumber(GetFilteredAvailableUnitCount())
		));
	}

	if (EmptySelectedArmyText)
	{
		EmptySelectedArmyText->SetVisibility(SelectedArmyUnitClasses.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UArmyBuilderWidget::UpdateButtonStates()
{
	if (MainMenuButton)
	{
		MainMenuButton->SetIsEnabled(true);
		MainMenuButton->SetRenderOpacity(1.0f);
	}

	if (ClearArmyButton)
	{
		ClearArmyButton->SetIsEnabled(!SelectedArmyUnitClasses.IsEmpty());
		ClearArmyButton->SetRenderOpacity(SelectedArmyUnitClasses.IsEmpty() ? 0.55f : 1.0f);
	}

	if (SaveArmyButton)
	{
		// Any current composition can be saved, including 0-2 units.
		// Battle readiness is checked separately on the mode select screen.
		SaveArmyButton->SetIsEnabled(true);
		SaveArmyButton->SetRenderOpacity(1.0f);
	}

	if (ShowDeploymentButton)
	{
		const bool bCanShowDeployment = IsSelectedArmyReadyForBattle();
		ShowDeploymentButton->SetIsEnabled(bCanShowDeployment);
		ShowDeploymentButton->SetRenderOpacity(bCanShowDeployment ? 1.0f : 0.45f);
	}
}

void UArmyBuilderWidget::UpdateFilterVisuals()
{
	SetButtonSelectedVisual(KingdomFactionButton, ActiveFactionFilters.Contains(EHexUnitFaction::Kingdom));
	SetButtonSelectedVisual(AnimalFactionButton, ActiveFactionFilters.Contains(EHexUnitFaction::Animal));
	SetButtonSelectedVisual(SoulFactionButton, ActiveFactionFilters.Contains(EHexUnitFaction::Soul));
	SetButtonSelectedVisual(BanditsFactionButton, ActiveFactionFilters.Contains(EHexUnitFaction::Bandits));

	SetButtonSelectedVisual(AllTypeButton, ActiveTypeFilters.IsEmpty());
	SetButtonSelectedVisual(RamTypeButton, ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Ram));
	SetButtonSelectedVisual(ChampionTypeButton, ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Champion));
	SetButtonSelectedVisual(SkirmisherTypeButton, ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Skirmisher));
	SetButtonSelectedVisual(SupportTypeButton, ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Support));
	SetButtonSelectedVisual(HealerTypeButton, ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Healer));
}

void UArmyBuilderWidget::UpdateArmyPresetVisuals()
{
	EnsureArmyPresetStorage();

	UButton* PresetButtons[5] =
	{
		ArmyPreset1Button,
		ArmyPreset2Button,
		ArmyPreset3Button,
		ArmyPreset4Button,
		ArmyPreset5Button
	};

	for (int32 PresetIndex = 0; PresetIndex < 5; ++PresetIndex)
	{
		UButton* Button = PresetButtons[PresetIndex];
		if (!Button)
		{
			continue;
		}

		const bool bSelected = PresetIndex == ActiveArmyPresetIndex;
		const int32 UnitCount = bSelected
			? SelectedArmyUnitClasses.Num()
			: (SavedArmyPresetRecords.IsValidIndex(PresetIndex)
				? SavedArmyPresetRecords[PresetIndex].UnitClasses.Num()
				: 0);

		Button->SetIsEnabled(true);
		Button->SetRenderOpacity(bSelected ? SelectedArmyPresetOpacity : NormalArmyPresetOpacity);
		Button->SetToolTipText(FText::FromString(FString::Printf(
			TEXT("Army template %d%s. Units saved: %d."),
			PresetIndex + 1,
			bSelected ? TEXT(" (active)") : TEXT(""),
			UnitCount
		)));
	}
}

void UArmyBuilderWidget::RefreshAvailableUnitList()
{
	if (!AvailableUnitsWrapBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("Army Builder: AvailableUnitsWrapBox is not bound. Create WrapBox in WBP and name it AvailableUnitsWrapBox."));
		return;
	}

	AvailableUnitsWrapBox->ClearChildren();

	int32 AddedCount = 0;

	if (!UnitCardWidgetClass)
	{
		if (EmptyAvailableUnitsText)
		{
			EmptyAvailableUnitsText->SetVisibility(ESlateVisibility::Visible);
		}

		UE_LOG(LogTemp, Warning, TEXT("Army Builder: UnitCardWidgetClass is not set. Set it to WBP_ArmyUnitCard in BP_ArmyBuilderWidget."));
		return;
	}

	for (const TSubclassOf<AHexUnitActor>& UnitClass : AvailableUnitClasses)
	{
		if (!DoesUnitPassCurrentFilters(UnitClass))
		{
			continue;
		}

		UArmyUnitCardWidget* CardWidget = CreateWidget<UArmyUnitCardWidget>(this, UnitCardWidgetClass);
		if (!CardWidget)
		{
			continue;
		}

		CardWidget->InitializeCard(this, UnitClass);

		if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
		{
			LoadingGameInstance->BindButtonClickSounds(CardWidget);
		}

		CardWidget->SetCanBeAdded(CanAddUnitClassToArmy(UnitClass));

		if (UWrapBoxSlot* WrapBoxSlot = AvailableUnitsWrapBox->AddChildToWrapBox(CardWidget))
		{
			WrapBoxSlot->SetPadding(UnitCardPadding);
		}

		++AddedCount;
	}

	if (EmptyAvailableUnitsText)
	{
		EmptyAvailableUnitsText->SetVisibility(AddedCount <= 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UArmyBuilderWidget::RefreshSelectedArmyList()
{
	if (!SelectedArmyPanel)
	{
		return;
	}

	SelectedArmyPanel->ClearChildren();

	if (!UnitCardWidgetClass)
	{
		return;
	}

	for (int32 Index = 0; Index < SelectedArmyUnitClasses.Num(); ++Index)
	{
		UArmyUnitCardWidget* CardWidget = CreateWidget<UArmyUnitCardWidget>(this, UnitCardWidgetClass);
		if (!CardWidget)
		{
			continue;
		}

		CardWidget->InitializeSelectedCard(this, SelectedArmyUnitClasses[Index], Index);

		if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
		{
			LoadingGameInstance->BindButtonClickSounds(CardWidget);
		}

		if (UPanelSlot* PanelSlot = SelectedArmyPanel->AddChild(CardWidget))
		{
			if (UVerticalBoxSlot* VerticalBoxSlot = Cast<UVerticalBoxSlot>(PanelSlot))
			{
				VerticalBoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
				VerticalBoxSlot->SetHorizontalAlignment(HAlign_Fill);
			}
			else if (UScrollBoxSlot* ScrollBoxSlot = Cast<UScrollBoxSlot>(PanelSlot))
			{
				ScrollBoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
				ScrollBoxSlot->SetHorizontalAlignment(HAlign_Fill);
			}
		}
	}
}

void UArmyBuilderWidget::SaveSelectedArmyForBattle()
{
	CommitSelectedArmyToActivePreset(true);
}

void UArmyBuilderWidget::CommitSelectedArmyToActivePreset(bool bRequestPersistentSave)
{
	CacheAvailableUnitClassesForBattle();

	// Persist the selected composition even when it is not battle-ready.
	// Battle readiness remains a separate check on the mode-select screen.
	NormalizeSelectedProgressForCurrentArmy();

	const TArray<TSubclassOf<AHexUnitActor>> PreviousSavedComposition = SavedPlayerArmyUnitClasses;

	SavedPlayerArmyUnitClasses = SelectedArmyUnitClasses;
	SavedPlayerArmyUnitClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			UClass* UnitClassObject = UnitClass.Get();
			return !IsValid(UnitClassObject) || !UnitClassObject->IsChildOf(AHexUnitActor::StaticClass());
		});

	SavedPlayerArmyUnitProgress = SelectedArmyUnitProgress;
	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);
	StoreProgressListInRoster(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);

	CachedPreviewArmyPower = CalculateArmyPowerForList(SavedPlayerArmyUnitClasses);
	RebuildSavedFactionEffectRecordsFromArmy(SavedPlayerArmyUnitClasses);

	// A formation belongs to an exact ordered composition. Any add/remove/reorder
	// invalidates the old formation instead of silently assigning it to other units.
	const bool bCompositionChanged = PreviousSavedComposition != SavedPlayerArmyUnitClasses;
	if (bCompositionChanged ||
		(SavedPlayerArmyDeploymentSlots.Num() > 0 &&
			!IsDeploymentSlotListValidForArmy(SavedPlayerArmyDeploymentSlots, SavedPlayerArmyUnitClasses.Num())))
	{
		SavedPlayerArmyDeploymentSlots.Reset();
	}

	StoreActiveRuntimeArmyInPreset();

	if (bRequestPersistentSave)
	{
		RequestPersistentAccountSave();
	}
}

void UArmyBuilderWidget::SelectArmyPreset(int32 NewPresetIndex)
{
	const int32 SafePresetIndex = FMath::Clamp(NewPresetIndex, 0, 4);
	if (SafePresetIndex == ActiveArmyPresetIndex)
	{
		UpdateArmyPresetVisuals();
		return;
	}

	// Preserve the current draft automatically before changing templates. This also
	// allows incomplete templates (0-2 units) without forcing extra confirmation.
	CommitSelectedArmyToActivePreset(false);

	if (ActiveDeploymentWidget)
	{
		ActiveDeploymentWidget->RemoveFromParent();
		ActiveDeploymentWidget = nullptr;
	}

	ActiveArmyPresetIndex = SafePresetIndex;
	LoadPresetIntoRuntime(ActiveArmyPresetIndex);
	LoadActivePresetIntoEditor();

	RequestPersistentAccountSave();
	UpdateAllVisuals();

	UE_LOG(LogTemp, Log, TEXT("Army template selected: %d Units=%d Deployment=%d."),
		ActiveArmyPresetIndex + 1,
		SelectedArmyUnitClasses.Num(),
		SavedPlayerArmyDeploymentSlots.Num()
	);
}

void UArmyBuilderWidget::LoadActivePresetIntoEditor()
{
	SelectedArmyUnitClasses = SavedPlayerArmyUnitClasses;
	SelectedArmyUnitClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			UClass* UnitClassObject = UnitClass.Get();
			return !IsValid(UnitClassObject) || !UnitClassObject->IsChildOf(AHexUnitActor::StaticClass());
		});

	SelectedArmyUnitProgress = SavedPlayerArmyUnitProgress;
	NormalizeSelectedProgressForCurrentArmy();
	StoreProgressListInRoster(SelectedArmyUnitClasses, SelectedArmyUnitProgress);
}

void UArmyBuilderWidget::RequestPersistentAccountSave() const
{
	UGameLoadingGameInstance* LoadingGameInstance = GetWorld()
		? Cast<UGameLoadingGameInstance>(GetWorld()->GetGameInstance())
		: nullptr;

	if (LoadingGameInstance)
	{
		LoadingGameInstance->SaveAccountProgression();
	}
}

void UArmyBuilderWidget::CacheAvailableUnitClassesForBattle()
{
	SavedAvailableUnitClasses.Reset();

	for (const TSubclassOf<AHexUnitActor>& UnitClass : AvailableUnitClasses)
	{
		const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (!DefaultUnit)
		{
			continue;
		}

		SavedAvailableUnitClasses.AddUnique(UnitClass);
	}
}

void UArmyBuilderWidget::NormalizeSelectedProgressForCurrentArmy()
{
	NormalizeProgressListForArmy(SelectedArmyUnitClasses, SelectedArmyUnitProgress);

	// If this unit class already has better session progress in the roster memory,
	// do not allow the selected slot to silently fall back to an older/default value.
	for (int32 Index = 0; Index < SelectedArmyUnitClasses.Num(); ++Index)
	{
		if (!SelectedArmyUnitProgress.IsValidIndex(Index))
		{
			continue;
		}

		const FArmyBuilderUnitProgress KnownProgress = MakeProgressForAddedUnit(SelectedArmyUnitClasses[Index]);
		if (IsProgressBetterOrEqual(KnownProgress, SelectedArmyUnitProgress[Index]))
		{
			SelectedArmyUnitProgress[Index] = KnownProgress;
		}
	}
}

void UArmyBuilderWidget::EnsureArmyPresetStorage()
{
	const bool bHadNoPresetRecords = SavedArmyPresetRecords.IsEmpty();

	if (SavedArmyPresetRecords.Num() > 5)
	{
		SavedArmyPresetRecords.SetNum(5);
	}
	else
	{
		while (SavedArmyPresetRecords.Num() < 5)
		{
			SavedArmyPresetRecords.AddDefaulted();
		}
	}

	ActiveArmyPresetIndex = FMath::Clamp(ActiveArmyPresetIndex, 0, 4);

	// Session migration safety: if older code populated the legacy active army before
	// preset data existed, preserve that army as template 1.
	if (bHadNoPresetRecords &&
		(!SavedPlayerArmyUnitClasses.IsEmpty() || !SavedPlayerArmyDeploymentSlots.IsEmpty()))
	{
		FAccountArmyPresetRecord& FirstPreset = SavedArmyPresetRecords[0];

		for (const TSubclassOf<AHexUnitActor>& UnitClass : SavedPlayerArmyUnitClasses)
		{
			UClass* UnitClassObject = UnitClass.Get();
			if (IsValid(UnitClassObject) && UnitClassObject->IsChildOf(AHexUnitActor::StaticClass()))
			{
				FirstPreset.UnitClasses.Add(TSoftClassPtr<AHexUnitActor>(UnitClassObject));
			}
		}

		for (const FArmyBuilderDeploymentSlot& DeploymentSlot : SavedPlayerArmyDeploymentSlots)
		{
			FAccountDeploymentSlotRecord& Record = FirstPreset.DeploymentSlots.AddDefaulted_GetRef();
			Record.UnitIndex = DeploymentSlot.UnitIndex;
			Record.Q = DeploymentSlot.Q;
			Record.R = DeploymentSlot.R;
		}

		RebuildSavedFactionEffectRecordsFromArmy(SavedPlayerArmyUnitClasses);
		FirstPreset.FactionEffects = SavedFactionEffectRecords;
		ActiveArmyPresetIndex = 0;
	}
}

bool UArmyBuilderWidget::TryConvertPersistentDeployment(
	const TArray<FAccountDeploymentSlotRecord>& Records,
	int32 UnitCount,
	TArray<FArmyBuilderDeploymentSlot>& OutDeploymentSlots
)
{
	OutDeploymentSlots.Reset();

	if (UnitCount <= 0 || Records.Num() != UnitCount)
	{
		return false;
	}

	TSet<int32> UsedUnitIndexes;
	TSet<FIntPoint> UsedCoords;

	for (const FAccountDeploymentSlotRecord& Record : Records)
	{
		if (Record.UnitIndex < 0 || Record.UnitIndex >= UnitCount)
		{
			OutDeploymentSlots.Reset();
			return false;
		}

		const FIntPoint Coord(Record.Q, Record.R);
		if (UsedUnitIndexes.Contains(Record.UnitIndex) || UsedCoords.Contains(Coord))
		{
			OutDeploymentSlots.Reset();
			return false;
		}

		UsedUnitIndexes.Add(Record.UnitIndex);
		UsedCoords.Add(Coord);
		OutDeploymentSlots.Add(FArmyBuilderDeploymentSlot(Record.UnitIndex, Record.Q, Record.R));
	}

	return OutDeploymentSlots.Num() == UnitCount;
}

void UArmyBuilderWidget::StoreActiveRuntimeArmyInPreset()
{
	EnsureArmyPresetStorage();

	FAccountArmyPresetRecord& ActivePreset = SavedArmyPresetRecords[ActiveArmyPresetIndex];
	ActivePreset.UnitClasses.Reset();
	ActivePreset.DeploymentSlots.Reset();

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SavedPlayerArmyUnitClasses)
	{
		if (ActivePreset.UnitClasses.Num() >= 5)
		{
			break;
		}

		UClass* UnitClassObject = UnitClass.Get();
		if (!IsValid(UnitClassObject) || !UnitClassObject->IsChildOf(AHexUnitActor::StaticClass()))
		{
			continue;
		}

		ActivePreset.UnitClasses.Add(TSoftClassPtr<AHexUnitActor>(UnitClassObject));
	}

	if (SavedPlayerArmyDeploymentSlots.Num() == SavedPlayerArmyUnitClasses.Num() &&
		!SavedPlayerArmyUnitClasses.IsEmpty())
	{
		TSet<int32> UsedUnitIndexes;
		TSet<FIntPoint> UsedCoords;
		bool bValidDeployment = true;

		for (const FArmyBuilderDeploymentSlot& DeploymentSlot : SavedPlayerArmyDeploymentSlots)
		{
			const FIntPoint Coord(DeploymentSlot.Q, DeploymentSlot.R);
			if (DeploymentSlot.UnitIndex < 0 ||
				DeploymentSlot.UnitIndex >= SavedPlayerArmyUnitClasses.Num() ||
				UsedUnitIndexes.Contains(DeploymentSlot.UnitIndex) ||
				UsedCoords.Contains(Coord))
			{
				bValidDeployment = false;
				break;
			}

			UsedUnitIndexes.Add(DeploymentSlot.UnitIndex);
			UsedCoords.Add(Coord);

			FAccountDeploymentSlotRecord& Record = ActivePreset.DeploymentSlots.AddDefaulted_GetRef();
			Record.UnitIndex = DeploymentSlot.UnitIndex;
			Record.Q = DeploymentSlot.Q;
			Record.R = DeploymentSlot.R;
		}

		if (!bValidDeployment)
		{
			ActivePreset.DeploymentSlots.Reset();
		}
	}

	// Composition is authoritative. Store a fresh snapshot so every template keeps
	// the faction effects matching its own units even after balance-rule changes.
	RebuildSavedFactionEffectRecordsFromArmy(SavedPlayerArmyUnitClasses);
	ActivePreset.FactionEffects = SavedFactionEffectRecords;
}

void UArmyBuilderWidget::LoadPresetIntoRuntime(int32 PresetIndex)
{
	EnsureArmyPresetStorage();
	ActiveArmyPresetIndex = FMath::Clamp(PresetIndex, 0, 4);

	SavedPlayerArmyUnitClasses.Reset();
	SavedPlayerArmyDeploymentSlots.Reset();
	SavedPlayerArmyUnitProgress.Reset();
	SavedFactionEffectRecords.Reset();

	const FAccountArmyPresetRecord& Preset = SavedArmyPresetRecords[ActiveArmyPresetIndex];

	for (const TSoftClassPtr<AHexUnitActor>& SoftUnitClass : Preset.UnitClasses)
	{
		if (SavedPlayerArmyUnitClasses.Num() >= 5)
		{
			break;
		}

		UClass* LoadedClass = SoftUnitClass.LoadSynchronous();
		if (!IsValid(LoadedClass) || !LoadedClass->IsChildOf(AHexUnitActor::StaticClass()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Army template %d skipped missing unit class: %s"),
				ActiveArmyPresetIndex + 1,
				*SoftUnitClass.ToSoftObjectPath().ToString()
			);
			continue;
		}

		SavedPlayerArmyUnitClasses.Add(LoadedClass);
	}

	for (const TSubclassOf<AHexUnitActor>& UnitClass : SavedPlayerArmyUnitClasses)
	{
		SavedPlayerArmyUnitProgress.Add(MakeProgressForAddedUnit(UnitClass));
	}
	NormalizeProgressListForArmy(SavedPlayerArmyUnitClasses, SavedPlayerArmyUnitProgress);

	TryConvertPersistentDeployment(
		Preset.DeploymentSlots,
		SavedPlayerArmyUnitClasses.Num(),
		SavedPlayerArmyDeploymentSlots
	);

	RebuildSavedFactionEffectRecordsFromArmy(SavedPlayerArmyUnitClasses);

	CachedPreviewArmyPower = 0;
	for (int32 Index = 0; Index < SavedPlayerArmyUnitClasses.Num(); ++Index)
	{
		const AHexUnitActor* DefaultUnit = SavedPlayerArmyUnitClasses[Index]
			? SavedPlayerArmyUnitClasses[Index]->GetDefaultObject<AHexUnitActor>()
			: nullptr;

		if (DefaultUnit && SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			CachedPreviewArmyPower += DefaultUnit->GetArmyPowerValueForLevel(
				SavedPlayerArmyUnitProgress[Index].Level
			);
		}
	}

	// Sanitize the soft record after loading (missing classes/invalid deployment are removed).
	StoreActiveRuntimeArmyInPreset();
}

void UArmyBuilderWidget::NormalizeProgressListForArmy(const TArray<TSubclassOf<AHexUnitActor>>& ArmyClasses, TArray<FArmyBuilderUnitProgress>& ProgressList)
{
	TArray<FArmyBuilderUnitProgress> NormalizedProgress;
	NormalizedProgress.Reserve(ArmyClasses.Num());

	for (int32 Index = 0; Index < ArmyClasses.Num(); ++Index)
	{
		const TSubclassOf<AHexUnitActor> UnitClass = ArmyClasses[Index];
		FArmyBuilderUnitProgress Progress = ProgressList.IsValidIndex(Index)
			? ProgressList[Index]
			: MakeDefaultUnitProgress(UnitClass);

		Progress.UnitClass = UnitClass;
		Progress.Level = FMath::Clamp(Progress.Level, 1, AHexUnitActor::GetMaxProgressionLevel());

		if (Progress.Level >= AHexUnitActor::GetMaxProgressionLevel())
		{
			Progress.CurrentExperience = 0;
		}
		else
		{
			Progress.CurrentExperience = FMath::Max(0, Progress.CurrentExperience);
		}

		NormalizedProgress.Add(Progress);
	}

	ProgressList = NormalizedProgress;
}

void UArmyBuilderWidget::AddExperienceToProgress(FArmyBuilderUnitProgress& Progress, int32 ExperienceToAdd)
{
	Progress.Level = FMath::Clamp(Progress.Level, 1, AHexUnitActor::GetMaxProgressionLevel());

	if (Progress.Level >= AHexUnitActor::GetMaxProgressionLevel())
	{
		Progress.Level = AHexUnitActor::GetMaxProgressionLevel();
		Progress.CurrentExperience = 0;
		return;
	}

	// Do not level up here. Experience keeps accumulating, even above the current
	// threshold, and is consumed one threshold at a time by TryUpgradeUnitClass().
	Progress.CurrentExperience = FMath::Max(0, Progress.CurrentExperience) + FMath::Max(0, ExperienceToAdd);
}

float UArmyBuilderWidget::GetOutcomeExperienceMultiplier(EArmyBattleExperienceOutcome Outcome)
{
	switch (Outcome)
	{
	case EArmyBattleExperienceOutcome::Victory:
		return 1.0f;

	case EArmyBattleExperienceOutcome::Surrender:
		return 0.3f;

	case EArmyBattleExperienceOutcome::Defeat:
	default:
		return 0.5f;
	}
}

bool UArmyBuilderWidget::IsArmyClassListReadyForBattle(const TArray<TSubclassOf<AHexUnitActor>>& ArmyClasses)
{
	if (ArmyClasses.Num() < 3 || ArmyClasses.Num() > 5)
	{
		return false;
	}

	int32 UsedSlots = 0;
	int32 ChampionCount = 0;
	for (const TSubclassOf<AHexUnitActor>& UnitClass : ArmyClasses)
	{
		const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (!DefaultUnit)
		{
			return false;
		}

		UsedSlots += FMath::Max(0, DefaultUnit->OccupiedSlots);
		if (DefaultUnit->UnitType == EHexUnitType::Champion)
		{
			++ChampionCount;
		}
	}

	// Static validation is used outside the Army Builder widget, for example in the mode select screen.
	// The builder itself still uses its BP slot rules before saving.
	return UsedSlots <= 15 && ChampionCount <= 1;
}

bool UArmyBuilderWidget::IsUnitChampion(TSubclassOf<AHexUnitActor> UnitClass) const
{
	const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	return DefaultUnit && DefaultUnit->UnitType == EHexUnitType::Champion;
}

bool UArmyBuilderWidget::IsDeploymentSlotListValidForArmy(const TArray<FArmyBuilderDeploymentSlot>& DeploymentSlots, int32 UnitCount) const
{
	if (UnitCount <= 0 || DeploymentSlots.Num() != UnitCount)
	{
		return false;
	}

	TSet<int32> UsedUnitIndexes;
	TSet<FIntPoint> UsedCoords;

	for (const FArmyBuilderDeploymentSlot& DeploymentSlot : DeploymentSlots)
	{
		if (DeploymentSlot.UnitIndex < 0 || DeploymentSlot.UnitIndex >= UnitCount)
		{
			return false;
		}

		if (UsedUnitIndexes.Contains(DeploymentSlot.UnitIndex))
		{
			return false;
		}

		const FIntPoint Coord(DeploymentSlot.Q, DeploymentSlot.R);
		if (UsedCoords.Contains(Coord))
		{
			return false;
		}

		UsedUnitIndexes.Add(DeploymentSlot.UnitIndex);
		UsedCoords.Add(Coord);
	}

	return UsedUnitIndexes.Num() == UnitCount;
}

int32 UArmyBuilderWidget::GetUnitSlotCost(TSubclassOf<AHexUnitActor> UnitClass) const
{
	const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	if (!DefaultUnit)
	{
		return 0;
	}

	return FMath::Max(0, DefaultUnit->OccupiedSlots);
}


int32 UArmyBuilderWidget::GetUnitPowerValue(TSubclassOf<AHexUnitActor> UnitClass) const
{
	const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
	return DefaultUnit ? DefaultUnit->GetArmyPowerValueForLevel(1) : 0;
}

int32 UArmyBuilderWidget::CalculateArmyPowerForList(const TArray<TSubclassOf<AHexUnitActor>>& UnitClasses) const
{
	int32 TotalPower = 0;

	for (int32 Index = 0; Index < UnitClasses.Num(); ++Index)
	{
		const TSubclassOf<AHexUnitActor>& UnitClass = UnitClasses[Index];
		const AHexUnitActor* DefaultUnit = UnitClass ? UnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (!DefaultUnit)
		{
			continue;
		}

		int32 Level = 1;
		if (SelectedArmyUnitProgress.IsValidIndex(Index))
		{
			Level = SelectedArmyUnitProgress[Index].Level;
		}
		else if (SavedPlayerArmyUnitClasses.IsValidIndex(Index) && SavedPlayerArmyUnitClasses[Index] == UnitClass && SavedPlayerArmyUnitProgress.IsValidIndex(Index))
		{
			Level = SavedPlayerArmyUnitProgress[Index].Level;
		}

		TotalPower += DefaultUnit->GetArmyPowerValueForLevel(Level);
	}

	return TotalPower;
}

int32 UArmyBuilderWidget::GetSlotLimitForUnitCount(int32 UnitCountLimit) const
{
	switch (FMath::Clamp(UnitCountLimit, 3, 5))
	{
	case 3:
		return FMath::Max(1, SlotLimitFor3Units);

	case 4:
		return FMath::Max(1, SlotLimitFor4Units);

	case 5:
	default:
		return FMath::Max(1, SlotLimitFor5Units);
	}
}

int32 UArmyBuilderWidget::GetFilteredAvailableUnitCount() const
{
	int32 Count = 0;

	for (const TSubclassOf<AHexUnitActor>& UnitClass : AvailableUnitClasses)
	{
		if (DoesUnitPassCurrentFilters(UnitClass))
		{
			++Count;
		}
	}

	return Count;
}

FString UArmyBuilderWidget::GetFactionFilterText() const
{
	if (ActiveFactionFilters.IsEmpty())
	{
		return TEXT("All Factions");
	}

	TArray<FString> FilterNames;
	FilterNames.Reserve(ActiveFactionFilters.Num());

	if (ActiveFactionFilters.Contains(EHexUnitFaction::Kingdom))
	{
		FilterNames.Add(TEXT("Kingdom"));
	}

	if (ActiveFactionFilters.Contains(EHexUnitFaction::Animal))
	{
		FilterNames.Add(TEXT("Animals"));
	}

	if (ActiveFactionFilters.Contains(EHexUnitFaction::Soul))
	{
		FilterNames.Add(TEXT("Souls"));
	}

	if (ActiveFactionFilters.Contains(EHexUnitFaction::Bandits))
	{
		FilterNames.Add(TEXT("Bandits"));
	}

	return FString::Join(FilterNames, TEXT(", "));
}

FString UArmyBuilderWidget::GetTypeFilterText() const
{
	if (ActiveTypeFilters.IsEmpty())
	{
		return TEXT("All");
	}

	TArray<FString> FilterNames;
	FilterNames.Reserve(ActiveTypeFilters.Num());

	if (ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Ram))
	{
		FilterNames.Add(TEXT("Ram"));
	}

	if (ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Champion))
	{
		FilterNames.Add(TEXT("Champion"));
	}

	if (ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Skirmisher))
	{
		FilterNames.Add(TEXT("Skirmisher"));
	}

	if (ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Support))
	{
		FilterNames.Add(TEXT("Support"));
	}

	if (ActiveTypeFilters.Contains(EArmyUnitTypeFilter::Healer))
	{
		FilterNames.Add(TEXT("Healer"));
	}

	return FString::Join(FilterNames, TEXT(", "));
}

bool UArmyBuilderWidget::ConvertTypeFilterToUnitType(EArmyUnitTypeFilter TypeFilter, EHexUnitType& OutUnitType) const
{
	switch (TypeFilter)
	{
	case EArmyUnitTypeFilter::Ram:
		OutUnitType = EHexUnitType::Ram;
		return true;

	case EArmyUnitTypeFilter::Champion:
		OutUnitType = EHexUnitType::Champion;
		return true;

	case EArmyUnitTypeFilter::Skirmisher:
		OutUnitType = EHexUnitType::Skirmisher;
		return true;

	case EArmyUnitTypeFilter::Support:
		OutUnitType = EHexUnitType::Support;
		return true;

	case EArmyUnitTypeFilter::Healer:
		OutUnitType = EHexUnitType::Healer;
		return true;

	case EArmyUnitTypeFilter::All:
	default:
		return false;
	}
}

void UArmyBuilderWidget::SetButtonSelectedVisual(UButton* Button, bool bSelected) const
{
	if (!Button)
	{
		return;
	}

	Button->SetIsEnabled(true);
	Button->SetRenderOpacity(bSelected ? SelectedFilterOpacity : NormalFilterOpacity);
}
// SUPPORT HEALER NEW v3 - generated 2026-06-20, contains bCanHeal/HealAmount/HealMontage and InHealAmount fix
#include "HexUnitActor.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Blueprint/UserWidget.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ProgressBar.h"
#include "Styling/SlateTypes.h"
#include "Components/TextBlock.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

AHexUnitActor::AHexUnitActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	UnitMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UnitMesh"));
	UnitMesh->SetupAttachment(SceneRoot);

	HealthBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidget"));
	HealthBarWidget->SetupAttachment(SceneRoot);
	HealthBarWidget->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidget->SetDrawSize(HealthBarDrawSize);
	HealthBarWidget->SetRelativeLocation(FVector(0.0f, 0.0f, HealthBarHeight));
	HealthBarWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthBarWidget->SetCollisionResponseToAllChannels(ECR_Ignore);
	HealthBarWidget->SetGenerateOverlapEvents(false);

	//      :       trace                               ,                      .
	UnitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UnitMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	UnitMesh->SetGenerateOverlapEvents(false);
}

void AHexUnitActor::BeginPlay()
{
	Super::BeginPlay();

	SetProgressionState(UnitLevel, CurrentExperience);
	ApplyProgressionStatsToRuntimeUnit();
	CurrentHealth = MaxHealth;
	bIsDead = false;
	bIsActionLocked = false;
	bIsHitReactionLocked = false;
	bLastStandActive = false;
	RemainingLastStandTurns = 0;
	RemainingChampionAbilityCooldownTurns = 0;
	bMarkedForDeathActive = false;
	RemainingMarkedForDeathTurns = 0;
	ActiveMarkedForDeathDamageIncreasePercent = 0.0f;
	bMarkedForDeathCountdownStarted = false;

	InitializeSummonedLifetime();

	//                    ,      Blueprint-                                 
	//               collision-                    .
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (!Component)
		{
			continue;
		}

		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetGenerateOverlapEvents(false);
	}

	ApplyInitialTeamRotation();
	UpdateHealthBarWidget();
}

void AHexUnitActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		StopMovement();
		return;
	}

	AdvanceMovement(DeltaTime);
}

void AHexUnitActor::SetGridCoord(int32 Q, int32 R)
{
	CurrentQ = Q;
	CurrentR = R;
}

FIntPoint AHexUnitActor::GetGridCoord() const
{
	return FIntPoint(CurrentQ, CurrentR);
}

void AHexUnitActor::MoveAlongWorldPath(const TArray<FVector>& WorldPath)
{
	if (bIsDead)
	{
		StopMovement();
		return;
	}

	if (!CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Move blocked: unit is busy. Unit=%s"), *GetNameSafe(this));
		return;
	}

	PathPoints = WorldPath;

	bIsMoving = PathPoints.Num() > 0;
	VisualSpeed = bIsMoving ? MoveSpeed : 0.0f;
}

void AHexUnitActor::StopMovement()
{
	PathPoints.Empty();

	bIsMoving = false;
	VisualSpeed = 0.0f;
}


bool AHexUnitActor::IsEnemyFor(const AHexUnitActor* OtherUnit) const
{
	if (!IsValid(OtherUnit))
	{
		return false;
	}

	return Team != OtherUnit->Team;
}

bool AHexUnitActor::IsAllyFor(const AHexUnitActor* OtherUnit) const
{
	if (!IsValid(OtherUnit))
	{
		return false;
	}

	return Team == OtherUnit->Team;
}

FText AHexUnitActor::GetFactionDisplayText() const
{
	switch (Faction)
	{
	case EHexUnitFaction::Kingdom:
		return FText::FromString(TEXT("Kingdom"));

	case EHexUnitFaction::Soul:
		return FText::FromString(TEXT("Souls"));

	case EHexUnitFaction::Animal:
		return FText::FromString(TEXT("Animals"));

	case EHexUnitFaction::Bandits:
		return FText::FromString(TEXT("Bandits"));

	default:
		return FText::FromString(TEXT("Unknown"));
	}
}

int32 AHexUnitActor::GetMaxProgressionLevel()
{
	return 15;
}

int32 AHexUnitActor::GetCombatStatScale()
{
	return 10;
}

int32 AHexUnitActor::GetExperienceToNextLevelForLevel(int32 Level)
{
	const int32 SafeLevel = FMath::Clamp(Level, 1, GetMaxProgressionLevel());
	return 200 + (SafeLevel - 1) * 100;
}

int32 AHexUnitActor::GetUpgradeCoinCostForLevel(int32 Level)
{
	const int32 SafeLevel = FMath::Clamp(Level, 1, GetMaxProgressionLevel());
	if (SafeLevel >= GetMaxProgressionLevel())
	{
		return 0;
	}

	// 1 -> 2: 50 coins, 2 -> 3: 75 coins, ... 14 -> 15: 375 coins.
	return 25 * (SafeLevel + 1);
}

int32 AHexUnitActor::GetScaledStatForLevel(int32 BaseValue, int32 Level)
{
	const int32 SafeBaseValue = FMath::Max(0, BaseValue);
	if (SafeBaseValue <= 0)
	{
		return 0;
	}

	const int32 SafeLevel = FMath::Clamp(Level, 1, GetMaxProgressionLevel());
	const float Multiplier = FMath::Pow(1.04f, static_cast<float>(SafeLevel - 1));
	return FMath::Max(1, FMath::RoundToInt(static_cast<float>(SafeBaseValue) * Multiplier));
}

int32 AHexUnitActor::GetClampedUnitLevel() const
{
	return FMath::Clamp(UnitLevel, 1, GetMaxProgressionLevel());
}

int32 AHexUnitActor::GetScaledMaxHealthForLevel(int32 Level) const
{
	const int32 BaseHealth = bRuntimeBaseStatsCaptured ? RuntimeBaseMaxHealth : MaxHealth;
	return GetScaledStatForLevel(BaseHealth * GetCombatStatScale(), Level);
}

int32 AHexUnitActor::GetScaledAttackDamageForLevel(int32 Level) const
{
	const int32 BaseAttackDamage = bRuntimeBaseStatsCaptured ? RuntimeBaseAttackDamage : AttackDamage;
	return GetScaledStatForLevel(BaseAttackDamage * GetCombatStatScale(), Level);
}

void AHexUnitActor::SetProgressionState(int32 NewLevel, int32 NewCurrentExperience)
{
	UnitLevel = FMath::Clamp(NewLevel, 1, GetMaxProgressionLevel());

	if (UnitLevel >= GetMaxProgressionLevel())
	{
		CurrentExperience = 0;
		return;
	}

	CurrentExperience = FMath::Max(0, NewCurrentExperience);
}

void AHexUnitActor::ApplyProgressionStatsToRuntimeUnit()
{
	if (!bRuntimeBaseStatsCaptured)
	{
		RuntimeBaseMaxHealth = FMath::Max(1, MaxHealth);
		RuntimeBaseAttackDamage = FMath::Max(0, AttackDamage);
		RuntimeBaseHealAmount = FMath::Max(0, HealAmount);
		bRuntimeBaseStatsCaptured = true;
	}

	UnitLevel = GetClampedUnitLevel();
	MaxUnitLevel = GetMaxProgressionLevel();
	ExperienceToNextLevelAtLevel1 = 200;
	ExperienceToNextLevelIncreasePerLevel = 100;

	MaxHealth = GetScaledStatForLevel(RuntimeBaseMaxHealth * GetCombatStatScale(), UnitLevel);
	AttackDamage = GetScaledStatForLevel(RuntimeBaseAttackDamage * GetCombatStatScale(), UnitLevel);
	if (bCanHeal)
	{
		HealAmount = RuntimeBaseHealAmount * GetCombatStatScale();
	}
	CurrentHealth = FMath::Clamp(CurrentHealth, 0, MaxHealth);
}

int32 AHexUnitActor::GetExperienceToNextLevel() const
{
	if (GetClampedUnitLevel() >= GetMaxProgressionLevel())
	{
		return 0;
	}

	return GetExperienceToNextLevelForLevel(GetClampedUnitLevel());
}

float AHexUnitActor::GetExperienceProgressPercent() const
{
	if (GetClampedUnitLevel() >= GetMaxProgressionLevel())
	{
		return 1.0f;
	}

	const int32 RequiredExperience = GetExperienceToNextLevel();
	if (RequiredExperience <= 0)
	{
		return 0.0f;
	}

	return FMath::Clamp(static_cast<float>(FMath::Max(0, CurrentExperience)) / static_cast<float>(RequiredExperience), 0.0f, 1.0f);
}

int32 AHexUnitActor::GetArmyPowerValueForLevel(int32 Level) const
{
	if (bUseManualArmyPower)
	{
		return FMath::Max(0, ManualArmyPower);
	}

	const int32 EffectiveHealth = GetScaledMaxHealthForLevel(Level);
	const int32 EffectiveDamage = GetScaledAttackDamageForLevel(Level);
	const int32 BaseHealForPower = bRuntimeBaseStatsCaptured ? RuntimeBaseHealAmount : (bCanHeal ? FMath::Max(0, HealAmount) : 0);
	const float StatScale = static_cast<float>(FMath::Max(1, GetCombatStatScale()));

	// Keep Army Power on the pre-x10 balance scale.
	float PowerValue = 0.0f;
	PowerValue += (static_cast<float>(EffectiveHealth) / StatScale) * 0.5f;
	PowerValue += (static_cast<float>(EffectiveDamage) / StatScale) * 4.0f;
	PowerValue += static_cast<float>(BaseHealForPower) * 3.0f;
	PowerValue += static_cast<float>(FMath::Max(0, MovementRange)) * 6.0f;
	PowerValue += static_cast<float>(FMath::Max(0, AttackRange)) * 8.0f;
	PowerValue += static_cast<float>(FMath::Max(0, OccupiedSlots)) * 10.0f;

	switch (UnitType)
	{
	case EHexUnitType::Champion:
		PowerValue += 35.0f;
		break;

	case EHexUnitType::Ram:
		PowerValue += 25.0f;
		break;

	case EHexUnitType::Healer:
		PowerValue += 20.0f;
		break;

	case EHexUnitType::Support:
		PowerValue += 20.0f;
		break;

	case EHexUnitType::Skirmisher:
	default:
		break;
	}

	return FMath::Max(0, FMath::RoundToInt(PowerValue));
}

int32 AHexUnitActor::GetArmyPowerValue() const
{
	return GetArmyPowerValueForLevel(GetClampedUnitLevel());
}

bool AHexUnitActor::CanUseChampionAbility() const
{
	return UnitType == EHexUnitType::Champion && bCanUseChampionAbility;
}

bool AHexUnitActor::IsChampionAbilityEffectActive() const
{
	if (!CanUseChampionAbility())
	{
		return false;
	}

	if (ChampionAbilityType == EHexChampionAbilityType::LastStand)
	{
		return bLastStandActive && RemainingLastStandTurns > 0;
	}

	return false;
}

bool AHexUnitActor::IsChampionAbilityOnCooldown() const
{
	return CanUseChampionAbility() && RemainingChampionAbilityCooldownTurns > 0;
}

int32 AHexUnitActor::GetRemainingChampionAbilityCooldownTurns() const
{
	return FMath::Max(0, RemainingChampionAbilityCooldownTurns);
}

void AHexUnitActor::StartChampionAbilityCooldown()
{
	if (!CanUseChampionAbility())
	{
		RemainingChampionAbilityCooldownTurns = 0;
		return;
	}

	RemainingChampionAbilityCooldownTurns = FMath::Max(0, ChampionAbilityCooldownTurns);

	UE_LOG(LogTemp, Log, TEXT("Champion ability cooldown started. Unit=%s Cooldown=%d"),
		*GetNameSafe(this),
		RemainingChampionAbilityCooldownTurns
	);
}

void AHexUnitActor::ReduceChampionAbilityCooldownAtOwnerTurnStart()
{
	if (RemainingChampionAbilityCooldownTurns <= 0)
	{
		RemainingChampionAbilityCooldownTurns = 0;
		return;
	}

	RemainingChampionAbilityCooldownTurns = FMath::Max(0, RemainingChampionAbilityCooldownTurns - 1);

	UE_LOG(LogTemp, Log, TEXT("Champion ability cooldown tick. Unit=%s RemainingCooldown=%d"),
		*GetNameSafe(this),
		RemainingChampionAbilityCooldownTurns
	);
}

bool AHexUnitActor::CanActivateChampionAbility() const
{
	return CanUseChampionAbility() && !IsChampionAbilityEffectActive() && !IsChampionAbilityOnCooldown();
}

bool AHexUnitActor::IsChampionAbilitySummon() const
{
	return CanUseChampionAbility() && ChampionAbilityType == EHexChampionAbilityType::Summon;
}

bool AHexUnitActor::IsChampionAbilityLastStand() const
{
	return CanUseChampionAbility() && ChampionAbilityType == EHexChampionAbilityType::LastStand;
}

bool AHexUnitActor::IsChampionAbilityMarkedForDeath() const
{
	return CanUseChampionAbility() && ChampionAbilityType == EHexChampionAbilityType::MarkedForDeath;
}

bool AHexUnitActor::ActivateLastStand()
{
	if (bIsDead)
	{
		return false;
	}

	if (!IsChampionAbilityLastStand())
	{
		return false;
	}

	if (bLastStandActive && RemainingLastStandTurns > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Last Stand activation blocked: ability is already active. Unit=%s RemainingTurns=%d"),
			*GetNameSafe(this),
			RemainingLastStandTurns
		);
		return false;
	}

	if (IsChampionAbilityOnCooldown())
	{
		UE_LOG(LogTemp, Log, TEXT("Last Stand activation blocked: ability is on cooldown. Unit=%s RemainingCooldown=%d"),
			*GetNameSafe(this),
			RemainingChampionAbilityCooldownTurns
		);
		return false;
	}

	bLastStandActive = true;
	RemainingLastStandTurns = FMath::Max(1, LastStandDurationTurns);

	UpdateHealthBarWidget();
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	UE_LOG(LogTemp, Log, TEXT("Last Stand activated. Unit=%s Duration=%d"),
		*GetNameSafe(this),
		RemainingLastStandTurns
	);

	return true;
}

bool AHexUnitActor::TrySurviveLethalDamageWithLastStand()
{
	if (bIsDead)
	{
		return false;
	}

	if (!bLastStandActive)
	{
		return false;
	}

	CurrentHealth = FMath::Clamp(LastStandSurviveHealth, 1, MaxHealth);

	if (bLastStandConsumedOnLethalDamage)
	{
		bLastStandActive = false;
		RemainingLastStandTurns = 0;
	}

	UpdateHealthBarWidget();
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	PlayChampionAbilityVFX(this, GetActorLocation());
	PlayHitReaction();

	UE_LOG(LogTemp, Log, TEXT("Last Stand saved unit from lethal damage. Unit=%s HP=%d/%d"),
		*GetNameSafe(this),
		CurrentHealth,
		MaxHealth
	);

	return true;
}

void AHexUnitActor::ReduceLastStandDurationAtOwnerTurnStart()
{
	if (!bLastStandActive)
	{
		return;
	}

	RemainingLastStandTurns = FMath::Max(0, RemainingLastStandTurns - 1);

	if (RemainingLastStandTurns <= 0)
	{
		bLastStandActive = false;

		UE_LOG(LogTemp, Log, TEXT("Last Stand expired. Unit=%s"),
			*GetNameSafe(this)
		);
	}
}

void AHexUnitActor::ApplyMarkedForDeath(float DamageIncreasePercent, int32 DurationTurns)
{
	if (bIsDead)
	{
		return;
	}

	const float SafeIncreasePercent = FMath::Clamp(DamageIncreasePercent, 0.0f, 5.0f);
	const int32 SafeDurationTurns = FMath::Max(1, DurationTurns);

	bMarkedForDeathActive = true;
	RemainingMarkedForDeathTurns = SafeDurationTurns;
	ActiveMarkedForDeathDamageIncreasePercent = SafeIncreasePercent;
	bMarkedForDeathCountdownStarted = false;

	UE_LOG(LogTemp, Log, TEXT("Marked for Death applied. Target=%s DamageIncrease=%.1f%% Duration=%d"),
		*GetNameSafe(this),
		ActiveMarkedForDeathDamageIncreasePercent * 100.0f,
		RemainingMarkedForDeathTurns
	);
}

bool AHexUnitActor::IsMarkedForDeathActive() const
{
	return !bIsDead && bMarkedForDeathActive && RemainingMarkedForDeathTurns > 0;
}

int32 AHexUnitActor::GetModifiedIncomingDamage(int32 RawDamage) const
{
	const int32 SafeRawDamage = FMath::Max(0, RawDamage);
	if (SafeRawDamage <= 0 || !IsMarkedForDeathActive())
	{
		return SafeRawDamage;
	}

	const float Multiplier = 1.0f + FMath::Clamp(ActiveMarkedForDeathDamageIncreasePercent, 0.0f, 5.0f);
	return FMath::Max(1, FMath::RoundToInt(static_cast<float>(SafeRawDamage) * Multiplier));
}

void AHexUnitActor::ReduceMarkedForDeathDurationAtOwnerTurnStart()
{
	if (!IsMarkedForDeathActive())
	{
		bMarkedForDeathActive = false;
		RemainingMarkedForDeathTurns = 0;
		ActiveMarkedForDeathDamageIncreasePercent = 0.0f;
		bMarkedForDeathCountdownStarted = false;
		return;
	}

	// The first target-owner turn starts the timer without consuming a full turn.
	if (!bMarkedForDeathCountdownStarted)
	{
		bMarkedForDeathCountdownStarted = true;
		return;
	}

	RemainingMarkedForDeathTurns = FMath::Max(0, RemainingMarkedForDeathTurns - 1);
	if (RemainingMarkedForDeathTurns <= 0)
	{
		bMarkedForDeathActive = false;
		ActiveMarkedForDeathDamageIncreasePercent = 0.0f;
		bMarkedForDeathCountdownStarted = false;

		UE_LOG(LogTemp, Log, TEXT("Marked for Death expired. Target=%s"), *GetNameSafe(this));
	}
}

int32 AHexUnitActor::GetChampionAbilityActionPointCost() const
{
	return FMath::Max(0, ChampionAbilityActionPointCost);
}

bool AHexUnitActor::CanHeal() const
{
	return UnitType == EHexUnitType::Healer && bCanHeal && HealAmount > 0;
}

bool AHexUnitActor::CanHealTarget(const AHexUnitActor* TargetUnit) const
{
	if (!CanHeal() || !IsValid(TargetUnit))
	{
		return false;
	}

	if (TargetUnit->GetIsDead())
	{
		return false;
	}

	if (!IsAllyFor(TargetUnit))
	{
		return false;
	}

	return TargetUnit->CurrentHealth < TargetUnit->MaxHealth;
}

int32 AHexUnitActor::GetHealActionPointCost() const
{
	return FMath::Max(0, HealActionPointCost);
}

void AHexUnitActor::ApplyInitialTeamRotation()
{
	if (bInitialTeamRotationApplied)
	{
		return;
	}

	bInitialTeamRotationApplied = true;

	if (Team != EHexUnitTeam::Enemy)
	{
		return;
	}

	if (FMath::IsNearlyZero(EnemyStartYawOffsetDegrees))
	{
		return;
	}

	AddActorWorldRotation(FRotator(0.0f, EnemyStartYawOffsetDegrees, 0.0f));

	UE_LOG(LogTemp, Log, TEXT("Initial enemy rotation applied. Unit=%s YawOffset=%.1f"),
		*GetNameSafe(this),
		EnemyStartYawOffsetDegrees
	);
}

void AHexUnitActor::InitializeSummonedLifetime()
{
	if (!bIsSummonedUnit)
	{
		RemainingSummonedOwnerTurns = 0;
		bSummonedLifetimeCountdownStarted = false;
		return;
	}

	SummonedUnitLifetimeTurns = FMath::Max(1, SummonedUnitLifetimeTurns);
	RemainingSummonedOwnerTurns = SummonedUnitLifetimeTurns;
	bSummonedLifetimeCountdownStarted = false;
}

bool AHexUnitActor::ConsumeSummonedOwnerTurn()
{
	if (!bIsSummonedUnit || bIsDead)
	{
		return false;
	}

	//        owner-turn                                        .
	//           ,                         ,                1                                .
	if (!bSummonedLifetimeCountdownStarted)
	{
		bSummonedLifetimeCountdownStarted = true;
		return false;
	}

	RemainingSummonedOwnerTurns = FMath::Max(0, RemainingSummonedOwnerTurns - 1);
	return RemainingSummonedOwnerTurns <= 0;
}

bool AHexUnitActor::ShouldSkipKillActionPointBonus() const
{
	return bIsSummonedUnit;
}

void AHexUnitActor::PlayHealVFX(AHexUnitActor* HealTarget)
{
	if (!bUseHealVFX || !HealVFX)
	{
		return;
	}

	if (!GetWorld())
	{
		return;
	}

	AHexUnitActor* VFXOwner = this;

	if (HealVFXTarget == EHexHealVFXTarget::Target)
	{
		VFXOwner = HealTarget;
	}

	if (!IsValid(VFXOwner))
	{
		return;
	}

	const FVector SpawnLocation = VFXOwner->GetActorLocation() + HealVFXLocationOffset;
	const FRotator SpawnRotation = VFXOwner->GetActorRotation();
	const FVector SpawnScale(FMath::Max(0.01f, HealVFXScale));

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		HealVFX,
		SpawnLocation,
		SpawnRotation,
		SpawnScale,
		true,
		true
	);
}

bool AHexUnitActor::IsSummonedUnitClassValid() const
{
	if (!SummonedUnitClass)
	{
		return false;
	}

	const AHexUnitActor* DefaultSummonedUnit = SummonedUnitClass->GetDefaultObject<AHexUnitActor>();
	return IsValid(DefaultSummonedUnit) && DefaultSummonedUnit->bIsSummonedUnit;
}

bool AHexUnitActor::CanSummon() const
{
	const bool bLegacyNonChampionSummoner = UnitType != EHexUnitType::Champion && bCanSummon;
	const bool bChampionSummonAbility = IsChampionAbilitySummon();

	return (bLegacyNonChampionSummoner || bChampionSummonAbility) && IsSummonedUnitClassValid();
}

int32 AHexUnitActor::GetSummonActionPointCost() const
{
	if (IsChampionAbilitySummon())
	{
		return GetChampionAbilityActionPointCost();
	}

	return FMath::Max(0, SummonActionPointCost);
}

int32 AHexUnitActor::GetSummonRange() const
{
	if (IsChampionAbilitySummon())
	{
		return FMath::Max(1, ChampionAbilityRange);
	}

	return FMath::Max(1, SummonRange);
}

int32 AHexUnitActor::GetSummonUnitCount() const
{
	return FMath::Max(1, SummonUnitCount);
}

void AHexUnitActor::PlaySummonVFX(const FVector& SummonLocation)
{
	if (!bUseSummonVFX || !SummonVFX)
	{
		return;
	}

	if (!GetWorld())
	{
		return;
	}

	FVector VFXLocation = SummonLocation;
	FRotator VFXRotation = GetActorRotation();

	if (SummonVFXTarget == EHexSummonVFXTarget::Summoner)
	{
		VFXLocation = GetActorLocation();
	}

	VFXLocation += SummonVFXLocationOffset;

	const FVector SpawnScale(FMath::Max(0.01f, SummonVFXScale));

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		SummonVFX,
		VFXLocation,
		VFXRotation,
		SpawnScale,
		true,
		true
	);
}

void AHexUnitActor::PlayChampionAbilityVFX(AHexUnitActor* TargetUnit, const FVector& TargetLocation)
{
	if (!bUseChampionAbilityVFX || !ChampionAbilityVFX)
	{
		return;
	}

	if (!GetWorld())
	{
		return;
	}

	FVector VFXLocation = GetActorLocation();
	FRotator VFXRotation = GetActorRotation();

	switch (ChampionAbilityVFXTarget)
	{
	case EHexChampionAbilityVFXTarget::TargetUnit:
		if (IsValid(TargetUnit))
		{
			VFXLocation = TargetUnit->GetActorLocation();
			VFXRotation = TargetUnit->GetActorRotation();
		}
		else
		{
			VFXLocation = TargetLocation;
		}
		break;

	case EHexChampionAbilityVFXTarget::TargetLocation:
		VFXLocation = TargetLocation;
		break;

	case EHexChampionAbilityVFXTarget::Champion:
	default:
		VFXLocation = GetActorLocation();
		VFXRotation = GetActorRotation();
		break;
	}

	VFXLocation += ChampionAbilityVFXLocationOffset;

	const FVector SpawnScale(FMath::Max(0.01f, ChampionAbilityVFXScale));

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ChampionAbilityVFX,
		VFXLocation,
		VFXRotation,
		SpawnScale,
		true,
		true
	);
}

bool AHexUnitActor::UsesAttackProjectileVFX() const
{
	return bUseAttackProjectileVFX && (AttackProjectileMesh != nullptr || AttackProjectileVFX != nullptr);
}

FVector AHexUnitActor::GetAttackProjectileStartLocation() const
{
	FVector StartLocation = GetActorLocation();

	if (UnitMesh
		&& !AttackProjectileSocketName.IsNone()
		&& (UnitMesh->DoesSocketExist(AttackProjectileSocketName) || UnitMesh->GetBoneIndex(AttackProjectileSocketName) != INDEX_NONE))
	{
		StartLocation = UnitMesh->GetSocketLocation(AttackProjectileSocketName);
	}

	if (!AttackProjectileSpawnOffset.IsNearlyZero())
	{
		StartLocation += GetActorTransform().TransformVectorNoScale(AttackProjectileSpawnOffset);
	}

	return StartLocation;
}

void AHexUnitActor::SetAttackProjectileActionLocked(bool bLocked)
{
	bIsAttackProjectileActionLocked = bLocked;
}

bool AHexUnitActor::PlayAttackAnimation()
{
	if (!CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Attack animation blocked: unit cannot act now. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (!AttackMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack animation blocked: AttackMontage is not set. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (!UnitMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack animation blocked: UnitMesh is null. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	UAnimInstance* AnimInstance = UnitMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack animation blocked: AnimInstance is null. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (AnimInstance->Montage_IsPlaying(AttackMontage))
	{
		UE_LOG(LogTemp, Log, TEXT("Attack animation blocked: AttackMontage is already playing. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	bIsActionLocked = true;

	const float MontageDuration = AnimInstance->Montage_Play(AttackMontage, 1.0f);
	if (MontageDuration <= 0.0f)
	{
		bIsActionLocked = false;

		UE_LOG(LogTemp, Warning, TEXT("Attack animation failed to start. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AHexUnitActor::OnAttackMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, AttackMontage);

	return true;
}

bool AHexUnitActor::PlayChampionAbilityAnimation()
{
	if (!CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Champion ability animation blocked: unit cannot act now. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (!CanUseChampionAbility())
	{
		UE_LOG(LogTemp, Warning, TEXT("Champion ability animation blocked: unit is not a champion ability user. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	//                                :                                          VFX.
	if (!ChampionAbilityMontage)
	{
		return true;
	}

	if (!UnitMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Champion ability animation blocked: UnitMesh is null. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	UAnimInstance* AnimInstance = UnitMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Champion ability animation blocked: AnimInstance is null. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (AnimInstance->Montage_IsPlaying(ChampionAbilityMontage))
	{
		UE_LOG(LogTemp, Log, TEXT("Champion ability animation blocked: ChampionAbilityMontage is already playing. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	bIsActionLocked = true;

	const float MontageDuration = AnimInstance->Montage_Play(ChampionAbilityMontage, 1.0f);
	if (MontageDuration <= 0.0f)
	{
		bIsActionLocked = false;

		UE_LOG(LogTemp, Warning, TEXT("Champion ability animation failed to start. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AHexUnitActor::OnChampionAbilityMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, ChampionAbilityMontage);

	return true;
}

bool AHexUnitActor::PlayHealAnimation()
{
	if (!CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Heal animation blocked: unit cannot act now. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (!CanHeal())
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal animation blocked: unit is not a healer. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (!HealMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal animation blocked: HealMontage is not set. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (!UnitMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal animation blocked: UnitMesh is null. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	UAnimInstance* AnimInstance = UnitMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal animation blocked: AnimInstance is null. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	if (AnimInstance->Montage_IsPlaying(HealMontage))
	{
		UE_LOG(LogTemp, Log, TEXT("Heal animation blocked: HealMontage is already playing. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	bIsActionLocked = true;

	const float MontageDuration = AnimInstance->Montage_Play(HealMontage, 1.0f);
	if (MontageDuration <= 0.0f)
	{
		bIsActionLocked = false;

		UE_LOG(LogTemp, Warning, TEXT("Heal animation failed to start. Unit=%s"), *GetNameSafe(this));
		return false;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AHexUnitActor::OnHealMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, HealMontage);

	return true;
}

bool AHexUnitActor::CanAct() const
{
	return !bIsDead
		&& !bIsMoving
		&& !bIsActionLocked
		&& !bIsHitReactionLocked
		&& !bIsAttackProjectileActionLocked;
}

bool AHexUnitActor::GetIsActionLocked() const
{
	return bIsActionLocked;
}

bool AHexUnitActor::GetIsHitReactionLocked() const
{
	return bIsHitReactionLocked;
}

void AHexUnitActor::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != AttackMontage)
	{
		return;
	}

	bIsActionLocked = false;

	UE_LOG(LogTemp, Log, TEXT("Attack animation ended. Unit=%s Interrupted=%s"),
		*GetNameSafe(this),
		bInterrupted ? TEXT("true") : TEXT("false")
	);
}

void AHexUnitActor::OnChampionAbilityMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ChampionAbilityMontage)
	{
		return;
	}

	bIsActionLocked = false;

	UE_LOG(LogTemp, Log, TEXT("Champion ability animation ended. Unit=%s Interrupted=%s"),
		*GetNameSafe(this),
		bInterrupted ? TEXT("true") : TEXT("false")
	);
}

void AHexUnitActor::OnHealMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != HealMontage)
	{
		return;
	}

	bIsActionLocked = false;

	UE_LOG(LogTemp, Log, TEXT("Heal animation ended. Unit=%s Interrupted=%s"),
		*GetNameSafe(this),
		bInterrupted ? TEXT("true") : TEXT("false")
	);
}

void AHexUnitActor::TakeUnitDamage(int32 DamageAmount)
{
	if (bIsDead)
	{
		return;
	}

	if (DamageAmount <= 0)
	{
		return;
	}

	const int32 AppliedDamage = GetModifiedIncomingDamage(DamageAmount);
	if (AppliedDamage <= 0)
	{
		return;
	}

	const int32 OldHealth = CurrentHealth;
	const int32 NewHealth = FMath::Clamp(CurrentHealth - AppliedDamage, 0, MaxHealth);

	if (NewHealth == OldHealth)
	{
		return;
	}

	if (NewHealth <= 0)
	{
		if (TrySurviveLethalDamageWithLastStand())
		{
			return;
		}

		CurrentHealth = 0;
		Die();
		return;
	}

	CurrentHealth = NewHealth;

	UpdateHealthBarWidget();
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	PlayHitReaction();
}

void AHexUnitActor::HealUnit(int32 InHealAmount)
{
	if (bIsDead)
	{
		return;
	}

	if (InHealAmount <= 0)
	{
		return;
	}

	const int32 OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth + InHealAmount, 0, MaxHealth);

	if (CurrentHealth != OldHealth)
	{
		UpdateHealthBarWidget();
		OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	}
}

void AHexUnitActor::Die()
{
	if (bIsDead)
	{
		return;
	}

	CurrentHealth = 0;
	bIsDead = true;
	bIsActionLocked = false;
	bIsHitReactionLocked = false;
	bMarkedForDeathActive = false;
	RemainingMarkedForDeathTurns = 0;
	ActiveMarkedForDeathDamageIncreasePercent = 0.0f;
	bMarkedForDeathCountdownStarted = false;

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(HitReactionLockTimerHandle);
	}

	StopMovement();

	//                                HitReact montage,                      death-state   Anim Graph.
	//                                                             .
	if (UnitMesh)
	{
		if (UAnimInstance* AnimInstance = UnitMesh->GetAnimInstance())
		{
			AnimInstance->StopAllMontages(0.05f);
		}
	}

	UpdateHealthBarWidget();
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	OnUnitDied.Broadcast(this);

	if (bDestroyAfterDeath && !bIsSummonedUnit)
	{
		SetLifeSpan(DestroyAfterDeathDelay);
	}
}

bool AHexUnitActor::GetIsDead() const
{
	return bIsDead;
}

float AHexUnitActor::GetHealthPercent() const
{
	if (MaxHealth <= 0)
	{
		return 0.0f;
	}

	return static_cast<float>(CurrentHealth) / static_cast<float>(MaxHealth);
}

void AHexUnitActor::UpdateHealthBarWidget()
{
	if (!HealthBarWidget)
	{
		return;
	}

	HealthBarWidget->SetRelativeLocation(FVector(0.0f, 0.0f, HealthBarHeight));
	HealthBarWidget->SetDrawSize(HealthBarDrawSize);

	const bool bShouldHide =
		(bHideHealthBarWhenDead && bIsDead) ||
		(bHideHealthBarWhenFull && !bIsDead && CurrentHealth >= MaxHealth);

	HealthBarWidget->SetVisibility(!bShouldHide, true);

	// Ensure the UUserWidget exists before trying to recolor it.
	// Without this, some units can keep the default WBP color on their first frame.
	HealthBarWidget->InitWidget();

	UUserWidget* UserWidget = HealthBarWidget->GetUserWidgetObject();
	if (!UserWidget)
	{
		return;
	}

	UProgressBar* ProgressBar = Cast<UProgressBar>(UserWidget->GetWidgetFromName(HealthProgressBarName));
	if (!ProgressBar)
	{
		return;
	}

	// Different character WBPs may have different Fill Image tints.
	// Reset the brush tint to white so the runtime team color is not multiplied
	// by an old blue/red tint stored in an individual WBP.
	FProgressBarStyle ProgressBarStyle = ProgressBar->GetWidgetStyle();
	ProgressBarStyle.FillImage.TintColor = FSlateColor(FLinearColor::White);
	ProgressBar->SetWidgetStyle(ProgressBarStyle);

	// Health amount is represented only by the bar length.
	ProgressBar->SetPercent(FMath::Clamp(GetHealthPercent(), 0.0f, 1.0f));

	// The fill color depends only on the runtime team.
	const FLinearColor HealthBarColor =
		Team == EHexUnitTeam::Enemy
			? EnemyHealthBarColor
			: PlayerHealthBarColor;

	ProgressBar->SetFillColorAndOpacity(HealthBarColor);

	// Display level in brackets above the health bar: [1], [2], ...
	if (UTextBlock* LevelTextBlock = Cast<UTextBlock>(UserWidget->GetWidgetFromName(HealthLevelTextBlockName)))
	{
		LevelTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("[%d]"),
			GetClampedUnitLevel()
		)));
	}

	// Display exact current/max HP to the right of the level: 75/100.
	if (UTextBlock* HealthValueTextBlock = Cast<UTextBlock>(UserWidget->GetWidgetFromName(HealthValueTextBlockName)))
	{
		HealthValueTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("%d/%d"),
			FMath::Max(0, CurrentHealth),
			FMath::Max(1, MaxHealth)
		)));
	}
}

void AHexUnitActor::PlayHitReaction()
{
	//                    /                                hit-reaction         .
	//                                           ,                                            .
	if (bIsActionLocked)
	{
		return;
	}

	if (!HitReactMontage)
	{
		return;
	}

	if (!UnitMesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = UnitMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	if (AnimInstance->Montage_IsPlaying(HitReactMontage))
	{
		AnimInstance->Montage_Stop(0.05f, HitReactMontage);
	}

	bIsHitReactionLocked = true;

	const float MontageDuration = AnimInstance->Montage_Play(HitReactMontage);
	if (MontageDuration <= 0.0f)
	{
		ClearHitReactionLock();
		return;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AHexUnitActor::OnHitReactMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, HitReactMontage);

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(HitReactionLockTimerHandle);
		GetWorldTimerManager().SetTimer(
			HitReactionLockTimerHandle,
			this,
			&AHexUnitActor::ClearHitReactionLock,
			MontageDuration + FMath::Max(0.0f, HitReactionLockSafetyExtraTime),
			false
		);
	}
}

void AHexUnitActor::ClearHitReactionLock()
{
	bIsHitReactionLocked = false;

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(HitReactionLockTimerHandle);
	}
}

void AHexUnitActor::OnHitReactMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != HitReactMontage)
	{
		return;
	}

	ClearHitReactionLock();

	UE_LOG(LogTemp, Log, TEXT("Hit reaction ended. Unit=%s Interrupted=%s"),
		*GetNameSafe(this),
		bInterrupted ? TEXT("true") : TEXT("false")
	);
}

void AHexUnitActor::AdvanceMovement(float DeltaTime)
{
	if (bIsDead)
	{
		StopMovement();
		return;
	}

	if (!bIsMoving || PathPoints.Num() == 0)
	{
		StopMovement();
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const FVector TargetLocation = PathPoints[0];

	const FVector ToTarget = TargetLocation - CurrentLocation;
	const float Distance = ToTarget.Size();

	if (Distance <= KINDA_SMALL_NUMBER)
	{
		SetActorLocation(TargetLocation);
		PathPoints.RemoveAt(0);

		if (PathPoints.Num() == 0)
		{
			StopMovement();
			OnMovementFinished.Broadcast();
		}

		return;
	}

	const float Step = MoveSpeed * DeltaTime;

	if (Distance <= Step)
	{
		SetActorLocation(TargetLocation);
		PathPoints.RemoveAt(0);

		if (PathPoints.Num() == 0)
		{
			StopMovement();
			OnMovementFinished.Broadcast();
		}

		return;
	}

	const FVector Direction = ToTarget / Distance;
	const FVector NewLocation = CurrentLocation + Direction * Step;

	SetActorLocation(NewLocation);

	FRotator DesiredRotation = Direction.Rotation();
	DesiredRotation.Pitch = 0.0f;
	DesiredRotation.Roll = 0.0f;

	const FRotator NewRotation = FMath::RInterpTo(
		GetActorRotation(),
		DesiredRotation,
		DeltaTime,
		RotationInterpSpeed
	);

	SetActorRotation(NewRotation);

	bIsMoving = true;
	VisualSpeed = MoveSpeed;
}

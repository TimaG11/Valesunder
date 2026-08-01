// SUPPORT HEALER NEW v3 - generated 2026-06-20, contains bCanHeal/HealAmount/HealMontage and InHealAmount fix
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "HexUnitActor.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UWidgetComponent;
class UAnimMontage;
class UTexture2D;
class UStaticMesh;
class UNiagaraSystem;
class AHexUnitActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FHexUnitMovementFinishedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHexUnitDiedSignature, AHexUnitActor*, DeadUnit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHexUnitHealthChangedSignature, int32, CurrentHealth, int32, MaxHealth);

UENUM(BlueprintType)
enum class EHexUnitTeam : uint8
{
	Player UMETA(DisplayName = "Player"),
	Enemy  UMETA(DisplayName = "Enemy")
};

UENUM(BlueprintType)
enum class EHexUnitType : uint8
{
	Champion  UMETA(DisplayName = "Champion"),
	Ram       UMETA(DisplayName = "Ram"),
	Skirmisher UMETA(DisplayName = "Skirmisher"),
	Support   UMETA(DisplayName = "Support"),
	Healer    UMETA(DisplayName = "Healer")
};

UENUM(BlueprintType)
enum class EHexUnitFaction : uint8
{
	Kingdom UMETA(DisplayName = "kingdom"),
	Soul    UMETA(DisplayName = "soul"),
	Animal  UMETA(DisplayName = "animal"),
	Bandits UMETA(DisplayName = "Bandits")
};

UENUM(BlueprintType)
enum class EHexHealVFXTarget : uint8
{
	Healer UMETA(DisplayName = "Healer"),
	Target UMETA(DisplayName = "Heal Target")
};

UENUM(BlueprintType)
enum class EHexSummonVFXTarget : uint8
{
	Summoner UMETA(DisplayName = "Summoner"),
	SpawnLocation UMETA(DisplayName = "Spawn Location")
};

UENUM(BlueprintType)
enum class EHexChampionAbilityVFXTarget : uint8
{
	Champion       UMETA(DisplayName = "Champion"),
	TargetUnit     UMETA(DisplayName = "Target Unit"),
	TargetLocation UMETA(DisplayName = "Target Location")
};

UENUM(BlueprintType)
enum class EHexChampionAbilityType : uint8
{
	Custom          UMETA(DisplayName = "Custom Ability"),
	Summon          UMETA(DisplayName = "Summon"),
	LastStand       UMETA(DisplayName = "Last Stand"),
	MarkedForDeath  UMETA(DisplayName = "Marked for Death")
};

UENUM(BlueprintType)
enum class EHexSummonPlacementMode : uint8
{
	SelectedCell        UMETA(DisplayName = "Selected Cell"),
	RandomFreeCells    UMETA(DisplayName = "Random Free Cells"),
	FixedRelativeCells UMETA(DisplayName = "Fixed Relative Cells")
};

USTRUCT(BlueprintType)
struct FHexSummonRelativeCell
{
	GENERATED_BODY()

	//   Q   .  Q=1, R=0 =     axial-.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon")
	int32 Q = 0;

	//   R   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon")
	int32 R = 0;

	FHexSummonRelativeCell()
	{
	}

	FHexSummonRelativeCell(int32 InQ, int32 InR)
		: Q(InQ), R(InR)
	{
	}
};

UCLASS()
class OTHERBIOS_API AHexUnitActor : public AActor
{
	GENERATED_BODY()

public:
	AHexUnitActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit")
	USceneComponent* SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit")
	USkeletalMeshComponent* UnitMesh = nullptr;

	//      .
	//  Blueprint-   HealthBarWidget   Widget Class.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Health Bar")
	UWidgetComponent* HealthBarWidget = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Movement", meta = (ClampMin = "1.0"))
	float MoveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Movement", meta = (ClampMin = "0.1"))
	float RotationInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Movement", meta = (ClampMin = "0"))
	int32 MovementRange = 3;

	//  . Player  , Enemy    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat")
	EHexUnitTeam Team = EHexUnitTeam::Player;

	//       .
	//      / .         .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Start Rotation", meta = (EditCondition = "Team == EHexUnitTeam::Enemy", EditConditionHides, ClampMin = "-360.0", ClampMax = "360.0"))
	float EnemyStartYawOffsetDegrees = 180.0f;

	//   .   UI, ,     .
	// Champion =   .
	// Ram =   / ,      .
	// Skirmisher =  /       .
	// Support = : , ,    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Type")
	EHexUnitType UnitType = EHexUnitType::Skirmisher;

	// Unit faction. Used by enemy bot difficulty profiles and future faction bonuses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Faction")
	EHexUnitFaction Faction = EHexUnitFaction::Kingdom;

	// ,        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|UI")
	FString UnitDisplayName = TEXT("Unit");

	// /     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|UI")
	UTexture2D* UnitPortrait = nullptr;

	// Runtime/saved level. Do not balance units through BP level values anymore.
	// The shared progression rules are: max level 15, 200 EXP from level 1 to 2, +100 EXP for every next level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Progression", meta = (ClampMin = "1", ClampMax = "15"))
	int32 UnitLevel = 1;

	// Banked experience for the current level. It may exceed the next-level threshold
	// until the player manually buys an upgrade with coins.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Progression", meta = (ClampMin = "0"))
	int32 CurrentExperience = 0;

	// Kept only for old Blueprints that already have these fields serialized. The code uses the fixed shared rules above.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Progression")
	int32 ExperienceToNextLevelAtLevel1 = 200;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Progression")
	int32 ExperienceToNextLevelIncreasePerLevel = 100;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Progression")
	int32 MaxUnitLevel = 15;

	// If true, army builder uses ManualArmyPower instead of the automatic formula.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Army Builder")
	bool bUseManualArmyPower = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Army Builder", meta = (ClampMin = "0", EditCondition = "bUseManualArmyPower", EditConditionHides))
	int32 ManualArmyPower = 0;

	//      .
	// 0    ,         .
	//         UI/;     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Stats", meta = (ClampMin = "0"))
	int32 OccupiedSlots = 1;

	//    . 1 =  .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat", meta = (ClampMin = "1"))
	int32 AttackRange = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat", meta = (ClampMin = "1"))
	int32 AttackDamage = 25;

	//    Ability    Champion.
	//  ChampionAbilityType = Summon,    Ability    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (EditCondition = "UnitType == EHexUnitType::Champion", EditConditionHides))
	bool bCanUseChampionAbility = true;

	//   .
	// Custom =    .
	// Summon = Ability         SummonedUnitClass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	EHexChampionAbilityType ChampionAbilityType = EHexChampionAbilityType::Custom;

	// ,      Ability,   WBP  TextBlock AbilityButtonText.
	//  -   "SUMMON"   "ABILITY".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	FText ChampionAbilityDisplayName = FText::FromString(TEXT("ABILITY"));

	// Ability description shown in the selected-unit panel when the ability button is held/hovered.
	// Fill this in every champion BP. Supports multi-line text.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|UI", meta = (MultiLine = "true", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	FText ChampionAbilityDescription = FText::GetEmpty();

	//   Ability.
	//  ChampionAbilityType = Summon   .  SummonActionPointCost   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (ClampMin = "0", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	int32 ChampionAbilityActionPointCost = 1;

	// Cooldown in owner turns after a champion ability is used.
	// 0 = no cooldown. 1 = available again at the start of this champion's next owner turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (ClampMin = "0", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	int32 ChampionAbilityCooldownTurns = 1;

	// Runtime cooldown counter. While this is > 0, ability button is disabled.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Champion Ability")
	int32 RemainingChampionAbilityCooldownTurns = 0;

	//   Ability.
	//  ChampionAbilityType = Summon     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (ClampMin = "0", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	int32 ChampionAbilityRange = 1;

	//  : true,          .
	//  ChampionAbilityType = Summon   ,      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType == EHexChampionAbilityType::Custom", EditConditionHides))
	bool bChampionAbilityNeedsTarget = false;

	// Marked for Death: enemy target receives extra incoming damage for several of its owner turns.
	// Healing is intentionally not modified by this effect.
	// Editable ability tuning. Kept visible for all unit Blueprints because Unreal can
	// incorrectly hide enum-dependent UPROPERTY fields after Live Coding / BP reload.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Marked for Death", meta = (DisplayName = "Damage Increase Percent", ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "1.0"))
	float MarkedForDeathDamageIncreasePercent = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Marked for Death", meta = (DisplayName = "Duration Turns", ClampMin = "1"))
	int32 MarkedForDeathDurationTurns = 2;

	// Last Stand:         ,
	//   ,      HP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Last Stand", meta = (ClampMin = "1", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType == EHexChampionAbilityType::LastStand", EditConditionHides))
	int32 LastStandDurationTurns = 1;

	//  HP    .     1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Last Stand", meta = (ClampMin = "1", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType == EHexChampionAbilityType::LastStand", EditConditionHides))
	int32 LastStandSurviveHealth = 1;

	//  true,       .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Last Stand", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType == EHexChampionAbilityType::LastStand", EditConditionHides))
	bool bLastStandConsumedOnLethalDamage = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Champion Ability|Last Stand")
	bool bLastStandActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Champion Ability|Last Stand")
	int32 RemainingLastStandTurns = 0;

	// Runtime debuff state stored on the marked target.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Status|Marked for Death")
	bool bMarkedForDeathActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Status|Marked for Death")
	int32 RemainingMarkedForDeathTurns = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Status|Marked for Death")
	float ActiveMarkedForDeathDamageIncreasePercent = 0.0f;

	//       Support.
	//  BP       UnitType = Support.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing", meta = (EditCondition = "UnitType == EHexUnitType::Healer", EditConditionHides))
	bool bCanHeal = false;

	//  HP    .
	//    UnitType = Support  bCanHeal = true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing", meta = (ClampMin = "1", EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal", EditConditionHides))
	int32 HealAmount = 25;

	//        .
	//    BP ,    ,        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing", meta = (ClampMin = "0", EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal", EditConditionHides))
	int32 HealActionPointCost = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Movement")
	bool bIsMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Movement")
	float VisualSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Grid")
	int32 CurrentQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Grid")
	int32 CurrentR = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health", meta = (ClampMin = "1"))
	int32 MaxHealth = 100;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Health")
	int32 CurrentHealth = 100;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Health")
	bool bIsDead = false;

	// true,    ,     .
	//      Anim Montage.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|State")
	bool bIsActionLocked = false;

	// true,     .
	//  ,          hit-reaction.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|State")
	bool bIsHitReactionLocked = false;

	//  :  Montage End Delegate -  ,    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|State", meta = (ClampMin = "0.0"))
	float HitReactionLockSafetyExtraTime = 0.05f;

	//  ProgressBar  Widget Blueprint.
	//  UMG  ProgressBar     HealthProgressBar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar")
	FName HealthProgressBarName = TEXT("HealthProgressBar");

	//     .  /    BP .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar", meta = (ClampMin = "0.0"))
	float HealthBarHeight = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar")
	FVector2D HealthBarDrawSize = FVector2D(150.0f, 42.0f);

	// TextBlock inside the health-bar widget that displays the unit level as [N].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar|Text")
	FName HealthLevelTextBlockName = TEXT("HealthLevelTextBlock");

	// TextBlock inside the health-bar widget that displays current/max HP, for example 75/100.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar|Text")
	FName HealthValueTextBlockName = TEXT("HealthValueTextBlock");

	// Constant health-bar color for player-controlled units. Approx. HEX: #2FE36F.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar|Color")
	FLinearColor PlayerHealthBarColor = FLinearColor(0.18f, 0.89f, 0.44f, 1.0f);

	// Constant health-bar color for enemy units. Approx. HEX: #FF4B55.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar|Color")
	FLinearColor EnemyHealthBarColor = FLinearColor(1.0f, 0.29f, 0.33f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar")
	bool bHideHealthBarWhenFull = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health Bar")
	bool bHideHealthBarWhenDead = true;

	//     .
	//  Blueprint     Anim Montage,   Get_hit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Animation")
	UAnimMontage* HitReactMontage = nullptr;

	//  .  BP    Anim Montage .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Animation")
	UAnimMontage* AttackMontage = nullptr;

	// Enables a real travelling projectile for this unit's normal attack.
	// The projectile may use a Static Mesh, Niagara, or both. Melee units should keep this false.
	// The old variable name is preserved so existing Blueprint defaults do not reset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (DisplayName = "Use Attack Projectile"))
	bool bUseAttackProjectileVFX = false;

	// Ordinary Static Mesh used for arrows, bolts, stones, etc.
	// Assign this field when no Niagara effect is needed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	UStaticMesh* AttackProjectileMesh = nullptr;

	// Optional Niagara system shown while the projectile flies.
	// It can be used together with AttackProjectileMesh or left empty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	UNiagaraSystem* AttackProjectileVFX = nullptr;

	// Optional Niagara system spawned when the projectile reaches the target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	UNiagaraSystem* AttackImpactVFX = nullptr;

	// Socket or bone on UnitMesh where the projectile starts.
	// Recommended socket name: ProjectileSocket.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	FName AttackProjectileSocketName = TEXT("ProjectileSocket");

	// Socket or bone on the target UnitMesh where the projectile aims.
	// If it does not exist, TargetOffset is added to the target actor location.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	FName AttackProjectileTargetSocketName = TEXT("spine_02");

	// Local-space offset added after the launch socket position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	FVector AttackProjectileSpawnOffset = FVector::ZeroVector;

	// World-space offset added to the target socket or actor location.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	FVector AttackProjectileTargetOffset = FVector::ZeroVector;

	// Delay after the attack montage starts. Use it to match the exact release frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (ClampMin = "0.0", EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	float AttackProjectileLaunchDelay = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (ClampMin = "1.0", EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	float AttackProjectileSpeed = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (ClampMin = "1.0", EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	float AttackProjectileArrivalRadius = 25.0f;

	// Safety timeout. On timeout the attack is still resolved at the last known target point,
	// so combat can never get stuck because of a broken VFX.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (ClampMin = "0.1", EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	float AttackProjectileMaxLifetime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (ClampMin = "0.01", EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	float AttackProjectileScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (ClampMin = "0.01", EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	float AttackImpactVFXScale = 1.0f;

	// Use this when the Niagara asset's forward direction is not +X.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Combat|Projectile", meta = (EditCondition = "bUseAttackProjectileVFX", EditConditionHides))
	FRotator AttackProjectileRotationOffset = FRotator::ZeroRotator;

	//   .    Champion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Animation", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility", EditConditionHides))
	UAnimMontage* ChampionAbilityMontage = nullptr;

	//  Niagara VFX   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType != EHexChampionAbilityType::Summon", EditConditionHides))
	bool bUseChampionAbilityVFX = false;

	//   VFX :  ,  -    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType != EHexChampionAbilityType::Summon && bUseChampionAbilityVFX", EditConditionHides))
	EHexChampionAbilityVFXTarget ChampionAbilityVFXTarget = EHexChampionAbilityVFXTarget::Champion;

	// Niagara System    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType != EHexChampionAbilityType::Summon && bUseChampionAbilityVFX", EditConditionHides))
	UNiagaraSystem* ChampionAbilityVFX = nullptr;

	//  VFX    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType != EHexChampionAbilityType::Summon && bUseChampionAbilityVFX", EditConditionHides))
	FVector ChampionAbilityVFXLocationOffset = FVector::ZeroVector;

	//  VFX  .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|VFX", meta = (ClampMin = "0.01", EditCondition = "UnitType == EHexUnitType::Champion && bCanUseChampionAbility && ChampionAbilityType != EHexChampionAbilityType::Summon && bUseChampionAbilityVFX", EditConditionHides))
	float ChampionAbilityVFXScale = 1.0f;

	//  .  BP    Anim Montage .
	//      Support +  bCanHeal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing", meta = (EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal", EditConditionHides))
	UAnimMontage* HealMontage = nullptr;

	//  Niagara VFX   .
	//      Support  bCanHeal = true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal", EditConditionHides))
	bool bUseHealVFX = false;

	//   VFX :       .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal && bUseHealVFX", EditConditionHides))
	EHexHealVFXTarget HealVFXTarget = EHexHealVFXTarget::Target;

	// Niagara System   .   None.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal && bUseHealVFX", EditConditionHides))
	UNiagaraSystem* HealVFX = nullptr;

	//   VFX  ,    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing|VFX", meta = (EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal && bUseHealVFX", EditConditionHides))
	FVector HealVFXLocationOffset = FVector::ZeroVector;

	//  VFX .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Healer|Healing|VFX", meta = (ClampMin = "0.01", EditCondition = "UnitType == EHexUnitType::Healer && bCanHeal && bUseHealVFX", EditConditionHides))
	float HealVFXScale = 1.0f;

	//     -.
	// -     :  UnitType = Champion, bCanUseChampionAbility = true, ChampionAbilityType = Summon.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon|Legacy Non-Champion Summoner", meta = (EditCondition = "UnitType != EHexUnitType::Champion", EditConditionHides))
	bool bCanSummon = false;

	//      -.
	//  -  ChampionAbilityActionPointCost.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon|Legacy Non-Champion Summoner", meta = (ClampMin = "0", EditCondition = "UnitType != EHexUnitType::Champion && bCanSummon", EditConditionHides))
	int32 SummonActionPointCost = 1;

	//      -.
	//  -  ChampionAbilityRange.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon|Legacy Non-Champion Summoner", meta = (ClampMin = "1", EditCondition = "UnitType != EHexUnitType::Champion && bCanSummon", EditConditionHides))
	int32 SummonRange = 1;

	//  ,   Champion Ability   Summon.
	// :  BP     bIsSummonedUnit = true,      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon", meta = (EditCondition = "ChampionAbilityType == EHexChampionAbilityType::Summon", EditConditionHides))
	TSubclassOf<AHexUnitActor> SummonedUnitClass;

	//       .  Ability      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon", meta = (ClampMin = "1", EditCondition = "ChampionAbilityType == EHexChampionAbilityType::Summon", EditConditionHides))
	int32 SummonUnitCount = 1;

	//    .
	// Selected Cell =   ;  SummonUnitCount > 1,        .
	// Random Free Cells =        .
	// Fixed Relative Cells =    SummonFixedRelativeCells  .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon", meta = (EditCondition = "ChampionAbilityType == EHexChampionAbilityType::Summon", EditConditionHides))
	EHexSummonPlacementMode SummonPlacementMode = EHexSummonPlacementMode::SelectedCell;

	//    Fixed Relative Cells.     ,    .
	// : (1,0), (0,1), (-1,1), (-1,0), (0,-1), (1,-1)     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon", meta = (EditCondition = "SummonPlacementMode == EHexSummonPlacementMode::FixedRelativeCells", EditConditionHides))
	TArray<FHexSummonRelativeCell> SummonFixedRelativeCells;

	//  Niagara VFX   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon|VFX", meta = (EditCondition = "ChampionAbilityType == EHexChampionAbilityType::Summon", EditConditionHides))
	bool bUseSummonVFX = false;

	//   VFX :        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon|VFX", meta = (EditCondition = "bUseSummonVFX", EditConditionHides))
	EHexSummonVFXTarget SummonVFXTarget = EHexSummonVFXTarget::SpawnLocation;

	// Niagara System   .   None.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon|VFX", meta = (EditCondition = "bUseSummonVFX", EditConditionHides))
	UNiagaraSystem* SummonVFX = nullptr;

	//  VFX     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon|VFX", meta = (EditCondition = "bUseSummonVFX", EditConditionHides))
	FVector SummonVFXLocationOffset = FVector::ZeroVector;

	//  VFX .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Champion Ability|Summon|VFX", meta = (ClampMin = "0.01", EditCondition = "bUseSummonVFX", EditConditionHides))
	float SummonVFXScale = 1.0f;

	//        .
	// , Enemy-   Enemy-turn, Player-turn   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon")
	bool bIsSummonedUnit = false;

	//   -   .
	//  Enemy    Enemy,  Player    Player.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon", meta = (ClampMin = "1", EditCondition = "bIsSummonedUnit", EditConditionHides))
	int32 SummonedUnitLifetimeTurns = 2;

	//   .    .
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Unit|Summon", meta = (EditCondition = "bIsSummonedUnit", EditConditionHides))
	int32 RemainingSummonedOwnerTurns = 0;

	//             .
	//    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Summon", meta = (ClampMin = "0.0", EditCondition = "bIsSummonedUnit", EditConditionHides))
	float SummonedDespawnDelay = 1.0f;

	//  true,      DestroyAfterDeathDelay .
	//  false,        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health")
	bool bDestroyAfterDeath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Unit|Health", meta = (ClampMin = "0.0", EditCondition = "bDestroyAfterDeath"))
	float DestroyAfterDeathDelay = 5.0f;

	UPROPERTY(BlueprintAssignable, Category = "Hex Unit|Movement")
	FHexUnitMovementFinishedSignature OnMovementFinished;

	UPROPERTY(BlueprintAssignable, Category = "Hex Unit|Health")
	FHexUnitDiedSignature OnUnitDied;

	UPROPERTY(BlueprintAssignable, Category = "Hex Unit|Health")
	FHexUnitHealthChangedSignature OnHealthChanged;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Grid")
	void SetGridCoord(int32 Q, int32 R);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Grid")
	FIntPoint GetGridCoord() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Movement")
	void MoveAlongWorldPath(const TArray<FVector>& WorldPath);

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Movement")
	void StopMovement();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Combat")
	bool IsEnemyFor(const AHexUnitActor* OtherUnit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Combat")
	bool IsAllyFor(const AHexUnitActor* OtherUnit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Faction")
	FText GetFactionDisplayText() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	static int32 GetMaxProgressionLevel();

	// Runtime combat stats use 10x integer precision.
	static int32 GetCombatStatScale();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	static int32 GetExperienceToNextLevelForLevel(int32 Level);

	// Coin price for one manual level-up. Level 1 -> 2 costs 50,
	// then every next transition costs 25 coins more. Max level returns 0.
	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	static int32 GetUpgradeCoinCostForLevel(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	static int32 GetScaledStatForLevel(int32 BaseValue, int32 Level);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	int32 GetClampedUnitLevel() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	int32 GetScaledMaxHealthForLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	int32 GetScaledAttackDamageForLevel(int32 Level) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Progression")
	void SetProgressionState(int32 NewLevel, int32 NewCurrentExperience);

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Progression")
	void ApplyProgressionStatsToRuntimeUnit();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	int32 GetExperienceToNextLevel() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Progression")
	float GetExperienceProgressPercent() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Army Builder")
	int32 GetArmyPowerValue() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Army Builder")
	int32 GetArmyPowerValueForLevel(int32 Level) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Combat")
	bool PlayAttackAnimation();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Combat|Projectile", meta = (DisplayName = "Uses Attack Projectile"))
	bool UsesAttackProjectileVFX() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Combat|Projectile")
	FVector GetAttackProjectileStartLocation() const;

	// Used by the grid while the visual projectile is in flight.
	// This remains separate from the montage lock, so a short montage cannot unlock the unit early.
	void SetAttackProjectileActionLocked(bool bLocked);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	bool CanUseChampionAbility() const;

	// True while the champion's active ability effect is still running.
	// For now this blocks Last Stand while bLastStandActive is true.
	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	bool IsChampionAbilityEffectActive() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	bool IsChampionAbilityOnCooldown() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	int32 GetRemainingChampionAbilityCooldownTurns() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability")
	void StartChampionAbilityCooldown();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability")
	void ReduceChampionAbilityCooldownAtOwnerTurnStart();

	// Use this for starting/clicking the ability.
	// CanUseChampionAbility() only means that this unit has an ability at all.
	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	bool CanActivateChampionAbility() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	bool IsChampionAbilitySummon() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability|Last Stand")
	bool IsChampionAbilityLastStand() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability|Marked for Death")
	bool IsChampionAbilityMarkedForDeath() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability|Last Stand")
	bool ActivateLastStand();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability|Last Stand")
	bool TrySurviveLethalDamageWithLastStand();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability|Last Stand")
	void ReduceLastStandDurationAtOwnerTurnStart();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Status|Marked for Death")
	void ApplyMarkedForDeath(float DamageIncreasePercent, int32 DurationTurns);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Status|Marked for Death")
	bool IsMarkedForDeathActive() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Status|Marked for Death")
	int32 GetModifiedIncomingDamage(int32 RawDamage) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Status|Marked for Death")
	void ReduceMarkedForDeathDurationAtOwnerTurnStart();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Champion Ability")
	int32 GetChampionAbilityActionPointCost() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability")
	bool PlayChampionAbilityAnimation();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Champion Ability|VFX")
	void PlayChampionAbilityVFX(AHexUnitActor* TargetUnit, const FVector& TargetLocation);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Healer|Healing")
	bool CanHeal() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Healer|Healing")
	bool CanHealTarget(const AHexUnitActor* TargetUnit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Healer|Healing")
	int32 GetHealActionPointCost() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Healer|Healing")
	bool PlayHealAnimation();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Healer|Healing|VFX")
	void PlayHealVFX(AHexUnitActor* HealTarget);

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Summon|Summoner")
	bool CanSummon() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Summon|Summoner")
	bool IsSummonedUnitClassValid() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Summon|Summoner")
	int32 GetSummonActionPointCost() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Summon|Summoner")
	int32 GetSummonRange() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Summon|Summoner")
	int32 GetSummonUnitCount() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Summon|Summoner|VFX")
	void PlaySummonVFX(const FVector& SummonLocation);

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Start Rotation")
	void ApplyInitialTeamRotation();

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Summon")
	void InitializeSummonedLifetime();

	//    1  .  true,    .
	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Summon")
	bool ConsumeSummonedOwnerTurn();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Summon")
	bool ShouldSkipKillActionPointBonus() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|State")
	bool CanAct() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|State")
	bool GetIsActionLocked() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|State")
	bool GetIsHitReactionLocked() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Health")
	void TakeUnitDamage(int32 DamageAmount);

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Health")
	void HealUnit(int32 InHealAmount);

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Health")
	void Die();

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Health")
	bool GetIsDead() const;

	UFUNCTION(BlueprintPure, Category = "Hex Unit|Health")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Unit|Health Bar")
	void UpdateHealthBarWidget();

private:
	UPROPERTY()
	TArray<FVector> PathPoints;

	int32 RuntimeBaseMaxHealth = 0;
	int32 RuntimeBaseAttackDamage = 0;
	int32 RuntimeBaseHealAmount = 0;
	bool bRuntimeBaseStatsCaptured = false;

	bool bInitialTeamRotationApplied = false;
	bool bIsAttackProjectileActionLocked = false;

	// ,  ,      ,   1       .
	bool bSummonedLifetimeCountdownStarted = false;

	// The first owner-turn start after application only starts the countdown.
	// This gives the effect the configured number of full enemy attack windows.
	bool bMarkedForDeathCountdownStarted = false;

	FTimerHandle HitReactionLockTimerHandle;

	void AdvanceMovement(float DeltaTime);
	void PlayHitReaction();
	void ClearHitReactionLock();
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void OnChampionAbilityMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void OnHealMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void OnHitReactMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};

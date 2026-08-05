// SUPPORT HEALER NEW v3 - generated 2026-06-20, contains bCanHeal/HealAmount/HealMontage and InHealAmount fix
#include "HexGridActor.h"

#include "HexUnitActor.h"
#include "HexAttackProjectile.h"
#include "ArmyBuilderWidget.h"
#include "GameLoadingGameInstance.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Algo/RandomShuffle.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Widget.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 ExactPhotoCellBase = 200000;
	constexpr int32 LegacyPhotoDeploymentCoordBase = 100000;
	constexpr int32 LegacyPhotoDeploymentCoordScale = 10000;

	bool DecodeExactPhotoCell(int32 EncodedQ, int32 EncodedR, int32& OutColumn, int32& OutRow)
	{
		if (EncodedQ < ExactPhotoCellBase || EncodedQ >= ExactPhotoCellBase + 1000 ||
			EncodedR < ExactPhotoCellBase || EncodedR >= ExactPhotoCellBase + 1000)
		{
			return false;
		}
		OutColumn = EncodedQ - ExactPhotoCellBase;
		OutRow = EncodedR - ExactPhotoCellBase;
		return true;
	}

	// Compatibility only for deployments saved by v4/v5. New saves never use this.
	bool DecodeLegacyPhotoDeploymentCoord(int32 EncodedQ, int32 EncodedR, FVector2D& OutNormalized)
	{
		if (EncodedQ < LegacyPhotoDeploymentCoordBase || EncodedQ > LegacyPhotoDeploymentCoordBase + LegacyPhotoDeploymentCoordScale ||
			EncodedR < LegacyPhotoDeploymentCoordBase || EncodedR > LegacyPhotoDeploymentCoordBase + LegacyPhotoDeploymentCoordScale)
		{
			return false;
		}

		OutNormalized.X = static_cast<float>(EncodedQ - LegacyPhotoDeploymentCoordBase) / static_cast<float>(LegacyPhotoDeploymentCoordScale);
		OutNormalized.Y = static_cast<float>(EncodedR - LegacyPhotoDeploymentCoordBase) / static_cast<float>(LegacyPhotoDeploymentCoordScale);
		return true;
	}
}

AHexGridActor::AHexGridActor()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	HexMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HexMeshComponent"));
	HexMeshComponent->SetupAttachment(SceneRoot);

	HexMeshComponent->SetMobility(EComponentMobility::Movable);

	HexMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HexMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	HexMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	HexMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HexMeshComponent->SetGenerateOverlapEvents(false);

	// PerInstanceCustomData:
	// Index 0 = normal hover / selected player/empty cell (yellow)
	// Index 1 = reachable movement cell (set this color to blue in the material)
	// Index 2 = enemy unit hover / selected enemy cell (red)
	// Index 3 = attack range border / orange outline
	// Index 4 = champion ability preview range (set this color to purple in the material)
	// Index 5 = player/allied unit hover or selection (green)
	HexMeshComponent->NumCustomDataFloats = 6;

	PlayerArmyDeploymentCoords =
	{
		FHexCoord(-4, 0),
		FHexCoord(-6, 2),
		FHexCoord(-4, -2),
		FHexCoord(-2, -4),
		FHexCoord(-6, 4)
	};

	EnemyArmyDeploymentCoords =
	{
		FHexCoord(4, 0),
		FHexCoord(4, 2),
		FHexCoord(2, 4),
		FHexCoord(6, -4),
		FHexCoord(6, -2)
	};

	// Difficulty bands are relative to the weakest/strongest legal composition
	// for the current roster, unit count and EnemyArmyLevel.
	WarmUpEnemyArmyPowerBand = FEnemyArmyPowerBand(0.00f, 0.35f);
	ChallengeEnemyArmyPowerBand = FEnemyArmyPowerBand(0.35f, 0.60f);
	OrdealEnemyArmyPowerBand = FEnemyArmyPowerBand(0.60f, 0.85f);
	NightmareEnemyArmyPowerBand = FEnemyArmyPowerBand(1.00f, 1.00f);
}

void AHexGridActor::ApplyEnemyBotDifficultyFromLevelOptions()
{
	if (!bReadEnemyBotDifficultyFromLevelOptions || !GetWorld())
	{
		return;
	}

	const FString DifficultyOption = GetWorld()->URL.GetOption(TEXT("BotDifficulty="), TEXT(""));
	if (DifficultyOption.IsEmpty())
	{
		return;
	}

	if (DifficultyOption.Equals(TEXT("WarmUp"), ESearchCase::IgnoreCase) || DifficultyOption.Equals(TEXT("Warm Up"), ESearchCase::IgnoreCase))
	{
		EnemyBotDifficulty = EHexBotDifficulty::WarmUp;
	}
	else if (DifficultyOption.Equals(TEXT("Challenge"), ESearchCase::IgnoreCase))
	{
		EnemyBotDifficulty = EHexBotDifficulty::Challenge;
	}
	else if (DifficultyOption.Equals(TEXT("Ordeal"), ESearchCase::IgnoreCase))
	{
		EnemyBotDifficulty = EHexBotDifficulty::Ordeal;
	}
	else if (DifficultyOption.Equals(TEXT("Nightmare"), ESearchCase::IgnoreCase))
	{
		EnemyBotDifficulty = EHexBotDifficulty::Nightmare;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Unknown BotDifficulty option: %s. Using current EnemyBotDifficulty."), *DifficultyOption);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Enemy bot difficulty from level option: %s"), *DifficultyOption);
}

bool AHexGridActor::IsEnemyBotDifficultyAtLeast(EHexBotDifficulty MinimumDifficulty) const
{
	return static_cast<uint8>(EnemyBotDifficulty) >= static_cast<uint8>(MinimumDifficulty);
}

float AHexGridActor::GetEnemyBotDifficultyTargetPriorityScale() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return 0.28f;

	case EHexBotDifficulty::Challenge:
		return 1.00f;

	case EHexBotDifficulty::Ordeal:
		return 1.35f;

	case EHexBotDifficulty::Nightmare:
		return 1.85f;

	default:
		return 1.00f;
	}
}

float AHexGridActor::GetEnemyBotDifficultyKillScoreScale() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return 0.55f;

	case EHexBotDifficulty::Challenge:
		return 1.00f;

	case EHexBotDifficulty::Ordeal:
		return 1.25f;

	case EHexBotDifficulty::Nightmare:
		return 1.65f;

	default:
		return 1.00f;
	}
}

float AHexGridActor::GetEnemyBotDifficultyMistakeChance() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return 0.28f;

	case EHexBotDifficulty::Challenge:
		return 0.10f;

	case EHexBotDifficulty::Ordeal:
		return 0.03f;

	case EHexBotDifficulty::Nightmare:
		return 0.0f;

	default:
		return 0.05f;
	}
}

float AHexGridActor::GetEnemyBotDifficultyFutureThreatScale() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return 0.0f;

	case EHexBotDifficulty::Challenge:
		return 0.35f;

	case EHexBotDifficulty::Ordeal:
		return 0.95f;

	case EHexBotDifficulty::Nightmare:
		return 1.45f;

	default:
		return 1.0f;
	}
}

float AHexGridActor::GetEnemyBotTargetBaseValue(AHexUnitActor* Target) const
{
	if (!IsValid(Target) || Target->GetIsDead())
	{
		return 0.0f;
	}

	float Value = 500.0f;
	switch (Target->UnitType)
	{
	case EHexUnitType::Champion:
		Value = 1000.0f;
		break;

	case EHexUnitType::Healer:
	case EHexUnitType::Support:
		Value = Target->CanHeal() ? 900.0f : 650.0f;
		break;

	case EHexUnitType::Ram:
		Value = 450.0f;
		break;

	case EHexUnitType::Skirmisher:
		Value = Target->AttackRange > 1 ? 750.0f : 500.0f;
		break;

	default:
		Value = 500.0f;
		break;
	}

	if (Target->UnitType == EHexUnitType::Champion && Target->ChampionAbilityType == EHexChampionAbilityType::Summon)
	{
		Value += 180.0f;
	}

	if (Target->bIsSummonedUnit)
	{
		Value = FMath::Min(Value, 260.0f);
	}

	return Value;
}

float AHexGridActor::GetEnemyBotFactionTargetBonus(AHexUnitActor* EnemyUnit, AHexUnitActor* Target) const
{
	if (!IsValid(EnemyUnit) || !IsValid(Target) || !IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Ordeal))
	{
		return 0.0f;
	}

	float Bonus = 0.0f;
	switch (EnemyUnit->Faction)
	{
	case EHexUnitFaction::Kingdom:
		if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer))
		{
			Bonus += 180.0f;
		}
		if (Target->AttackRange > 1)
		{
			Bonus += 130.0f;
		}
		if (Target->UnitType == EHexUnitType::Ram && Target->CurrentHealth > Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage))
		{
			Bonus -= 140.0f;
		}
		break;

	case EHexUnitFaction::Soul:
		if (Target->UnitType == EHexUnitType::Champion && Target->ChampionAbilityType == EHexChampionAbilityType::Summon)
		{
			Bonus += 260.0f;
		}
		if (Target->bIsSummonedUnit)
		{
			Bonus -= 180.0f;
		}
		if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer))
		{
			Bonus += 150.0f;
		}
		break;

	case EHexUnitFaction::Animal:
		if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer))
		{
			Bonus += 220.0f;
		}
		if (Target->AttackRange > 1)
		{
			Bonus += 180.0f;
		}
		if (Target->MaxHealth > 0 && static_cast<float>(Target->CurrentHealth) / static_cast<float>(Target->MaxHealth) <= 0.45f)
		{
			Bonus += 140.0f;
		}
		if (Target->UnitType == EHexUnitType::Ram && Target->CurrentHealth > Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage))
		{
			Bonus -= 110.0f;
		}
		break;

	default:
		break;
	}

	if (EnemyBotDifficulty == EHexBotDifficulty::Nightmare)
	{
		Bonus *= 1.35f;
	}

	return Bonus;
}

float AHexGridActor::GetEnemyBotFactionMoveBonus(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, int32 NearestPlayerDistance, bool bCanAttackFromCandidate) const
{
	(void)CandidateCoord;

	if (!IsValid(EnemyUnit) || !IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Ordeal))
	{
		return 0.0f;
	}

	float Bonus = 0.0f;
	switch (EnemyUnit->Faction)
	{
	case EHexUnitFaction::Kingdom:
		if (EnemyUnit->UnitType == EHexUnitType::Ram && NearestPlayerDistance <= 2)
		{
			Bonus += 150.0f;
		}
		if ((EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer) && NearestPlayerDistance >= 3 && NearestPlayerDistance <= 5)
		{
			Bonus += 120.0f;
		}
		if (EnemyUnit->AttackRange > 1 && bCanAttackFromCandidate && NearestPlayerDistance >= 2)
		{
			Bonus += 120.0f;
		}
		break;

	case EHexUnitFaction::Soul:
		if (EnemyUnit->UnitType == EHexUnitType::Champion && EnemyUnit->ChampionAbilityType == EHexChampionAbilityType::Summon)
		{
			Bonus += static_cast<float>(NearestPlayerDistance) * 35.0f;
		}
		if (EnemyUnit->bIsSummonedUnit && NearestPlayerDistance <= 2)
		{
			Bonus += 140.0f;
		}
		break;

	case EHexUnitFaction::Animal:
		if (bCanAttackFromCandidate)
		{
			Bonus += 160.0f;
		}
		if (EnemyUnit->AttackRange <= 1 && NearestPlayerDistance <= 2)
		{
			Bonus += 110.0f;
		}
		if (NearestPlayerDistance > 5)
		{
			Bonus -= 100.0f;
		}
		break;

	default:
		break;
	}

	if (EnemyBotDifficulty == EHexBotDifficulty::Nightmare)
	{
		Bonus *= 1.25f;
	}

	return Bonus;
}

bool AHexGridActor::ShouldEnemyBotMakeDifficultyMistake(float ChanceMultiplier) const
{
	const float Chance = FMath::Clamp(GetEnemyBotDifficultyMistakeChance() * FMath::Max(0.0f, ChanceMultiplier), 0.0f, 1.0f);
	return Chance > 0.0f && FMath::FRand() < Chance;
}

int32 AHexGridActor::GetEnemyBotPlanningDepth() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return 1;

	case EHexBotDifficulty::Challenge:
		return 2;

	case EHexBotDifficulty::Ordeal:
		return 3;

	case EHexBotDifficulty::Nightmare:
		return 5;

	default:
		return 1;
	}
}

void AHexGridActor::RefreshEnemyBotPlan(bool bForce)
{
	EnemyBotPlannedHorizon = GetEnemyBotPlanningDepth();
	EnemyBotPlannedFrontCell = GetArmyCenterCell(EHexUnitTeam::Player);

	AHexUnitActor* CurrentTarget = EnemyBotPlannedFocusTarget.Get();
	const bool bCurrentTargetValid = IsValid(CurrentTarget) && !CurrentTarget->GetIsDead() && CurrentTarget->Team == EHexUnitTeam::Player;

	AHexUnitActor* BestTarget = FindEnemyBotPlanFocusTarget();
	if (!IsValid(BestTarget))
	{
		EnemyBotPlannedFocusTarget.Reset();
		EnemyBotPlanTurnsRemaining = 0;
		return;
	}

	if (!bForce && bCurrentTargetValid && EnemyBotPlanTurnsRemaining > 0 && CurrentTarget != BestTarget)
	{
		const float CurrentScore = ScoreEnemyBotPlanTarget(CurrentTarget);
		const float BestScore = ScoreEnemyBotPlanTarget(BestTarget);

		// The bot keeps its plan unless a clearly better target appears.
		// This prevents "one-step thinking", but still lets it react to a wounded champion/support.
		if (BestScore < CurrentScore * 1.25f + 180.0f)
		{
			EnemyBotPlanTurnsRemaining = FMath::Max(1, EnemyBotPlanTurnsRemaining - 1);
			return;
		}
	}

	EnemyBotPlannedFocusTarget = BestTarget;
	EnemyBotPlanTurnsRemaining = FMath::Max(1, EnemyBotPlannedHorizon);
}

AHexUnitActor* AHexGridActor::FindEnemyBotPlanFocusTarget() const
{
	AHexUnitActor* BestTarget = nullptr;
	float BestScore = -1000000000.0f;

	TArray<AHexUnitActor*> PlayerUnits;
	CollectAlivePlayerUnits(PlayerUnits);

	for (AHexUnitActor* PlayerUnit : PlayerUnits)
	{
		const float Score = ScoreEnemyBotPlanTarget(PlayerUnit);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = PlayerUnit;
		}
	}

	return BestTarget;
}

float AHexGridActor::ScoreEnemyBotPlanTarget(AHexUnitActor* Target) const
{
	if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
	{
		return -1000000000.0f;
	}

	const FIntPoint TargetCoord = Target->GetGridCoord();
	const float HealthPercent = Target->MaxHealth > 0
		? static_cast<float>(Target->CurrentHealth) / static_cast<float>(Target->MaxHealth)
		: 1.0f;

	float Score = GetEnemyBotTargetBaseValue(Target) * (0.70f + 0.25f * static_cast<float>(GetEnemyBotPlanningDepth()));
	Score += (1.0f - HealthPercent) * 480.0f;
	Score += (static_cast<float>(Target->AttackDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * 3.0f;
	Score += static_cast<float>(Target->AttackRange) * 28.0f;

	if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer) && Target->CanHeal())
	{
		Score += 280.0f;
	}

	if (Target->UnitType == EHexUnitType::Champion)
	{
		Score += 220.0f;
	}

	if (IsPlayerUnitIsolatedForEnemyBot(Target))
	{
		Score += EnemyBotIsolatedTargetBonus;
	}

	const int32 CurrentPressureAttackers = CountEnemyBotAttackersThreateningPlayerTarget(Target, 0, nullptr);
	if (CurrentPressureAttackers > 0)
	{
		Score += static_cast<float>(FMath::Min(CurrentPressureAttackers, GetDesiredEnemyBotAttackersForTarget(Target))) * EnemyBotFocusFireBonus * 0.35f;
	}

	int32 EnemyAttackersInOneTurn = 0;
	int32 PotentialDamage = 0;
	int32 BestDistance = MAX_int32;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* EnemyUnit = Pair.Value;
		if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(EnemyCoord.X, EnemyCoord.Y, TargetCoord.X, TargetCoord.Y);
		BestDistance = FMath::Min(BestDistance, Distance);

		const int32 Reach = FMath::Max(0, EnemyUnit->MovementRange) + FMath::Max(1, EnemyUnit->AttackRange);
		if (Distance > 0 && Distance <= Reach)
		{
			EnemyAttackersInOneTurn++;
			PotentialDamage += FMath::Max(0, EnemyUnit->AttackDamage);
		}
	}

	Score += static_cast<float>(EnemyAttackersInOneTurn) * 130.0f;
	Score += static_cast<float>(PotentialDamage) * 1.15f;

	if (PotentialDamage >= Target->CurrentHealth)
	{
		Score += EnemyBotKillScoreBonus * GetEnemyBotDifficultyKillScoreScale();
	}

	if (BestDistance != MAX_int32)
	{
		Score -= static_cast<float>(BestDistance) * 18.0f;
	}

	return Score;
}


float AHexGridActor::GetEnemyBotUnitBoardValue(AHexUnitActor* Unit) const
{
	if (!IsValid(Unit) || Unit->GetIsDead())
	{
		return 0.0f;
	}

	float Value = 120.0f;
	Value += (static_cast<float>(FMath::Max(1, Unit->MaxHealth)) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * 1.15f;
	Value += (static_cast<float>(FMath::Max(0, Unit->AttackDamage)) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * 5.5f;
	Value += static_cast<float>(FMath::Max(1, Unit->AttackRange)) * 45.0f;
	Value += static_cast<float>(FMath::Max(0, Unit->MovementRange)) * 32.0f;

	switch (Unit->UnitType)
	{
	case EHexUnitType::Champion:
		Value += 560.0f;
		break;

	case EHexUnitType::Healer:
	case EHexUnitType::Support:
		Value += Unit->CanHeal() ? 520.0f : 250.0f;
		break;

	case EHexUnitType::Ram:
		Value += 380.0f;
		break;

	case EHexUnitType::Skirmisher:
		Value += 190.0f;
		break;

	default:
		break;
	}

	if (Unit->bIsSummonedUnit)
	{
		Value *= 0.55f;
	}

	return Value;
}

int32 AHexGridActor::CountEnemyBotAttackersThreateningPlayerTarget(AHexUnitActor* Target, int32 ExtraReach, AHexUnitActor* IgnoreEnemyUnit) const
{
	if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
	{
		return 0;
	}

	int32 Count = 0;
	const FIntPoint TargetCoord = Target->GetGridCoord();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* EnemyUnit = Pair.Value;
		if (!IsValid(EnemyUnit) || EnemyUnit == IgnoreEnemyUnit || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(EnemyCoord.X, EnemyCoord.Y, TargetCoord.X, TargetCoord.Y);
		const int32 Reach = FMath::Max(1, EnemyUnit->AttackRange) + FMath::Max(0, ExtraReach);
		if (Distance > 0 && Distance <= Reach)
		{
			Count++;
		}
	}

	return Count;
}

int32 AHexGridActor::GetPotentialEnemyBotDamageToPlayerTarget(AHexUnitActor* Target, int32 ExtraReach, AHexUnitActor* IgnoreEnemyUnit) const
{
	if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
	{
		return 0;
	}

	int32 Damage = 0;
	const FIntPoint TargetCoord = Target->GetGridCoord();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* EnemyUnit = Pair.Value;
		if (!IsValid(EnemyUnit) || EnemyUnit == IgnoreEnemyUnit || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(EnemyCoord.X, EnemyCoord.Y, TargetCoord.X, TargetCoord.Y);
		const int32 Reach = FMath::Max(1, EnemyUnit->AttackRange) + FMath::Max(0, ExtraReach);
		if (Distance > 0 && Distance <= Reach)
		{
			Damage += Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage);
		}
	}

	return Damage;
}

int32 AHexGridActor::GetDesiredEnemyBotAttackersForTarget(AHexUnitActor* Target) const
{
	if (!IsValid(Target) || Target->GetIsDead())
	{
		return 1;
	}

	if (Target->UnitType == EHexUnitType::Champion)
	{
		return 4;
	}

	if (Target->UnitType == EHexUnitType::Ram)
	{
		return 3;
	}

	if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer) && Target->CanHeal())
	{
		return 2;
	}

	const float HealthPercent = Target->MaxHealth > 0
		? static_cast<float>(Target->CurrentHealth) / static_cast<float>(Target->MaxHealth)
		: 1.0f;

	return HealthPercent <= 0.35f ? 1 : 2;
}

bool AHexGridActor::IsPlayerUnitIsolatedForEnemyBot(AHexUnitActor* Target) const
{
	if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
	{
		return false;
	}

	const FIntPoint TargetCoord = Target->GetGridCoord();
	int32 NearbyPlayerAllies = 0;
	int32 NearbyEnemies = 0;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit == Target || Unit->GetIsDead())
		{
			continue;
		}

		const FIntPoint UnitCoord = Unit->GetGridCoord();
		const int32 Distance = GetHexDistance(TargetCoord.X, TargetCoord.Y, UnitCoord.X, UnitCoord.Y);
		if (Unit->Team == EHexUnitTeam::Player && Distance <= 3)
		{
			NearbyPlayerAllies++;
		}
		else if (Unit->Team == EHexUnitTeam::Enemy && Distance <= 3)
		{
			NearbyEnemies++;
		}
	}

	return NearbyEnemies >= 1 && NearbyPlayerAllies <= 0;
}

bool AHexGridActor::IsEnemyBotAllyDoomed(AHexUnitActor* Ally, int32 ExtraHealing) const
{
	if (!IsValid(Ally) || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	int32 DirectDamage = 0;
	IsCellThreatenedByPlayerUnits(Ally->GetGridCoord().X, Ally->GetGridCoord().Y, DirectDamage);

	int32 FutureDamage = 0;
	IsCellThreatenedByPlayerUnitsAfterMove(Ally->GetGridCoord().X, Ally->GetGridCoord().Y, FutureDamage);

	const int32 ExpectedDamage = FMath::Max(DirectDamage, FutureDamage);
	return ExpectedDamage >= Ally->CurrentHealth + FMath::Max(0, ExtraHealing);
}

float AHexGridActor::ScoreEnemyBotTargetPressure(AHexUnitActor* EnemyUnit, AHexUnitActor* Target) const
{
	if (!IsValid(EnemyUnit) || !IsValid(Target) || Target->GetIsDead())
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const int32 OtherAttackersNow = CountEnemyBotAttackersThreateningPlayerTarget(Target, 0, EnemyUnit);
	const int32 OtherPotentialDamage = GetPotentialEnemyBotDamageToPlayerTarget(Target, EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 0 : EnemyUnit->MovementRange, EnemyUnit);
	const int32 DesiredAttackers = GetDesiredEnemyBotAttackersForTarget(Target);

	if (OtherAttackersNow > 0)
	{
		Score += static_cast<float>(FMath::Min(OtherAttackersNow, DesiredAttackers)) * EnemyBotFocusFireBonus;
	}

	if (OtherPotentialDamage + FMath::Max(0, EnemyUnit->AttackDamage) >= Target->CurrentHealth)
	{
		Score += EnemyBotExecuteKillBonus * 0.55f;
	}

	if (OtherAttackersNow >= DesiredAttackers && Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) < Target->CurrentHealth && Target->UnitType != EHexUnitType::Champion)
	{
		Score -= static_cast<float>(OtherAttackersNow - DesiredAttackers + 1) * EnemyBotOvercommitPenalty;
	}

	if (IsPlayerUnitIsolatedForEnemyBot(Target))
	{
		Score += EnemyBotIsolatedTargetBonus;
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotTrade(AHexUnitActor* EnemyUnit, AHexUnitActor* Target, bool bCanKillTarget) const
{
	if (!IsValid(EnemyUnit) || !IsValid(Target))
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const float EnemyValue = GetEnemyBotUnitBoardValue(EnemyUnit);
	const float TargetValue = GetEnemyBotUnitBoardValue(Target);

	if (bCanKillTarget)
	{
		Score += (TargetValue - EnemyValue * 0.55f) * (EnemyBotTradeScore / 500.0f);
	}

	if (EnemyUnit->bIsSummonedUnit && bCanKillTarget)
	{
		Score += EnemyBotTradeScore * 0.75f;
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotCounterplayAtCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, bool bCanAttackFromCandidate, bool bCanKillFromCandidate) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return 0.0f;
	}

	int32 FutureThreatDamage = 0;
	const bool bFutureThreatened = IsCellThreatenedByPlayerUnitsAfterMove(CandidateCoord.X, CandidateCoord.Y, FutureThreatDamage);
	if (!bFutureThreatened || FutureThreatDamage <= 0)
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const bool bCanBeKilled = FutureThreatDamage >= EnemyUnit->CurrentHealth;
	const bool bImportantUnit = EnemyUnit->UnitType == EHexUnitType::Champion || (EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer) || EnemyUnit->AttackRange > 1;

	if (bCanBeKilled)
	{
		float PenaltyScale = 1.0f;
		if (EnemyUnit->UnitType == EHexUnitType::Ram)
		{
			PenaltyScale = 0.45f;
		}
		else if (EnemyUnit->bIsSummonedUnit)
		{
			PenaltyScale = 0.25f;
		}
		else if (bImportantUnit)
		{
			PenaltyScale = 1.45f;
		}

		if (bCanKillFromCandidate)
		{
			PenaltyScale *= 0.35f;
		}
		else if (bCanAttackFromCandidate)
		{
			PenaltyScale *= 0.70f;
		}

		Score -= EnemyBotFreeKillPenalty * PenaltyScale;
	}
	else if (bImportantUnit)
	{
		Score -= static_cast<float>(FutureThreatDamage) * 1.2f;
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotBacklineScreen(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const int32 CandidateNearestPlayer = GetNearestPlayerDistanceFromCell(CandidateCoord);
	if (CandidateNearestPlayer == MAX_int32)
	{
		return 0.0f;
	}

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == EnemyUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const bool bBacklineAlly = (Ally->UnitType == EHexUnitType::Support || Ally->UnitType == EHexUnitType::Healer) || (Ally->AttackRange > 1 && Ally->UnitType != EHexUnitType::Ram);
		if (!bBacklineAlly)
		{
			continue;
		}

		const FIntPoint AllyCoord = Ally->GetGridCoord();
		const int32 DistanceToAlly = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, AllyCoord.X, AllyCoord.Y);
		const int32 AllyNearestPlayer = GetNearestPlayerDistanceFromCell(AllyCoord);
		if (DistanceToAlly >= 1 && DistanceToAlly <= 2 && CandidateNearestPlayer < AllyNearestPlayer)
		{
			if (EnemyUnit->UnitType == EHexUnitType::Ram || EnemyUnit->UnitType == EHexUnitType::Champion)
			{
				Score += EnemyBotBacklineScreenScore;
			}
			else if (EnemyUnit->AttackRange <= 1)
			{
				Score += EnemyBotBacklineScreenScore * 0.55f;
			}
		}
	}

	if ((EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer) || (EnemyUnit->AttackRange > 1 && EnemyUnit->UnitType != EHexUnitType::Ram))
	{
		const int32 DistanceToRam = GetNearestAliveEnemyRamDistanceFromCell(CandidateCoord, EnemyUnit);
		if (DistanceToRam != MAX_int32)
		{
			if (DistanceToRam >= 1 && DistanceToRam <= 3)
			{
				Score += EnemyBotBacklineScreenScore * 0.45f;
			}
			else if (DistanceToRam > 4)
			{
				Score -= EnemyBotBacklineScreenScore * 0.35f;
			}
		}
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotRetreatQuality(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return 0.0f;
	}

	const FIntPoint CurrentCoord = EnemyUnit->GetGridCoord();
	const int32 CurrentNearestPlayer = GetNearestPlayerDistanceFromCell(CurrentCoord);
	const int32 CandidateNearestPlayer = GetNearestPlayerDistanceFromCell(CandidateCoord);
	if (CurrentNearestPlayer == MAX_int32 || CandidateNearestPlayer == MAX_int32)
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const int32 CurrentNearbyAllies = GetEnemyAllyCountNearCell(CurrentCoord, 2, EnemyUnit);
	const int32 CandidateNearbyAllies = GetEnemyAllyCountNearCell(CandidateCoord, 2, EnemyUnit);
	Score += static_cast<float>(CandidateNearbyAllies - CurrentNearbyAllies) * EnemyBotFormationCohesionScore * 0.90f;

	const FIntPoint EnemyCenter = GetArmyCenterCell(EHexUnitTeam::Enemy);
	const int32 CurrentDistanceToCenter = GetHexDistance(CurrentCoord.X, CurrentCoord.Y, EnemyCenter.X, EnemyCenter.Y);
	const int32 CandidateDistanceToCenter = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, EnemyCenter.X, EnemyCenter.Y);
	if (CandidateDistanceToCenter < CurrentDistanceToCenter)
	{
		Score += static_cast<float>(CurrentDistanceToCenter - CandidateDistanceToCenter) * 80.0f;
	}
	else if (CandidateDistanceToCenter > CurrentDistanceToCenter + 1)
	{
		Score -= static_cast<float>(CandidateDistanceToCenter - CurrentDistanceToCenter) * 90.0f;
	}

	if (CandidateNearestPlayer <= CurrentNearestPlayer && CandidateNearbyAllies <= CurrentNearbyAllies)
	{
		Score -= 240.0f;
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotFlankPressure(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || !IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Ordeal))
	{
		return 0.0f;
	}

	const bool bCanFlank = EnemyUnit->Faction == EHexUnitFaction::Animal || EnemyUnit->UnitType == EHexUnitType::Skirmisher || EnemyUnit->MovementRange >= 4;
	if (!bCanFlank)
	{
		return 0.0f;
	}

	const FIntPoint PlayerCenter = GetArmyCenterCell(EHexUnitTeam::Player);
	const int32 DistanceToPlayerCenter = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, PlayerCenter.X, PlayerCenter.Y);
	if (DistanceToPlayerCenter > FMath::Max(3, EnemyUnit->MovementRange + EnemyUnit->AttackRange))
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const int32 SideOffset = FMath::Abs((CandidateCoord.X - PlayerCenter.X) - (CandidateCoord.Y - PlayerCenter.Y));
	Score += static_cast<float>(FMath::Min(SideOffset, 4)) * EnemyBotFlankPressureScore * 0.22f;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const bool bBacklineTarget = (PlayerUnit->UnitType == EHexUnitType::Support || PlayerUnit->UnitType == EHexUnitType::Healer) || PlayerUnit->AttackRange > 1;
		if (!bBacklineTarget)
		{
			continue;
		}

		const FIntPoint TargetCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, TargetCoord.X, TargetCoord.Y);
		if (Distance <= EnemyUnit->AttackRange + 1)
		{
			Score += EnemyBotFlankPressureScore;
		}
	}

	return Score;
}

bool AHexGridActor::IsEnemyBotCellBlockingImportantAlly(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return false;
	}

	AHexUnitActor* FocusTarget = EnemyBotPlannedFocusTarget.Get();
	if (!IsValid(FocusTarget) || FocusTarget->GetIsDead())
	{
		return false;
	}

	const FIntPoint FocusCoord = FocusTarget->GetGridCoord();
	const int32 CandidateDistanceToFocus = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, FocusCoord.X, FocusCoord.Y);

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == EnemyUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const bool bImportantMelee = Ally->AttackDamage > EnemyUnit->AttackDamage + 5 * AHexUnitActor::GetCombatStatScale() || Ally->UnitType == EHexUnitType::Champion || Ally->UnitType == EHexUnitType::Ram;
		if (!bImportantMelee || Ally->AttackRange > 1)
		{
			continue;
		}

		const FIntPoint AllyCoord = Ally->GetGridCoord();
		const int32 AllyDistanceToCandidate = GetHexDistance(AllyCoord.X, AllyCoord.Y, CandidateCoord.X, CandidateCoord.Y);
		const int32 AllyDistanceToFocus = GetHexDistance(AllyCoord.X, AllyCoord.Y, FocusCoord.X, FocusCoord.Y);
		if (AllyDistanceToCandidate <= 1 && CandidateDistanceToFocus < AllyDistanceToFocus && CandidateDistanceToFocus > EnemyUnit->AttackRange)
		{
			return true;
		}
	}

	return false;
}

float AHexGridActor::ScoreEnemyBotUnitTurnPriority(AHexUnitActor* EnemyUnit) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy || !EnemyUnit->CanAct())
	{
		return -1000000000.0f;
	}

	float Score = 0.0f;

	const int32 ActionsAlreadyTaken = GetEnemyBotActionCountThisTurn(EnemyUnit);
	const bool bFrontlineUnit = IsEnemyBotFrontlineUnit(EnemyUnit);
	const bool bBacklineUnit = EnemyUnit->CanHeal() || (EnemyUnit->AttackRange > 1 && EnemyUnit->UnitType != EHexUnitType::Ram);
	if (ActionsAlreadyTaken <= 0)
	{
		Score += EnemyBotFirstActionPriorityBonus;
		if (bFrontlineUnit)
		{
			Score += EnemyBotFrontlineFirstActionBonus;
		}
	}
	else
	{
		Score -= static_cast<float>(ActionsAlreadyTaken) * EnemyBotRepeatActionPenalty;
	}

	AHexUnitActor* AttackTarget = FindBestEnemyBotAttackTarget(EnemyUnit);
	const bool bHasImmediateAttack = CanUnitAttackThisTurn(EnemyUnit) && IsValid(AttackTarget) && HasEnoughActionPoints(CalculateAttackActionPointCost());
	if (bHasImmediateAttack)
	{
		const bool bCanKill = AttackTarget->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) >= AttackTarget->CurrentHealth;
		Score += bCanKill ? 9000.0f : 5200.0f;
		if (bCanKill)
		{
			Score += EnemyBotExecuteKillBonus;
		}
		Score += ScoreEnemyBotTargetPressure(EnemyUnit, AttackTarget) * 0.45f;
		Score += ScoreEnemyBotTrade(EnemyUnit, AttackTarget, bCanKill) * 0.55f;
		Score += (static_cast<float>(EnemyUnit->AttackDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * 45.0f;
		Score += GetEnemyBotTargetBaseValue(AttackTarget) * 0.20f;
		if (AttackTarget->MaxHealth > 0)
		{
			Score += (1.0f - static_cast<float>(AttackTarget->CurrentHealth) / static_cast<float>(AttackTarget->MaxHealth)) * 260.0f;
		}

		if (EnemyBotPlannedFocusTarget.Get() == AttackTarget)
		{
			Score += EnemyBotPlanCommitmentScore * 2.0f;
		}
	}

	bool bHasUsefulHeal = false;
	if (EnemyUnit->CanHeal() && HasEnoughActionPoints(CalculateHealActionPointCost(EnemyUnit)))
	{
		float HealScore = -1000000000.0f;
		AHexUnitActor* HealTarget = FindBestEnemyBotHealTargetFromCell(EnemyUnit, EnemyUnit->GetGridCoord(), &HealScore);
		if (IsValid(HealTarget))
		{
			bHasUsefulHeal = true;
			Score += 4300.0f + HealScore * 0.20f;
		}
	}

	if (ActionsAlreadyTaken <= 0 && bBacklineUnit && GetEnemyBotUnactedFrontlineUnitCount(EnemyUnit) > 0 && !bHasImmediateAttack && !bHasUsefulHeal)
	{
		Score -= EnemyBotBacklineWaitForFrontlinePenalty;
	}

	if (EnemyUnit->bIsSummonedUnit && ActionsAlreadyTaken <= 0 && bHasImmediateAttack)
	{
		Score += EnemyBotImmediateSummonActionPriorityBonus;
	}

	if (EnemyUnit->CanUseChampionAbility() && EnemyUnit->CanActivateChampionAbility() && HasEnoughActionPoints(CalculateChampionAbilityActionPointCost(EnemyUnit)))
	{
		if (EnemyUnit->IsChampionAbilityLastStand())
		{
			if (ShouldEnemyBotUseLastStand(EnemyUnit))
			{
				Score += 7600.0f;
			}
		}
		else if (EnemyUnit->IsChampionAbilityMarkedForDeath())
		{
			if (IsValid(FindBestEnemyBotMarkedForDeathTarget(EnemyUnit)))
			{
				Score += 5800.0f;
			}
		}
		else if (EnemyUnit->IsChampionAbilitySummon() && EnemyUnit->CanSummon() && GetEnemyBotUnactedFrontlineUnitCount(EnemyUnit) <= 0)
		{
			TArray<FIntPoint> UsefulSummonCells;
			float BestSummonUtility = -1000000000.0f;
			if (GetUsefulEnemyBotSummonCells(EnemyUnit, UsefulSummonCells, &BestSummonUtility))
			{
				Score += 4700.0f + BestSummonUtility;
			}
		}
	}

	const int32 NearestPlayerDistance = GetNearestPlayerDistanceFromCell(EnemyUnit->GetGridCoord());
	if (NearestPlayerDistance != MAX_int32)
	{
		Score += static_cast<float>(FMath::Max(0, 8 - NearestPlayerDistance)) * 85.0f;

		if (ActionsAlreadyTaken <= 0 && bFrontlineUnit && !bHasImmediateAttack)
		{
			// Far melee units move first, so they do not enter combat only after the forward ally dies.
			Score += static_cast<float>(FMath::Min(NearestPlayerDistance, 10)) * 240.0f;
		}
	}

	int32 ThreatenedAllyDamage = 0;
	AHexUnitActor* ThreatenedAlly = FindMostThreatenedEnemyAlly(EnemyUnit, &ThreatenedAllyDamage);
	if (IsValid(ThreatenedAlly))
	{
		const FIntPoint UnitCoord = EnemyUnit->GetGridCoord();
		const FIntPoint AllyCoord = ThreatenedAlly->GetGridCoord();
		const int32 DistanceToAlly = GetHexDistance(UnitCoord.X, UnitCoord.Y, AllyCoord.X, AllyCoord.Y);
		Score += static_cast<float>(FMath::Max(0, 6 - DistanceToAlly)) * 140.0f + static_cast<float>(ThreatenedAllyDamage);
		if (IsEnemyBotAllyDoomed(ThreatenedAlly, 0) && !EnemyUnit->CanHeal())
		{
			Score -= EnemyBotDoomedAllySavePenalty * 0.35f;
		}
	}

	// Stable tie-break: stronger units act earlier when the tactical value is similar.
	Score += (static_cast<float>(EnemyUnit->AttackDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * 8.0f;
	Score += static_cast<float>(EnemyUnit->MovementRange) * 12.0f;

	return Score;
}

void AHexGridActor::SortEnemyBotUnitsByTacticalPriority(TArray<AHexUnitActor*>& Units) const
{
	Units.Sort([this](const AHexUnitActor& Left, const AHexUnitActor& Right)
		{
			return ScoreEnemyBotUnitTurnPriority(const_cast<AHexUnitActor*>(&Left)) > ScoreEnemyBotUnitTurnPriority(const_cast<AHexUnitActor*>(&Right));
		});
}

FIntPoint AHexGridActor::GetArmyCenterCell(EHexUnitTeam Team) const
{
	int32 Count = 0;
	float SumQ = 0.0f;
	float SumR = 0.0f;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != Team)
		{
			continue;
		}

		const FIntPoint Coord = Unit->GetGridCoord();
		SumQ += static_cast<float>(Coord.X);
		SumR += static_cast<float>(Coord.Y);
		Count++;
	}

	if (Count <= 0)
	{
		return FIntPoint(0, 0);
	}

	FIntPoint ApproxCenter(FMath::RoundToInt(SumQ / static_cast<float>(Count)), FMath::RoundToInt(SumR / static_cast<float>(Count)));
	if (HasCell(ApproxCenter.X, ApproxCenter.Y))
	{
		return ApproxCenter;
	}

	FIntPoint BestCell = ApproxCenter;
	int32 BestDistance = MAX_int32;
	for (const FHexCell& Cell : Cells)
	{
		const int32 Distance = GetHexDistance(ApproxCenter.X, ApproxCenter.Y, Cell.Q, Cell.R);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestCell = FIntPoint(Cell.Q, Cell.R);
		}
	}

	return BestCell;
}

int32 AHexGridActor::GetAliveUnitCountForTeam(EHexUnitTeam Team) const
{
	int32 Count = 0;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (IsValid(Unit) && !Unit->GetIsDead() && Unit->Team == Team)
		{
			Count++;
		}
	}
	return Count;
}

int32 AHexGridActor::GetEnemyAllyCountNearCell(const FIntPoint& CellCoord, int32 Range, AHexUnitActor* IgnoreUnit) const
{
	int32 Count = 0;
	const int32 SafeRange = FMath::Max(0, Range);

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit == IgnoreUnit || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint UnitCoord = Unit->GetGridCoord();
		if (GetHexDistance(CellCoord.X, CellCoord.Y, UnitCoord.X, UnitCoord.Y) <= SafeRange)
		{
			Count++;
		}
	}

	return Count;
}

int32 AHexGridActor::GetPlayerUnitCountNearCell(const FIntPoint& CellCoord, int32 Range) const
{
	int32 Count = 0;
	const int32 SafeRange = FMath::Max(0, Range);

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint UnitCoord = Unit->GetGridCoord();
		if (GetHexDistance(CellCoord.X, CellCoord.Y, UnitCoord.X, UnitCoord.Y) <= SafeRange)
		{
			Count++;
		}
	}

	return Count;
}

bool AHexGridActor::HasEnemyBotCurrentAttackTarget(AHexUnitActor* EnemyUnit) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();
	const int32 AttackRange = FMath::Max(1, EnemyUnit->AttackRange);

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(
			EnemyCoord.X, EnemyCoord.Y,
			PlayerCoord.X, PlayerCoord.Y
		);

		if (Distance > 0 && Distance <= AttackRange)
		{
			return true;
		}
	}

	return false;
}

float AHexGridActor::ScoreEnemyBotPreemptiveSupportNeed(
	AHexUnitActor* Ally,
	int32* OutThreatCount,
	int32* OutLocalDefenderCount
) const
{
	if (OutThreatCount)
	{
		*OutThreatCount = 0;
	}
	if (OutLocalDefenderCount)
	{
		*OutLocalDefenderCount = 0;
	}

	if (!IsValid(Ally) || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
	{
		return 0.0f;
	}

	const FIntPoint AllyCoord = Ally->GetGridCoord();

	struct FPredictedThreat
	{
		AHexUnitActor* Unit = nullptr;
		float ReachWeight = 0.0f;
		int32 Distance = MAX_int32;
	};

	TArray<FPredictedThreat> Threats;
	float PredictedIncomingPerTurn = 0.0f;
	float ThreatHealthPool = 0.0f;

	// IMPORTANT: this forecast does not wait for Ally->CurrentHealth to become low.
	// It asks: "If this fight starts now and the ally were still at full HP, does the
	// current local matchup look losing over the next few turns?"
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(
			PlayerCoord.X, PlayerCoord.Y,
			AllyCoord.X, AllyCoord.Y
		);

		if (Distance <= 0)
		{
			continue;
		}

		const int32 ImmediateReach = FMath::Max(1, PlayerUnit->AttackRange);
		const int32 NextTurnReach =
			FMath::Max(0, PlayerUnit->MovementRange) +
			FMath::Max(1, PlayerUnit->AttackRange);

		if (Distance > NextTurnReach)
		{
			continue;
		}

		const float ReachWeight = Distance <= ImmediateReach
			? 1.0f
			: FMath::Clamp(EnemyBotReinforcementFutureThreatWeight, 0.0f, 1.0f);

		FPredictedThreat Threat;
		Threat.Unit = PlayerUnit;
		Threat.ReachWeight = ReachWeight;
		Threat.Distance = Distance;
		Threats.Add(Threat);

		PredictedIncomingPerTurn +=
			static_cast<float>(Ally->GetModifiedIncomingDamage(FMath::Max(0, PlayerUnit->AttackDamage))) *
			ReachWeight;

		ThreatHealthPool += static_cast<float>(FMath::Max(1, PlayerUnit->CurrentHealth));
	}

	if (Threats.IsEmpty() || PredictedIncomingPerTurn <= 0.0f)
	{
		return 0.0f;
	}

	// Local friendly damage includes the pressured ally and ONLY allies that are already
	// meaningfully contributing to this exact engagement. Idle units merely standing somewhere
	// nearby are intentionally not counted as "help" until they can actually attack a threat.
	float LocalFriendlyDamagePerTurn = static_cast<float>(FMath::Max(1, Ally->AttackDamage));
	int32 LocalDefenderCount = 1;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Defender = Pair.Value;
		if (!IsValid(Defender) || Defender == Ally || Defender->GetIsDead() || Defender->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint DefenderCoord = Defender->GetGridCoord();
		const int32 DistanceToAlly = GetHexDistance(
			DefenderCoord.X, DefenderCoord.Y,
			AllyCoord.X, AllyCoord.Y
		);

		if (DistanceToAlly > FMath::Max(1, EnemyBotReinforcementLocalSupportRange))
		{
			continue;
		}

		bool bCurrentlyContributes = false;
		for (const FPredictedThreat& Threat : Threats)
		{
			if (!IsValid(Threat.Unit))
			{
				continue;
			}

			const FIntPoint ThreatCoord = Threat.Unit->GetGridCoord();
			const int32 DistanceToThreat = GetHexDistance(
				DefenderCoord.X, DefenderCoord.Y,
				ThreatCoord.X, ThreatCoord.Y
			);

			if (DistanceToThreat > 0 && DistanceToThreat <= FMath::Max(1, Defender->AttackRange))
			{
				bCurrentlyContributes = true;
				break;
			}
		}

		if (!bCurrentlyContributes)
		{
			continue;
		}

		++LocalDefenderCount;
		LocalFriendlyDamagePerTurn += static_cast<float>(FMath::Max(1, Defender->AttackDamage));
	}

	const int32 PredictionTurns = FMath::Clamp(EnemyBotReinforcementPredictionTurns, 1, 4);
	const float FullHealth = static_cast<float>(FMath::Max(1, Ally->MaxHealth));
	const float ProjectedIncoming = PredictedIncomingPerTurn * static_cast<float>(PredictionTurns);
	const float FullHealthPressure = ProjectedIncoming / FullHealth;

	const float SafeFriendlyDamage = FMath::Max(1.0f, LocalFriendlyDamagePerTurn);
	const float PowerDisadvantage = PredictedIncomingPerTurn / SafeFriendlyDamage;

	// Combat-race estimate:
	// - survival time intentionally uses MaxHealth, not CurrentHealth;
	// - clear time uses the current threat HP pool because wounded enemies really are easier to finish.
	const float FullHealthSurvivalTurns = FullHealth / FMath::Max(1.0f, PredictedIncomingPerTurn);
	const float ThreatClearTurns = ThreatHealthPool / SafeFriendlyDamage;

	const bool bHeavyFullHealthPressure =
		FullHealthPressure >= FMath::Max(0.20f, EnemyBotReinforcementFullHealthDangerRatio);

	const bool bPowerDisadvantage =
		PowerDisadvantage >= FMath::Max(0.50f, EnemyBotReinforcementPowerDisadvantageRatio);

	const bool bLosesDamageRace =
		FullHealthSurvivalTurns <= ThreatClearTurns * 1.10f;

	const bool bLocallyOutnumbered =
		Threats.Num() > LocalDefenderCount &&
		PowerDisadvantage >= 0.85f;

	if (!bHeavyFullHealthPressure && !bPowerDisadvantage && !bLosesDamageRace && !bLocallyOutnumbered)
	{
		return 0.0f;
	}

	if (OutThreatCount)
	{
		*OutThreatCount = Threats.Num();
	}
	if (OutLocalDefenderCount)
	{
		*OutLocalDefenderCount = LocalDefenderCount;
	}

	float NeedScore = 900.0f;
	NeedScore += FullHealthPressure * 1500.0f;
	NeedScore += FMath::Max(0.0f, PowerDisadvantage - 0.75f) * 1300.0f;
	NeedScore += static_cast<float>(FMath::Max(0, Threats.Num() - LocalDefenderCount)) * 520.0f;

	if (bLosesDamageRace)
	{
		const float RaceGap = FMath::Max(0.0f, ThreatClearTurns - FullHealthSurvivalTurns);
		NeedScore += 850.0f + RaceGap * 180.0f;
	}

	// Existing damage increases urgency, but it is NEVER required to trigger the request.
	// This keeps the behavior proactive instead of waiting until "half his face is gone".
	const float CurrentHealthPercent = Ally->MaxHealth > 0
		? static_cast<float>(Ally->CurrentHealth) / static_cast<float>(Ally->MaxHealth)
		: 1.0f;
	NeedScore += (1.0f - FMath::Clamp(CurrentHealthPercent, 0.0f, 1.0f)) * 420.0f;

	if (Ally->UnitType == EHexUnitType::Champion)
	{
		NeedScore += 220.0f;
	}
	else if (Ally->UnitType == EHexUnitType::Ram)
	{
		NeedScore += 120.0f;
	}

	if (Ally->bIsSummonedUnit)
	{
		NeedScore *= 0.55f;
	}

	return NeedScore;
}

AHexUnitActor* AHexGridActor::FindEnemyBotPreemptiveSupportTarget(
	AHexUnitActor* ActingUnit,
	float* OutNeedScore
) const
{
	if (OutNeedScore)
	{
		*OutNeedScore = 0.0f;
	}

	if (!IsValid(ActingUnit) || ActingUnit->GetIsDead() || ActingUnit->Team != EHexUnitTeam::Enemy)
	{
		return nullptr;
	}

	AHexUnitActor* BestTarget = nullptr;
	float BestScore = 0.0f;
	const FIntPoint ActingCoord = ActingUnit->GetGridCoord();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == ActingUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const float NeedScore = ScoreEnemyBotPreemptiveSupportNeed(Ally, nullptr, nullptr);
		if (NeedScore <= 0.0f)
		{
			continue;
		}

		const FIntPoint AllyCoord = Ally->GetGridCoord();
		const int32 Distance = GetHexDistance(
			ActingCoord.X, ActingCoord.Y,
			AllyCoord.X, AllyCoord.Y
		);

		// Prefer the most dangerous fight, with a mild bias toward helpers that can arrive sooner.
		const float CandidateScore = NeedScore - static_cast<float>(Distance) * 55.0f;
		if (CandidateScore > BestScore)
		{
			BestScore = CandidateScore;
			BestTarget = Ally;
		}
	}

	if (OutNeedScore && IsValid(BestTarget))
	{
		*OutNeedScore = ScoreEnemyBotPreemptiveSupportNeed(BestTarget, nullptr, nullptr);
	}

	return BestTarget;
}

AHexUnitActor* AHexGridActor::FindMostThreatenedEnemyAlly(AHexUnitActor* ActingUnit, int32* OutThreatDamage) const
{
	if (OutThreatDamage)
	{
		*OutThreatDamage = 0;
	}

	AHexUnitActor* BestAlly = nullptr;
	float BestScore = -1000000000.0f;
	int32 BestThreatDamage = 0;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == ActingUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		int32 DirectThreatDamage = 0;
		const bool bDirectlyThreatened = IsEnemyBotUnitDirectlyThreatened(Ally, &DirectThreatDamage);
		if (!bDirectlyThreatened || DirectThreatDamage <= 0)
		{
			continue;
		}

		const float HealthPercent = Ally->MaxHealth > 0
			? static_cast<float>(Ally->CurrentHealth) / static_cast<float>(Ally->MaxHealth)
			: 1.0f;

		float Score = static_cast<float>(DirectThreatDamage) * 4.0f + (1.0f - HealthPercent) * 420.0f;
		if (Ally->UnitType == EHexUnitType::Ram)
		{
			Score += 180.0f;
		}
		else if (Ally->UnitType == EHexUnitType::Champion)
		{
			Score += 220.0f;
		}
		else if ((Ally->UnitType == EHexUnitType::Support || Ally->UnitType == EHexUnitType::Healer))
		{
			Score += 150.0f;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestAlly = Ally;
			BestThreatDamage = DirectThreatDamage;
		}
	}

	if (OutThreatDamage)
	{
		*OutThreatDamage = BestThreatDamage;
	}

	return BestAlly;
}

bool AHexGridActor::IsEnemyBotUnitDirectlyThreatened(AHexUnitActor* EnemyUnit, int32* OutThreatDamage) const
{
	if (OutThreatDamage)
	{
		*OutThreatDamage = 0;
	}

	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	int32 ThreatDamage = 0;
	const FIntPoint Coord = EnemyUnit->GetGridCoord();
	const bool bThreatened = IsCellThreatenedByPlayerUnits(Coord.X, Coord.Y, ThreatDamage);

	if (OutThreatDamage)
	{
		*OutThreatDamage = ThreatDamage;
	}

	return bThreatened;
}

bool AHexGridActor::ShouldEnemyBotUseLastStand(AHexUnitActor* Champion) const
{
	if (!IsValid(Champion) || Champion->GetIsDead() || Champion->Team != EHexUnitTeam::Enemy || !Champion->IsChampionAbilityLastStand())
	{
		return false;
	}

	TArray<int32> DirectAttackDamages;
	TArray<int32> FutureAttackDamages;
	const FIntPoint ChampionCoord = Champion->GetGridCoord();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(PlayerCoord.X, PlayerCoord.Y, ChampionCoord.X, ChampionCoord.Y);
		const int32 Damage = FMath::Max(0, PlayerUnit->AttackDamage);
		if (Damage <= 0)
		{
			continue;
		}

		if (Distance > 0 && Distance <= FMath::Max(1, PlayerUnit->AttackRange))
		{
			DirectAttackDamages.Add(Damage);
		}

		const int32 FutureReach = FMath::Max(0, PlayerUnit->MovementRange) + FMath::Max(1, PlayerUnit->AttackRange);
		if (Distance > 0 && Distance <= FutureReach)
		{
			FutureAttackDamages.Add(Damage);
		}
	}

	DirectAttackDamages.Sort([](const int32 Left, const int32 Right) { return Left > Right; });
	FutureAttackDamages.Sort([](const int32 Left, const int32 Right) { return Left > Right; });

	const int32 AttackCost = FMath::Max(0, CalculateAttackActionPointCost());
	const int32 MaxAttacksNextPlayerTurn = AttackCost <= 0
		? FutureAttackDamages.Num()
		: FMath::Max(1, MaxActionPoints / FMath::Max(1, AttackCost));

	auto SumBestAttacks = [MaxAttacksNextPlayerTurn](const TArray<int32>& Damages) -> int32
		{
			int32 TotalDamage = 0;
			const int32 Count = FMath::Min(MaxAttacksNextPlayerTurn, Damages.Num());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				TotalDamage += Damages[Index];
			}
			return TotalDamage;
		};

	const int32 DirectDamage = SumBestAttacks(DirectAttackDamages);
	if (DirectDamage >= Champion->CurrentHealth)
	{
		return true;
	}

	const int32 FutureDamage = SumBestAttacks(FutureAttackDamages);
	const int32 FutureSafetyDamage = FMath::Max(
		1,
		FMath::CeilToInt(static_cast<float>(FMath::Max(1, Champion->MaxHealth)) * FMath::Clamp(EnemyBotLastStandFutureDamageSafetyPercent, 0.0f, 1.0f))
	);

	return FutureDamage >= Champion->CurrentHealth + FutureSafetyDamage;
}

bool AHexGridActor::WillEnemyBotTargetTakeDamageThisOrNextTurn(
	AHexUnitActor* Target,
	AHexUnitActor* MarkingChampion
) const
{
	if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
	{
		return false;
	}

	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 AttackCost = FMath::Max(0, CalculateAttackActionPointCost());

	// Marked for Death is applied before the follow-up attack, so reserve its AP cost
	// when checking whether damage can still happen THIS enemy turn.
	const int32 AbilityCost = IsValid(MarkingChampion)
		? FMath::Max(0, CalculateChampionAbilityActionPointCost(MarkingChampion))
		: 0;
	const int32 APAfterMark = FMath::Max(0, CurrentActionPoints - AbilityCost);

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Attacker = Pair.Value;
		if (!IsValid(Attacker) || Attacker->GetIsDead() || Attacker->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint AttackerCoord = Attacker->GetGridCoord();
		const int32 Distance = GetHexDistance(
			AttackerCoord.X, AttackerCoord.Y,
			TargetCoord.X, TargetCoord.Y
		);
		if (Distance <= 0)
		{
			continue;
		}

		const int32 AttackRange = FMath::Max(1, Attacker->AttackRange);

		// THIS TURN:
		// There must still be AP after the mark and the attacker must not have already
		// spent its one normal attack for this turn.
		if (Distance <= AttackRange &&
			CanUnitAttackThisTurn(Attacker) &&
			APAfterMark >= AttackCost)
		{
			return true;
		}

		// NEXT ENEMY TURN:
		// Require a realistic move + attack package from a fresh AP pool.
		// The attacker has to be able to close the missing distance within MovementRange,
		// and that movement plus one attack must fit into MaxActionPoints.
		const int32 RequiredMoveHexes = FMath::Max(0, Distance - AttackRange);
		if (RequiredMoveHexes <= FMath::Max(0, Attacker->MovementRange))
		{
			const int32 RequiredMoveAP = CalculateMoveActionPointCost(RequiredMoveHexes);
			if (RequiredMoveAP + AttackCost <= FMath::Max(0, MaxActionPoints))
			{
				return true;
			}
		}
	}

	return false;
}

AHexUnitActor* AHexGridActor::FindBestEnemyBotMarkedForDeathTarget(AHexUnitActor* Champion) const
{
	if (!IsValid(Champion) || Champion->GetIsDead() || Champion->Team != EHexUnitTeam::Enemy || !Champion->IsChampionAbilityMarkedForDeath())
	{
		return nullptr;
	}

	const FIntPoint ChampionCoord = Champion->GetGridCoord();
	const int32 AbilityRange = FMath::Max(1, Champion->ChampionAbilityRange);
	AHexUnitActor* BestTarget = nullptr;
	float BestScore = -1000000000.0f;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Target = Pair.Value;
		if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint TargetCoord = Target->GetGridCoord();
		const int32 Distance = GetHexDistance(ChampionCoord.X, ChampionCoord.Y, TargetCoord.X, TargetCoord.Y);
		if (Distance <= 0 || Distance > AbilityRange)
		{
			continue;
		}

		// Ringleader/Marked for Death must create value immediately.
		// Do not spend the ability if nobody can damage this target during the
		// current enemy turn after paying the ability AP, or during the next enemy turn.
		if (!WillEnemyBotTargetTakeDamageThisOrNextTurn(Target, Champion))
		{
			continue;
		}

		const float HealthPercent = Target->MaxHealth > 0
			? static_cast<float>(Target->CurrentHealth) / static_cast<float>(Target->MaxHealth)
			: 1.0f;

		float Score = GetEnemyBotTargetBaseValue(Target);
		Score += (static_cast<float>(FMath::Max(0, Target->AttackDamage)) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * 4.0f;
		Score += (1.0f - HealthPercent) * 280.0f;
		Score += ScoreEnemyBotTargetPressure(Champion, Target) * 0.35f;

		if (Target->UnitType == EHexUnitType::Champion || (Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer))
		{
			Score += 180.0f;
		}

		if (EnemyBotPlannedFocusTarget.Get() == Target)
		{
			Score += EnemyBotPlanCommitmentScore * 1.5f;
		}

		// Refreshing a nearly expired mark can be useful; overwriting a fresh one is usually wasteful.
		if (Target->IsMarkedForDeathActive())
		{
			Score -= Target->RemainingMarkedForDeathTurns > 1 ? 1400.0f : 350.0f;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Target;
		}
	}

	return BestTarget;
}

float AHexGridActor::ScoreEnemyBotSummonCell(AHexUnitActor* Summoner, const FIntPoint& CellCoord) const
{
	if (!IsValid(Summoner) || Summoner->GetIsDead() || !Summoner->CanSummon() || !Summoner->SummonedUnitClass)
	{
		return -1000000000.0f;
	}

	const AHexUnitActor* SummonedDefault = Summoner->SummonedUnitClass->GetDefaultObject<AHexUnitActor>();
	if (!IsValid(SummonedDefault) || !SummonedDefault->bIsSummonedUnit || !HasCell(CellCoord.X, CellCoord.Y) || IsCellOccupied(CellCoord.X, CellCoord.Y))
	{
		return -1000000000.0f;
	}

	const int32 SummonMoveRange = FMath::Max(0, SummonedDefault->MovementRange);
	const int32 SummonAttackRange = FMath::Max(1, SummonedDefault->AttackRange);
	const int32 SummonDamage = FMath::Max(0, SummonedDefault->GetScaledAttackDamageForLevel(1));
	const int32 LifetimeTurns = FMath::Max(1, SummonedDefault->SummonedUnitLifetimeTurns);
	const int32 SummonCost = CalculateChampionAbilityActionPointCost(Summoner);
	const int32 AttackCost = CalculateAttackActionPointCost();
	const int32 RemainingAPAfterSummon = FMath::Max(0, CurrentActionPoints - SummonCost);

	// A short-lived summon is useless if there are no points left for its guaranteed attack.
	if (AttackCost > 0 && RemainingAPAfterSummon < AttackCost)
	{
		return -1000000000.0f;
	}

	float BestAttackUtility = -1000000000.0f;
	TArray<AHexUnitActor*> PlayerUnits;
	CollectAlivePlayerUnits(PlayerUnits);

	for (AHexUnitActor* Target : PlayerUnits)
	{
		if (!IsValid(Target) || Target->GetIsDead())
		{
			continue;
		}

		const FIntPoint TargetCoord = Target->GetGridCoord();
		const int32 DirectDistance = GetHexDistance(CellCoord.X, CellCoord.Y, TargetCoord.X, TargetCoord.Y);
		const bool bCanAttackImmediately = DirectDistance > 0 && DirectDistance <= SummonAttackRange;

		if (bEnemyBotSummonRequiresImmediateAttack && !bCanAttackImmediately)
		{
			continue;
		}

		int32 MinimumPathLength = bCanAttackImmediately ? 0 : MAX_int32;
		if (!bCanAttackImmediately)
		{
			for (const FHexCell& Cell : Cells)
			{
				const FIntPoint AttackCell(Cell.Q, Cell.R);
				const int32 DistanceToTarget = GetHexDistance(AttackCell.X, AttackCell.Y, TargetCoord.X, TargetCoord.Y);
				if (DistanceToTarget <= 0 || DistanceToTarget > SummonAttackRange)
				{
					continue;
				}

				if (AttackCell != CellCoord && IsCellOccupied(AttackCell.X, AttackCell.Y))
				{
					continue;
				}

				TArray<FHexCoord> Path;
				if (FindPath(CellCoord.X, CellCoord.Y, AttackCell.X, AttackCell.Y, Path))
				{
					MinimumPathLength = FMath::Min(MinimumPathLength, Path.Num());
				}
			}
		}

		if (MinimumPathLength == MAX_int32)
		{
			continue;
		}

		const int32 TurnsToAttack = MinimumPathLength <= 0
			? 0
			: (SummonMoveRange > 0 ? ((MinimumPathLength + SummonMoveRange - 1) / SummonMoveRange) : MAX_int32);

		// Keep one full owner turn in reserve. Reaching attack range exactly when the lifetime expires is not useful.
		if (TurnsToAttack == MAX_int32 || TurnsToAttack >= LifetimeTurns)
		{
			continue;
		}

		const int32 AttackWindows = bEnemyBotSummonRequiresImmediateAttack
			? 1
			: FMath::Max(1, LifetimeTurns - TurnsToAttack);
		const int32 ExpectedDamage = FMath::Min(Target->CurrentHealth, SummonDamage * AttackWindows);
		const float DamageFraction = Target->CurrentHealth > 0
			? static_cast<float>(ExpectedDamage) / static_cast<float>(Target->CurrentHealth)
			: 0.0f;

		const float MinimumDamageFraction = FMath::Clamp(EnemyBotSummonMinimumDamageFraction, 0.0f, 1.0f);
		const bool bCanKillWithFirstAttack = SummonDamage >= Target->CurrentHealth;
		if (!bCanKillWithFirstAttack && DamageFraction < MinimumDamageFraction)
		{
			continue;
		}

		// Do not spend a champion ability on a target that existing attackers already finish.
		const int32 ExistingDamageOnTarget = GetPotentialEnemyBotDamageToPlayerTarget(Target, 0, Summoner);
		if (!bCanKillWithFirstAttack && ExistingDamageOnTarget >= Target->CurrentHealth)
		{
			continue;
		}

		float Utility = GetEnemyBotTargetBaseValue(Target) * 0.45f;
		Utility += static_cast<float>(ExpectedDamage) * 10.0f;
		Utility += DamageFraction * 450.0f;
		Utility -= static_cast<float>(TurnsToAttack) * 260.0f;

		if (bCanKillWithFirstAttack)
		{
			Utility += 760.0f;
		}

		if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer) || Target->UnitType == EHexUnitType::Champion)
		{
			Utility += 180.0f;
		}

		if (bCanAttackImmediately)
		{
			Utility += 400.0f;
		}

		BestAttackUtility = FMath::Max(BestAttackUtility, Utility);
	}

	if (BestAttackUtility <= -999999000.0f)
	{
		return BestAttackUtility;
	}

	float TacticalUtility = 0.0f;
	TacticalUtility += static_cast<float>(GetEnemyAllyCountNearCell(CellCoord, 2, Summoner)) * 35.0f;

	int32 FutureThreatDamage = 0;
	if (IsCellThreatenedByPlayerUnitsAfterMove(CellCoord.X, CellCoord.Y, FutureThreatDamage))
	{
		// The summon is disposable, but it must still trade damage efficiently.
		TacticalUtility -= 30.0f + static_cast<float>(FutureThreatDamage) * 0.20f;
	}

	return BestAttackUtility + TacticalUtility;
}

bool AHexGridActor::GetUsefulEnemyBotSummonCells(AHexUnitActor* Summoner, TArray<FIntPoint>& OutCells, float* OutBestUtility) const
{
	OutCells.Empty();
	if (OutBestUtility)
	{
		*OutBestUtility = -1000000000.0f;
	}

	if (!IsValid(Summoner) || !Summoner->CanSummon())
	{
		return false;
	}

	TArray<FIntPoint> CandidateCells;
	GetSummonCandidateCells(Summoner, CandidateCells);
	if (CandidateCells.IsEmpty())
	{
		return false;
	}

	struct FScoredSummonCell
	{
		FIntPoint Coord = FIntPoint(0, 0);
		float Utility = -1000000000.0f;
	};

	TArray<FScoredSummonCell> ScoredCells;
	for (const FIntPoint& CandidateCell : CandidateCells)
	{
		const float Utility = ScoreEnemyBotSummonCell(Summoner, CandidateCell);
		if (Utility < EnemyBotSummonMinimumUtilityScore)
		{
			continue;
		}

		FScoredSummonCell& NewCell = ScoredCells.AddDefaulted_GetRef();
		NewCell.Coord = CandidateCell;
		NewCell.Utility = Utility;
	}

	ScoredCells.Sort([](const FScoredSummonCell& Left, const FScoredSummonCell& Right)
		{
			return Left.Utility > Right.Utility;
		});

	const int32 ConfiguredTargetCount = FMath::Max(1, Summoner->GetSummonUnitCount());
	const int32 AttackCost = FMath::Max(0, CalculateAttackActionPointCost());
	const int32 RemainingAPAfterSummon = FMath::Max(0, CurrentActionPoints - CalculateChampionAbilityActionPointCost(Summoner));
	const int32 MaxSummonsThatCanAttackNow = AttackCost <= 0
		? ConfiguredTargetCount
		: RemainingAPAfterSummon / FMath::Max(1, AttackCost);
	const int32 TargetCount = FMath::Min(ConfiguredTargetCount, MaxSummonsThatCanAttackNow);
	if (TargetCount <= 0)
	{
		return false;
	}

	for (int32 Index = 0; Index < ScoredCells.Num() && Index < TargetCount; ++Index)
	{
		OutCells.Add(ScoredCells[Index].Coord);
	}

	if (OutBestUtility && ScoredCells.Num() > 0)
	{
		*OutBestUtility = ScoredCells[0].Utility;
	}

	return OutCells.Num() > 0;
}

int32 AHexGridActor::GetEnemyBotActionCountThisTurn(AHexUnitActor* EnemyUnit) const
{
	if (!IsValid(EnemyUnit))
	{
		return 0;
	}

	const int32* Count = EnemyBotActionCountThisTurn.Find(EnemyUnit);
	return Count ? FMath::Max(0, *Count) : 0;
}

int32 AHexGridActor::GetEnemyBotUnactedUnitCount(AHexUnitActor* IgnoreUnit) const
{
	int32 Count = 0;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit == IgnoreUnit || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy || !Unit->CanAct())
		{
			continue;
		}

		if (GetEnemyBotActionCountThisTurn(Unit) <= 0)
		{
			++Count;
		}
	}
	return Count;
}


int32 AHexGridActor::GetEnemyBotUnactedFrontlineUnitCount(AHexUnitActor* IgnoreUnit) const
{
	int32 Count = 0;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit == IgnoreUnit || !IsEnemyBotFrontlineUnit(Unit) || !Unit->CanAct())
		{
			continue;
		}

		if (GetEnemyBotActionCountThisTurn(Unit) <= 0)
		{
			++Count;
		}
	}
	return Count;
}

bool AHexGridActor::IsEnemyBotArmyInApproachPhase() const
{
	bool bHasEnemy = false;
	bool bHasPlayer = false;

	for (const TPair<FIntPoint, AHexUnitActor*>& EnemyPair : UnitsByCoord)
	{
		AHexUnitActor* EnemyUnit = EnemyPair.Value;
		if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		bHasEnemy = true;

		const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();
		const int32 ReachThisTurn =
			FMath::Max(0, GetEffectiveMovementRangeForUnit(EnemyUnit)) +
			FMath::Max(1, EnemyUnit->AttackRange);

		for (const TPair<FIntPoint, AHexUnitActor*>& PlayerPair : UnitsByCoord)
		{
			AHexUnitActor* PlayerUnit = PlayerPair.Value;
			if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
			{
				continue;
			}

			bHasPlayer = true;

			const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
			const int32 Distance = GetHexDistance(
				EnemyCoord.X, EnemyCoord.Y,
				PlayerCoord.X, PlayerCoord.Y
			);

			// As soon as one enemy can realistically enter attack range this turn,
			// stop marching and hand control back to the normal tactical planner.
			if (Distance > 0 && Distance <= ReachThisTurn)
			{
				return false;
			}
		}
	}

	return bHasEnemy && bHasPlayer;
}

bool AHexGridActor::IsEnemyBotFrontlineUnit(AHexUnitActor* Unit) const
{
	if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if ((Unit->UnitType == EHexUnitType::Support || Unit->UnitType == EHexUnitType::Healer))
	{
		return false;
	}

	return Unit->UnitType == EHexUnitType::Ram || Unit->AttackRange <= 1;
}

int32 AHexGridActor::GetNearestPlayerDistanceForEnemyFrontline(AHexUnitActor* IgnoreUnit) const
{
	int32 BestDistance = MAX_int32;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit == IgnoreUnit || !IsEnemyBotFrontlineUnit(Unit))
		{
			continue;
		}

		BestDistance = FMath::Min(BestDistance, GetNearestPlayerDistanceFromCell(Unit->GetGridCoord()));
	}
	return BestDistance;
}

bool AHexGridActor::IsEnemyBotBacklineScreenedAtCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return false;
	}

	const int32 CandidatePlayerDistance = GetNearestPlayerDistanceFromCell(CandidateCoord);
	if (CandidatePlayerDistance == MAX_int32)
	{
		return false;
	}

	const int32 ScreenRange = FMath::Max(1, EnemyBotBacklineScreenRange);
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == EnemyUnit || !IsEnemyBotFrontlineUnit(Ally))
		{
			continue;
		}

		const FIntPoint AllyCoord = Ally->GetGridCoord();
		const int32 DistanceToAlly = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, AllyCoord.X, AllyCoord.Y);
		const int32 AllyPlayerDistance = GetNearestPlayerDistanceFromCell(AllyCoord);
		if (DistanceToAlly <= ScreenRange && AllyPlayerDistance <= CandidatePlayerDistance)
		{
			return true;
		}
	}

	return false;
}

bool AHexGridActor::CanEnemyBotAttackAnyPlayerFromCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, bool* OutCanKill) const
{
	if (OutCanKill)
	{
		*OutCanKill = false;
	}

	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return false;
	}

	bool bCanAttack = false;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Target = Pair.Value;
		if (!IsValid(Target) || Target->GetIsDead() || Target->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint TargetCoord = Target->GetGridCoord();
		const int32 Distance = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, TargetCoord.X, TargetCoord.Y);
		if (Distance > 0 && Distance <= FMath::Max(1, EnemyUnit->AttackRange))
		{
			bCanAttack = true;
			if (OutCanKill && Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) >= Target->CurrentHealth)
			{
				*OutCanKill = true;
			}
		}
	}

	return bCanAttack;
}

void AHexGridActor::RecordEnemyBotUnitAction(AHexUnitActor* EnemyUnit)
{
	if (!IsValid(EnemyUnit))
	{
		return;
	}

	EnemyBotActionCountThisTurn.FindOrAdd(EnemyUnit)++;
}

float AHexGridActor::ScoreEnemyBotGroupTactics(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, bool bCanAttackFromCandidate, bool bCanHealFromCandidate) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return 0.0f;
	}

	float Score = 0.0f;
	const FIntPoint CurrentCoord = EnemyUnit->GetGridCoord();
	const int32 AliveEnemyCount = GetAliveUnitCountForTeam(EHexUnitTeam::Enemy);
	const bool bApproachPhase = IsEnemyBotArmyInApproachPhase();

	const int32 AdjacentAllies = GetEnemyAllyCountNearCell(CandidateCoord, 1, EnemyUnit);
	const int32 NearbyAllies = GetEnemyAllyCountNearCell(CandidateCoord, 2, EnemyUnit);

	int32 NearestAllyDistance = MAX_int32;
	int32 FurthestAllyDistance = 0;
	int32 CurrentNearestAllyDistance = MAX_int32;
	int32 CurrentFurthestAllyDistance = 0;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == EnemyUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		const FIntPoint AllyCoord = Ally->GetGridCoord();
		const int32 CandidateDistance = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, AllyCoord.X, AllyCoord.Y);
		const int32 CurrentDistance = GetHexDistance(CurrentCoord.X, CurrentCoord.Y, AllyCoord.X, AllyCoord.Y);
		NearestAllyDistance = FMath::Min(NearestAllyDistance, CandidateDistance);
		FurthestAllyDistance = FMath::Max(FurthestAllyDistance, CandidateDistance);
		CurrentNearestAllyDistance = FMath::Min(CurrentNearestAllyDistance, CurrentDistance);
		CurrentFurthestAllyDistance = FMath::Max(CurrentFurthestAllyDistance, CurrentDistance);
	}

	// Loose formation: ideal nearest ally is ~2 hexes away. This keeps one visual gap
	// between most units while still making the army move as one connected group.
	if (AliveEnemyCount > 1 && NearestAllyDistance != MAX_int32)
	{
		const int32 PreferredSpacing = FMath::Max(1, EnemyBotPreferredFormationSpacing);
		const int32 LinkRange = FMath::Max(PreferredSpacing, EnemyBotFormationLinkRange);

		Score += EnemyBotHealthySpacingScore;
		Score -= static_cast<float>(FMath::Abs(NearestAllyDistance - PreferredSpacing)) * EnemyBotHealthySpacingScore * 0.65f;

		if (AdjacentAllies >= 2)
		{
			Score -= static_cast<float>(AdjacentAllies - 1) * EnemyBotAdjacentCrowdingPenalty;
		}
		else if (AdjacentAllies == 1 && PreferredSpacing >= 2)
		{
			Score -= EnemyBotAdjacentCrowdingPenalty * 0.16f;
		}

		if (NearestAllyDistance > LinkRange && !bCanAttackFromCandidate && !bCanHealFromCandidate)
		{
			Score -= static_cast<float>(NearestAllyDistance - LinkRange) * EnemyBotFormationRepairScore;
		}

		// When the army is already split, strongly reward moves that reconnect the pieces.
		if (CurrentNearestAllyDistance != MAX_int32 && NearestAllyDistance < CurrentNearestAllyDistance)
		{
			Score += static_cast<float>(CurrentNearestAllyDistance - NearestAllyDistance) * EnemyBotFormationRepairScore;
		}

		if (CurrentFurthestAllyDistance > EnemyBotMaxFormationDiameter && FurthestAllyDistance < CurrentFurthestAllyDistance)
		{
			Score += static_cast<float>(CurrentFurthestAllyDistance - FurthestAllyDistance) * EnemyBotFormationRepairScore * 0.70f;
		}
	}

	const FIntPoint EnemyCenter = GetArmyCenterCell(EHexUnitTeam::Enemy);
	const int32 CandidateDistanceToEnemyCenter = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, EnemyCenter.X, EnemyCenter.Y);
	if (CandidateDistanceToEnemyCenter > FMath::Max(3, EnemyBotMaxFormationDiameter / 2 + 1) && !bCanAttackFromCandidate)
	{
		Score -= static_cast<float>(CandidateDistanceToEnemyCenter - (EnemyBotMaxFormationDiameter / 2 + 1)) * EnemyBotFrontlineSpreadPenalty;
	}

	const int32 CurrentNearestPlayerDistance = GetNearestPlayerDistanceFromCell(CurrentCoord);
	const int32 CandidateNearestPlayerDistance = GetNearestPlayerDistanceFromCell(CandidateCoord);
	const bool bBacklineUnit = EnemyUnit->CanHeal() || (EnemyUnit->AttackRange > 1 && EnemyUnit->UnitType != EHexUnitType::Ram);
	const bool bMeaningfulRetreat = IsMeaningfulEnemyBotRetreat(EnemyUnit, CandidateCoord);

	if (CurrentNearestPlayerDistance != MAX_int32 && CandidateNearestPlayerDistance != MAX_int32)
	{
		const int32 AdvanceSteps = CurrentNearestPlayerDistance - CandidateNearestPlayerDistance;
		if (AdvanceSteps > 0)
		{
			Score += static_cast<float>(AdvanceSteps) * EnemyBotArmyAdvanceScore;

			// Before contact, covering ground is the primary objective. This is deliberately
			// much stronger than spacing/lookahead preferences so the army does not spend
			// five turns making tiny cosmetic adjustments on its own half of the map.
			if (bApproachPhase)
			{
				Score += static_cast<float>(AdvanceSteps) * EnemyBotApproachAdvanceScore;
			}
		}
		else if (AdvanceSteps < 0 && !bMeaningfulRetreat)
		{
			Score -= static_cast<float>(-AdvanceSteps) * EnemyBotBackwardMovePenalty;

			if (bApproachPhase)
			{
				Score -= static_cast<float>(-AdvanceSteps) * EnemyBotApproachNoProgressPenalty;
			}
		}
		else if (bApproachPhase && AdvanceSteps == 0 && !bCanAttackFromCandidate && !bCanHealFromCandidate)
		{
			Score -= EnemyBotApproachNoProgressPenalty;
		}
	}

	int32 FrontNearestPlayerDistance = MAX_int32;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!IsValid(Ally) || Ally == EnemyUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		FrontNearestPlayerDistance = FMath::Min(FrontNearestPlayerDistance, GetNearestPlayerDistanceFromCell(Ally->GetGridCoord()));
	}

	if (FrontNearestPlayerDistance != MAX_int32 && CandidateNearestPlayerDistance != MAX_int32)
	{
		const int32 AllowedLag = bBacklineUnit
			? FMath::Max(FMath::Max(1, EnemyBotMaxBacklineLag), EnemyUnit->AttackRange)
			: FMath::Max(1, EnemyBotMaxBacklineLag - 1);

		// Do not let one unit move far ahead of the common line unless it immediately contributes.
		if (CandidateNearestPlayerDistance < FrontNearestPlayerDistance - 1 && !bCanAttackFromCandidate && !bCanHealFromCandidate)
		{
			Score -= static_cast<float>((FrontNearestPlayerDistance - 1) - CandidateNearestPlayerDistance) * EnemyBotFrontlineSpreadPenalty;
		}

		const int32 CurrentLag = CurrentNearestPlayerDistance == MAX_int32 ? 0 : CurrentNearestPlayerDistance - FrontNearestPlayerDistance;
		const int32 CandidateLag = CandidateNearestPlayerDistance - FrontNearestPlayerDistance;
		if (CurrentLag > AllowedLag && CandidateLag < CurrentLag)
		{
			Score += static_cast<float>(CurrentLag - CandidateLag) * EnemyBotLaggingUnitCatchUpScore;
		}

		if (CandidateLag > AllowedLag && !bCanAttackFromCandidate && !bCanHealFromCandidate)
		{
			Score -= static_cast<float>(CandidateLag - AllowedLag) * EnemyBotFrontlineSpreadPenalty * 1.35f;
		}
	}

	if (bBacklineUnit)
	{
		const int32 MeleeFrontDistance = GetNearestPlayerDistanceForEnemyFrontline(EnemyUnit);
		if (MeleeFrontDistance != MAX_int32 && CandidateNearestPlayerDistance != MAX_int32)
		{
			const bool bScreened = IsEnemyBotBacklineScreenedAtCell(EnemyUnit, CandidateCoord);
			if (CandidateNearestPlayerDistance < MeleeFrontDistance)
			{
				Score -= static_cast<float>(MeleeFrontDistance - CandidateNearestPlayerDistance) * EnemyBotBacklineAheadOfFrontPenalty;
			}
			else if (CandidateNearestPlayerDistance == MeleeFrontDistance && !bScreened)
			{
				Score -= EnemyBotBacklineAheadOfFrontPenalty;
			}
			else if (CandidateNearestPlayerDistance > MeleeFrontDistance && bScreened)
			{
				Score += EnemyBotFormationCohesionScore * 0.85f;
			}
		}
	}

	AHexUnitActor* FocusTarget = EnemyBotPlannedFocusTarget.Get();
	if (IsValid(FocusTarget) && !FocusTarget->GetIsDead())
	{
		const FIntPoint FocusCoord = FocusTarget->GetGridCoord();
		const int32 CurrentDistanceToFocus = GetHexDistance(CurrentCoord.X, CurrentCoord.Y, FocusCoord.X, FocusCoord.Y);
		const int32 CandidateDistanceToFocus = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, FocusCoord.X, FocusCoord.Y);

		if (CandidateDistanceToFocus <= FMath::Max(1, EnemyUnit->AttackRange))
		{
			Score += EnemyBotPlanCommitmentScore * 2.0f;
		}
		else if (CandidateDistanceToFocus < CurrentDistanceToFocus)
		{
			Score += static_cast<float>(CurrentDistanceToFocus - CandidateDistanceToFocus) * EnemyBotPlanCommitmentScore * 0.45f;
		}
		else if (CandidateDistanceToFocus > CurrentDistanceToFocus + 1 && !bCanHealFromCandidate)
		{
			Score -= static_cast<float>(CandidateDistanceToFocus - CurrentDistanceToFocus) * EnemyBotPlanCommitmentScore * 0.35f;
		}
	}

	// Pre-emptive reinforcement: only idle/non-engaged units are pulled toward a
	// fight that the forecast says the frontline is likely to lose.
	if (!HasEnemyBotCurrentAttackTarget(EnemyUnit))
	{
		float ReinforcementNeed = 0.0f;
		AHexUnitActor* ReinforcementTarget = FindEnemyBotPreemptiveSupportTarget(EnemyUnit, &ReinforcementNeed);
		if (IsValid(ReinforcementTarget) && ReinforcementNeed > 0.0f)
		{
			const FIntPoint ReinforcementCoord = ReinforcementTarget->GetGridCoord();
			const int32 CurrentDistanceToReinforcement = GetHexDistance(
				CurrentCoord.X, CurrentCoord.Y,
				ReinforcementCoord.X, ReinforcementCoord.Y
			);
			const int32 CandidateDistanceToReinforcement = GetHexDistance(
				CandidateCoord.X, CandidateCoord.Y,
				ReinforcementCoord.X, ReinforcementCoord.Y
			);

			if (CandidateDistanceToReinforcement < CurrentDistanceToReinforcement)
			{
				const int32 ClosedDistance =
					CurrentDistanceToReinforcement - CandidateDistanceToReinforcement;

				Score += static_cast<float>(ClosedDistance) * EnemyBotPreemptiveReinforcementMoveScore;
				Score += ReinforcementNeed * 0.65f;
			}
			else if (CandidateDistanceToReinforcement > CurrentDistanceToReinforcement &&
				!bCanAttackFromCandidate && !bCanHealFromCandidate)
			{
				Score -= static_cast<float>(
					CandidateDistanceToReinforcement - CurrentDistanceToReinforcement
				) * EnemyBotPreemptiveReinforcementMoveScore * 0.70f;
			}

			if (CandidateDistanceToReinforcement <= FMath::Max(1, EnemyBotReinforcementArrivalRange))
			{
				Score += EnemyBotPreemptiveReinforcementArrivalScore;
			}
		}
	}

	int32 ThreatenedAllyDamage = 0;
	AHexUnitActor* ThreatenedAlly = FindMostThreatenedEnemyAlly(EnemyUnit, &ThreatenedAllyDamage);
	if (IsValid(ThreatenedAlly))
	{
		const FIntPoint AllyCoord = ThreatenedAlly->GetGridCoord();
		const int32 CurrentDistanceToAlly = GetHexDistance(CurrentCoord.X, CurrentCoord.Y, AllyCoord.X, AllyCoord.Y);
		const int32 CandidateDistanceToAlly = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, AllyCoord.X, AllyCoord.Y);

		if (CandidateDistanceToAlly < CurrentDistanceToAlly)
		{
			Score += static_cast<float>(CurrentDistanceToAlly - CandidateDistanceToAlly) * EnemyBotAssistAllyScore * 1.35f;
		}
		else if (CandidateDistanceToAlly > CurrentDistanceToAlly && !bCanAttackFromCandidate && !bCanHealFromCandidate)
		{
			Score -= static_cast<float>(CandidateDistanceToAlly - CurrentDistanceToAlly) * EnemyBotAssistAllyScore * 0.55f;
		}

		if (CandidateDistanceToAlly <= 2)
		{
			Score += EnemyBotAssistAllyScore + static_cast<float>(ThreatenedAllyDamage) * 2.0f;
		}
	}

	if (EnemyUnit->AttackRange > 1 || EnemyUnit->CanHeal())
	{
		const int32 DistanceToRam = GetNearestAliveEnemyRamDistanceFromCell(CandidateCoord, EnemyUnit);
		if (DistanceToRam != MAX_int32 && DistanceToRam >= 1 && DistanceToRam <= 3)
		{
			Score += 120.0f;
		}
	}

	Score += ScoreEnemyBotBacklineScreen(EnemyUnit, CandidateCoord);

	if (IsEnemyBotCellBlockingImportantAlly(EnemyUnit, CandidateCoord))
	{
		Score -= EnemyBotBlockingAllyPenalty;
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotStrategicStateForUnit(AHexUnitActor* EnemyUnit, const FIntPoint& VirtualCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return 0.0f;
	}

	float Score = 0.0f;

	AHexUnitActor* FocusTarget = EnemyBotPlannedFocusTarget.Get();
	if (IsValid(FocusTarget) && !FocusTarget->GetIsDead())
	{
		const FIntPoint FocusCoord = FocusTarget->GetGridCoord();
		const int32 DistanceToFocus = GetHexDistance(VirtualCoord.X, VirtualCoord.Y, FocusCoord.X, FocusCoord.Y);
		Score += 260.0f - static_cast<float>(DistanceToFocus) * 34.0f;
		if (DistanceToFocus > 0 && DistanceToFocus <= EnemyUnit->AttackRange)
		{
			Score += EnemyBotAttackOpportunityBonus * 0.50f;
		}
	}

	const int32 VirtualAdjacentAllies = GetEnemyAllyCountNearCell(VirtualCoord, 1, EnemyUnit);
	const int32 VirtualNearbyAllies = GetEnemyAllyCountNearCell(VirtualCoord, 2, EnemyUnit);
	Score += static_cast<float>(FMath::Min(VirtualNearbyAllies, 2)) * EnemyBotFormationCohesionScore * 0.65f;
	if (VirtualAdjacentAllies >= 2)
	{
		Score -= static_cast<float>(VirtualAdjacentAllies - 1) * EnemyBotAdjacentCrowdingPenalty * 0.55f;
	}

	int32 DirectThreatDamage = 0;
	if (IsCellThreatenedByPlayerUnits(VirtualCoord.X, VirtualCoord.Y, DirectThreatDamage))
	{
		float ThreatScale = 1.0f;
		if (EnemyUnit->UnitType == EHexUnitType::Ram)
		{
			ThreatScale = EnemyBotRamThreatPenaltyMultiplier;
		}
		else if ((EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer) || EnemyUnit->AttackRange > 1)
		{
			ThreatScale = 1.35f;
		}

		Score -= (EnemyBotThreatenedCellPenalty + static_cast<float>(DirectThreatDamage) * 1.2f) * ThreatScale;
	}

	int32 FutureThreatDamage = 0;
	if (IsCellThreatenedByPlayerUnitsAfterMove(VirtualCoord.X, VirtualCoord.Y, FutureThreatDamage))
	{
		Score -= (EnemyBotFutureThreatPenalty + static_cast<float>(FutureThreatDamage) * 0.45f) * GetEnemyBotDifficultyFutureThreatScale();
	}

	return Score;
}

float AHexGridActor::ScoreEnemyBotLookahead(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, int32 Depth) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || Depth <= 1 || EnemyBotLookaheadScoreWeight <= 0.0f)
	{
		return 0.0f;
	}

	float TotalScore = 0.0f;
	float Weight = EnemyBotLookaheadScoreWeight;

	for (int32 Step = 2; Step <= Depth; ++Step)
	{
		TotalScore += ScoreEnemyBotStrategicStateForUnit(EnemyUnit, CandidateCoord) * Weight;
		Weight *= EnemyBotLookaheadScoreWeight;
	}

	return TotalScore;
}

bool AHexGridActor::IsEnemyBotMovePurposeful(AHexUnitActor* EnemyUnit, const FIntPoint& TargetCoord, const TArray<FHexCoord>& Path, float MoveScore) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || Path.Num() <= 0)
	{
		return false;
	}

	const FIntPoint CurrentCoord = EnemyUnit->GetGridCoord();
	if (CurrentCoord == TargetCoord)
	{
		return false;
	}

	if (IsMeaningfulEnemyBotRetreat(EnemyUnit, TargetCoord))
	{
		return true;
	}

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 CandidateDistance = GetHexDistance(TargetCoord.X, TargetCoord.Y, PlayerCoord.X, PlayerCoord.Y);
		if (CandidateDistance > 0 && CandidateDistance <= EnemyUnit->AttackRange)
		{
			return true;
		}
	}

	if (EnemyUnit->CanHeal() && IsValid(FindBestEnemyBotHealTargetFromCell(EnemyUnit, TargetCoord, nullptr)))
	{
		return true;
	}

	int32 FutureThreatDamage = 0;
	if (!EnemyUnit->bIsSummonedUnit && IsCellThreatenedByPlayerUnitsAfterMove(TargetCoord.X, TargetCoord.Y, FutureThreatDamage) && FutureThreatDamage >= EnemyUnit->CurrentHealth)
	{
		// Non-summoned units should not walk into a free kill unless the move creates an immediate attack/heal/retreat purpose.
		return false;
	}

	AHexUnitActor* FocusTarget = EnemyBotPlannedFocusTarget.Get();
	if (IsValid(FocusTarget) && !FocusTarget->GetIsDead())
	{
		const FIntPoint FocusCoord = FocusTarget->GetGridCoord();
		const int32 CurrentDistance = GetHexDistance(CurrentCoord.X, CurrentCoord.Y, FocusCoord.X, FocusCoord.Y);
		const int32 TargetDistance = GetHexDistance(TargetCoord.X, TargetCoord.Y, FocusCoord.X, FocusCoord.Y);
		if (TargetDistance < CurrentDistance)
		{
			int32 AlliedFrontDistance = MAX_int32;
			for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
			{
				AHexUnitActor* Ally = Pair.Value;
				if (!IsValid(Ally) || Ally == EnemyUnit || Ally->GetIsDead() || Ally->Team != EHexUnitTeam::Enemy)
				{
					continue;
				}

				AlliedFrontDistance = FMath::Min(AlliedFrontDistance, GetNearestPlayerDistanceFromCell(Ally->GetGridCoord()));
			}

			const int32 CandidateNearestPlayer = GetNearestPlayerDistanceFromCell(TargetCoord);
			const int32 CandidateNearbyAllies = GetEnemyAllyCountNearCell(TargetCoord, 2, EnemyUnit);
			const bool bKeepsFormation =
				CandidateNearbyAllies > 0 ||
				AlliedFrontDistance == MAX_int32 ||
				(CandidateNearestPlayer != MAX_int32 && CandidateNearestPlayer >= AlliedFrontDistance - 1);

			if (bKeepsFormation)
			{
				return true;
			}
		}
	}

	// Moving an idle unit toward a forecasted losing engagement is a valid tactical
	// purpose even before anybody there has actually lost much HP.
	if (!HasEnemyBotCurrentAttackTarget(EnemyUnit))
	{
		float ReinforcementNeed = 0.0f;
		AHexUnitActor* ReinforcementTarget = FindEnemyBotPreemptiveSupportTarget(EnemyUnit, &ReinforcementNeed);
		if (IsValid(ReinforcementTarget) && ReinforcementNeed > 0.0f)
		{
			const FIntPoint ReinforcementCoord = ReinforcementTarget->GetGridCoord();
			const int32 CurrentDistanceToReinforcement = GetHexDistance(
				CurrentCoord.X, CurrentCoord.Y,
				ReinforcementCoord.X, ReinforcementCoord.Y
			);
			const int32 TargetDistanceToReinforcement = GetHexDistance(
				TargetCoord.X, TargetCoord.Y,
				ReinforcementCoord.X, ReinforcementCoord.Y
			);

			if (TargetDistanceToReinforcement < CurrentDistanceToReinforcement)
			{
				return true;
			}
		}
	}

	AHexUnitActor* ThreatenedAlly = FindMostThreatenedEnemyAlly(EnemyUnit, nullptr);
	if (IsValid(ThreatenedAlly))
	{
		const FIntPoint AllyCoord = ThreatenedAlly->GetGridCoord();
		const int32 CurrentDistance = GetHexDistance(CurrentCoord.X, CurrentCoord.Y, AllyCoord.X, AllyCoord.Y);
		const int32 TargetDistance = GetHexDistance(TargetCoord.X, TargetCoord.Y, AllyCoord.X, AllyCoord.Y);
		if (TargetDistance < CurrentDistance)
		{
			return true;
		}
	}

	const int32 CurrentNearbyAllies = GetEnemyAllyCountNearCell(CurrentCoord, 2, EnemyUnit);
	const int32 TargetNearbyAllies = GetEnemyAllyCountNearCell(TargetCoord, 2, EnemyUnit);
	const int32 TargetAdjacentAllies = GetEnemyAllyCountNearCell(TargetCoord, 1, EnemyUnit);

	// Regroup only if the unit is actually isolated. Merely increasing the ally count is not
	// a tactical purpose: that old rule was the main reason the army compressed into one blob.
	if (CurrentNearbyAllies <= 0 && TargetNearbyAllies > 0 && TargetAdjacentAllies <= 1)
	{
		return true;
	}

	const int32 CurrentNearestPlayerDistance = GetNearestPlayerDistanceFromCell(CurrentCoord);
	const int32 TargetNearestPlayerDistance = GetNearestPlayerDistanceFromCell(TargetCoord);
	if (CurrentNearestPlayerDistance != MAX_int32 && TargetNearestPlayerDistance != MAX_int32 && TargetNearestPlayerDistance < CurrentNearestPlayerDistance)
	{
		// Forward progress is a purpose by itself as long as the unit does not completely abandon the army.
		// It is allowed to go from two nearby allies to one; otherwise a compact formation can never open up.
		if (GetAliveUnitCountForTeam(EHexUnitTeam::Enemy) <= 1 || TargetNearbyAllies > 0)
		{
			return true;
		}

		// Frontline units may establish the new battle line one step ahead of the rest.
		if (IsEnemyBotFrontlineUnit(EnemyUnit) && GetEnemyAllyCountNearCell(TargetCoord, 3, EnemyUnit) > 0)
		{
			return true;
		}
	}

	return MoveScore >= EnemyBotPointlessMoveScoreThreshold;
}

bool AHexGridActor::TryEnemyBotChampionAbility(AHexUnitActor* Champion)
{
	if (!IsValid(Champion) || Champion->GetIsDead() || Champion->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if (!Champion->CanAct() || !Champion->CanUseChampionAbility() || !Champion->CanActivateChampionAbility())
	{
		return false;
	}

	if (!HasEnoughActionPoints(CalculateChampionAbilityActionPointCost(Champion)))
	{
		return false;
	}

	if (Champion->IsChampionAbilitySummon())
	{
		return TryEnemyBotSummon(Champion);
	}

	if (Champion->IsChampionAbilityMarkedForDeath())
	{
		AHexUnitActor* Target = FindBestEnemyBotMarkedForDeathTarget(Champion);
		if (!IsValid(Target))
		{
			UE_LOG(LogTemp, Verbose, TEXT(
				"Enemy bot skipped Marked for Death: no target is expected to take damage this turn or next. Champion=%s"),
				*GetNameSafe(Champion)
			);
			return false;
		}

		if (ExecuteMarkedForDeathChampionAbility(Champion, Target))
		{
			RecordEnemyBotUnitAction(Champion);
			EnemyBotMovesDoneThisTurn++;
			EnemyBotBusyRetriesDone = 0;
			ResetEnemyBotActionOrder();
			ScheduleEnemyBotContinueAfterAction();
			return true;
		}
	}

	if (Champion->IsChampionAbilityLastStand())
	{
		if (!ShouldEnemyBotUseLastStand(Champion))
		{
			return false;
		}

		if (ExecuteLastStandChampionAbility(Champion))
		{
			RecordEnemyBotUnitAction(Champion);
			EnemyBotMovesDoneThisTurn++;
			EnemyBotBusyRetriesDone = 0;
			ResetEnemyBotActionOrder();
			ScheduleEnemyBotContinueAfterAction();
			return true;
		}
	}

	return false;
}

bool AHexGridActor::TryEnemyBotSummon(AHexUnitActor* Summoner)
{
	if (!IsValid(Summoner) || Summoner->GetIsDead() || Summoner->Team != EHexUnitTeam::Enemy || !Summoner->CanSummon())
	{
		return false;
	}

	const int32 UnactedFrontlineUnits = GetEnemyBotUnactedFrontlineUnitCount(Summoner);
	if (UnactedFrontlineUnits > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy bot summon skipped: %d frontline unit(s) still need their first action. Summoner=%s"),
			UnactedFrontlineUnits,
			*GetNameSafe(Summoner)
		);
		return false;
	}

	TArray<FIntPoint> UsefulCells;
	float BestUtility = -1000000000.0f;
	if (!GetUsefulEnemyBotSummonCells(Summoner, UsefulCells, &BestUtility))
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy bot summon skipped: summon has no guaranteed immediate and meaningful attack. Summoner=%s"), *GetNameSafe(Summoner));
		return false;
	}

	if (SpawnSummonedUnitsAtCells(Summoner, UsefulCells))
	{
		RecordEnemyBotUnitAction(Summoner);
		EnemyBotMovesDoneThisTurn++;
		EnemyBotBusyRetriesDone = 0;
		ResetEnemyBotActionOrder();
		ScheduleEnemyBotContinueAfterAction();

		UE_LOG(LogTemp, Log, TEXT("Enemy bot summoned useful units. Summoner=%s Cells=%d BestUtility=%.1f"),
			*GetNameSafe(Summoner),
			UsefulCells.Num(),
			BestUtility
		);
		return true;
	}

	return false;
}

void AHexGridActor::ResetEnemyBotActionOrder()
{
	EnemyBotUnits.Empty();
	EnemyBotUnitIndex = 0;
	RefreshEnemyBotPlan(false);
}

void AHexGridActor::StartBattleMusic()
{
	if (!BattleMusic)
	{
		return;
	}

	if (IsValid(BattleMusicComponent) && BattleMusicComponent->IsPlaying())
	{
		return;
	}

	if (IsValid(BattleMusicComponent))
	{
		BattleMusicComponent->Stop();
		BattleMusicComponent->DestroyComponent();
		BattleMusicComponent = nullptr;
	}

	BattleMusicComponent = UGameplayStatics::CreateSound2D(
		this,
		BattleMusic,
		FMath::Clamp(BattleMusicVolume, 0.0f, 1.0f),
		1.0f,
		0.0f,
		nullptr,
		false,
		false
	);

	if (!BattleMusicComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create battle music component. Grid=%s Music=%s"),
			*GetNameSafe(this),
			*GetNameSafe(BattleMusic)
		);
		return;
	}

	BattleMusicComponent->bIsUISound = true;

	const float SafeVolume = FMath::Clamp(BattleMusicVolume, 0.0f, 1.0f);
	const float SafeFadeInTime = FMath::Max(0.0f, BattleMusicFadeInTime);
	if (SafeFadeInTime > 0.0f)
	{
		BattleMusicComponent->FadeIn(SafeFadeInTime, SafeVolume, 0.0f);
	}
	else
	{
		BattleMusicComponent->Play(0.0f);
	}
}

void AHexGridActor::StopBattleMusic(float FadeOutTime)
{
	if (!IsValid(BattleMusicComponent) || !BattleMusicComponent->IsPlaying())
	{
		return;
	}

	const float SafeFadeOutTime = FMath::Max(0.0f, FadeOutTime);
	if (SafeFadeOutTime > 0.0f)
	{
		BattleMusicComponent->FadeOut(SafeFadeOutTime, 0.0f);
	}
	else
	{
		BattleMusicComponent->Stop();
	}
}

void AHexGridActor::ApplyHexMaterialColors()
{
	if (!HexMeshComponent)
	{
		return;
	}

	// This changes only vector parameters inside the material already assigned to the hex mesh.
	// It does not assign a second material and therefore does not cover the surface texture.
	HexMeshComponent->SetVectorParameterValueOnMaterials(BaseCellColorParameterName, FVector(BaseCellColor.R, BaseCellColor.G, BaseCellColor.B));
	HexMeshComponent->SetVectorParameterValueOnMaterials(NormalHighlightColorParameterName, FVector(NormalHighlightColor.R, NormalHighlightColor.G, NormalHighlightColor.B));
	HexMeshComponent->SetVectorParameterValueOnMaterials(MoveHighlightColorParameterName, FVector(MoveHighlightColor.R, MoveHighlightColor.G, MoveHighlightColor.B));
	HexMeshComponent->SetVectorParameterValueOnMaterials(EnemyHighlightColorParameterName, FVector(EnemyHighlightColor.R, EnemyHighlightColor.G, EnemyHighlightColor.B));
	HexMeshComponent->SetVectorParameterValueOnMaterials(AllyHighlightColorParameterName, FVector(AllyHighlightColor.R, AllyHighlightColor.G, AllyHighlightColor.B));
	HexMeshComponent->SetVectorParameterValueOnMaterials(AttackHighlightColorParameterName, FVector(AttackHighlightColor.R, AttackHighlightColor.G, AttackHighlightColor.B));
	HexMeshComponent->SetVectorParameterValueOnMaterials(AbilityHighlightColorParameterName, FVector(AbilityHighlightColor.R, AbilityHighlightColor.G, AbilityHighlightColor.B));
}

void AHexGridActor::InitializeHexBorderMaterial()
{
	HexBorderMaterialInstance = nullptr;

	if (!HexMeshComponent)
	{
		return;
	}

	const int32 MaterialCount = HexMeshComponent->GetNumMaterials();
	if (MaterialCount <= 0)
	{
		return;
	}

	const int32 SafeFirstSlot = FMath::Clamp(FirstBorderMaterialSlot, 0, MaterialCount - 1);
	const int32 SafeLastSlot = FMath::Clamp(LastBorderMaterialSlot, SafeFirstSlot, MaterialCount - 1);

	HexBorderMaterialInstance = HexMeshComponent->CreateDynamicMaterialInstance(SafeFirstSlot);
	if (!HexBorderMaterialInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create dynamic border material from slot %d."), SafeFirstSlot);
		return;
	}

	// One shared MID is assigned to all six border sections. The centre slot stays untouched.
	for (int32 SlotIndex = SafeFirstSlot; SlotIndex <= SafeLastSlot; ++SlotIndex)
	{
		HexMeshComponent->SetMaterial(SlotIndex, HexBorderMaterialInstance);
	}
}

void AHexGridActor::ApplyHexBorderGlow(bool bEnabled)
{
	if (!HexBorderMaterialInstance)
	{
		return;
	}

	HexBorderMaterialInstance->SetVectorParameterValue(
		GridGlowColorParameterName,
		GridGlowColor
	);
	HexBorderMaterialInstance->SetScalarParameterValue(
		GridGlowIntensityParameterName,
		FMath::Max(0.0f, GridGlowIntensity)
	);
	HexBorderMaterialInstance->SetScalarParameterValue(
		GridGlowEnabledParameterName,
		bEnabled ? 1.0f : 0.0f
	);
}

void AHexGridActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyHexMaterialColors();
	InitializeHexBorderMaterial();
	ApplyHexBorderGlow(false);

	ApplyEnemyBotDifficultyFromLevelOptions();

	if (bAutoStartBattleMusic)
	{
		StartBattleMusic();
	}

	// The real loading screen is owned by UGameLoadingGameInstance/MoviePlayer.
	// HexGrid no longer creates a second viewport WBP loading screen.
	InitializeMatch();
}

void AHexGridActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bMatchFinished)
	{
		return;
	}

	UpdateHoverUnderCursor();
	UpdateBattleTimer(DeltaTime);
	UpdatePlayerTurnTimer(DeltaTime);
	UpdateTurnTimerWarningVisuals();

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return;
	}

	if (IsPlayerInputAllowed() && PlayerController->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		if (HoveredInstanceIndex != INDEX_NONE)
		{
			FHexCoord Coord;
			if (GetCoordByInstanceIndex(HoveredInstanceIndex, Coord))
			{
				UE_LOG(LogTemp, Log, TEXT("Clicked hex: Q=%d R=%d Instance=%d"),
					Coord.Q,
					Coord.R,
					HoveredInstanceIndex
				);

				HandleCellClicked(Coord.Q, Coord.R, HoveredInstanceIndex);
				OnHexCellClicked(Coord.Q, Coord.R, HoveredInstanceIndex);
			}
		}
	}

	if (IsPlayerInputAllowed() && bEnableDebugDamageHotkey && PlayerController->WasInputKeyJustPressed(EKeys::H))
	{
		DamageSelectedOrHoveredUnit();
	}

	if (IsPlayerInputAllowed() && PlayerController->WasInputKeyJustPressed(EKeys::Enter))
	{
		EndPlayerTurn();
	}

	HandleActionHotkeys();

	DrawAttackRangeOutline();
}

void AHexGridActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	GenerateGrid();
	ApplyHexMaterialColors();
}

void AHexGridActor::ApplyBattleInputMode()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return;
	}

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}

void AHexGridActor::InitializeMatch()
{
	if (bMatchRuntimeInitializationInProgress)
	{
		return;
	}

	bMatchRuntimeInitializationInProgress = true;
	bMatchFinished = false;
	PendingMatchResult = EHexMatchResult::None;
	PlayerArmyIndexByUnit.Reset();
	RawBattleExperienceByPlayerArmyIndex.Reset();
	bPlayerSurrenderedForExperience = false;
	bBattleExperienceCommitted = false;
	bBattleCoinsCommitted = false;
	InitialPlayerUnitCountForRewards = 0;
	InitialEnemyUnitCountForRewards = 0;
	LastBattleCoinReward = 0;
	LastBattleExperienceRows.Reset();

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(MatchResultTimerHandle);
	}

	if (ResultScreenWidget)
	{
		ResultScreenWidget->RemoveFromParent();
		ResultScreenWidget = nullptr;
	}

	//      :                                            runtime-      :
	// Cells, CoordToInstance, InstanceToCoord.
	GenerateGrid();

	ApplyBattleInputMode();

	UE_LOG(LogTemp, Warning, TEXT(
		"HexGrid BeginPlay: Cells=%d, DefaultUnitClass=%s, InitialUnits=%d, bSpawnInitialUnit=%s, LegacyInitial=(%d,%d)"
	),
		Cells.Num(),
		*GetNameSafe(UnitClass.Get()),
		InitialUnits.Num(),
		bSpawnInitialUnit ? TEXT("true") : TEXT("false"),
		InitialUnitCoord.Q,
		InitialUnitCoord.R
	);

	bool bSpawnedArmyBuilderArmies = false;
	bool bShouldSpawnLegacyInitialUnits = true;

	if (bUseArmyBuilderDeployment)
	{
		bSpawnedArmyBuilderArmies = TrySpawnArmyBuilderArmies();
		bShouldSpawnLegacyInitialUnits = !bSpawnedArmyBuilderArmies && bAllowInitialUnitsFallbackWithoutSavedArmy;

		if (!bSpawnedArmyBuilderArmies && !bAllowInitialUnitsFallbackWithoutSavedArmy)
		{
			UE_LOG(LogTemp, Warning, TEXT("Army Builder deployment did not spawn units. Legacy InitialUnits fallback is disabled. Save a ready army before opening battle."));
		}
	}

	if (bShouldSpawnLegacyInitialUnits)
	{
		// Legacy/debug fallback: InitialUnits, or old single UnitClass + InitialUnitCoord.
		if (InitialUnits.Num() > 0)
		{
			for (int32 Index = 0; Index < InitialUnits.Num(); ++Index)
			{
				const FHexUnitSpawnInfo& SpawnInfo = InitialUnits[Index];

				if (!SpawnInfo.bEnabled)
				{
					continue;
				}

				TSubclassOf<AHexUnitActor> SpawnClass = SpawnInfo.UnitClass ? SpawnInfo.UnitClass : UnitClass;
				AHexUnitActor* SpawnedUnit = SpawnUnitOfClassAt(SpawnClass, SpawnInfo.Coord.Q, SpawnInfo.Coord.R);

				UE_LOG(LogTemp, Warning, TEXT("InitialUnits[%d] spawn result: %s at Q=%d R=%d Class=%s"),
					Index,
					*GetNameSafe(SpawnedUnit),
					SpawnInfo.Coord.Q,
					SpawnInfo.Coord.R,
					*GetNameSafe(SpawnClass.Get())
				);
			}
		}
		else if (bSpawnInitialUnit)
		{
			AHexUnitActor* SpawnedUnit = SpawnUnitAt(InitialUnitCoord.Q, InitialUnitCoord.R);

			UE_LOG(LogTemp, Warning, TEXT("Legacy initial unit spawn result: %s"),
				*GetNameSafe(SpawnedUnit)
			);
		}
	}

	InitialPlayerUnitCountForRewards = CountAliveNonSummonedUnitsForTeam(EHexUnitTeam::Player);
	InitialEnemyUnitCountForRewards = CountAliveNonSummonedUnitsForTeam(EHexUnitTeam::Enemy);

	UE_LOG(LogTemp, Log, TEXT("Battle reward baseline: PlayerUnits=%d EnemyUnits=%d"),
		InitialPlayerUnitCountForRewards,
		InitialEnemyUnitCountForRewards
	);

	if (UnitsByCoord.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT(
			"BATTLE STARTED WITH ZERO UNITS. bUseArmyBuilderDeployment=%s SavedArmyReady=%s "
			"PlayerDeploymentCoords=%d EnemyDeploymentCoords=%d EnemyPoolOverrides=%d"
		),
			bUseArmyBuilderDeployment ? TEXT("true") : TEXT("false"),
			UArmyBuilderWidget::IsSavedPlayerArmyReadyForBattle() ? TEXT("true") : TEXT("false"),
			PlayerArmyDeploymentCoords.Num(),
			EnemyArmyDeploymentCoords.Num(),
			EnemyArmyPoolUnitClasses.Num()
		);
	}

	CreateActionPointsWidget();
	CreateTurnBannerWidget();
	CreateActionWarningWidget();

	bMatchRuntimeInitializationInProgress = false;
	StartMatchAfterInitialization();
}

FEnemyArmyPowerBand AHexGridActor::GetEnemyArmyPowerBandForCurrentDifficulty() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return WarmUpEnemyArmyPowerBand;

	case EHexBotDifficulty::Challenge:
		return ChallengeEnemyArmyPowerBand;

	case EHexBotDifficulty::Ordeal:
		return OrdealEnemyArmyPowerBand;

	case EHexBotDifficulty::Nightmare:
		return NightmareEnemyArmyPowerBand;

	default:
		return FEnemyArmyPowerBand(0.0f, 1.0f);
	}
}

bool AHexGridActor::BuildEnemyArmyForCurrentDifficulty(
	const TArray<TSubclassOf<AHexUnitActor>>& EnemyPoolClasses,
	int32 UnitCount,
	const TArray<TSubclassOf<AHexUnitActor>>& PlayerArmyClasses,
	TArray<TSubclassOf<AHexUnitActor>>& OutEnemyArmyClasses,
	int32& OutEnemyArmyPower
) const
{
	OutEnemyArmyClasses.Reset();
	OutEnemyArmyPower = 0;

	const int32 SafeUnitCount = FMath::Clamp(UnitCount, 3, 5);
	const int32 SafeEnemyLevel = FMath::Clamp(EnemyArmyLevel, 1, AHexUnitActor::GetMaxProgressionLevel());

	struct FPowerCandidate
	{
		TSubclassOf<AHexUnitActor> UnitClass;
		int32 Power = 0;
		bool bChampion = false;
	};

	TArray<FPowerCandidate> Candidates;
	Candidates.Reserve(EnemyPoolClasses.Num());

	int32 MaxUnitPower = 0;
	for (const TSubclassOf<AHexUnitActor>& CandidateClass : EnemyPoolClasses)
	{
		const AHexUnitActor* DefaultUnit = CandidateClass ? CandidateClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (!DefaultUnit)
		{
			continue;
		}

		FPowerCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.UnitClass = CandidateClass;
		Candidate.Power = FMath::Max(0, DefaultUnit->GetArmyPowerValueForLevel(SafeEnemyLevel));
		Candidate.bChampion = DefaultUnit->UnitType == EHexUnitType::Champion;
		MaxUnitPower = FMath::Max(MaxUnitPower, Candidate.Power);
	}

	if (Candidates.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Enemy power selection failed: enemy pool has no valid classes."));
		return false;
	}

	if (bRandomizeEnemyArmyOrder)
	{
		Algo::RandomShuffle(Candidates);
	}

	// Dynamic programming over: selected unit count, total power and champion count.
	// This guarantees that a reachable composition inside the requested power band is found,
	// while still allowing duplicate non-Champions and never allowing more than one Champion.
	struct FPowerState
	{
		int32 PreviousKey = INDEX_NONE;
		int32 UnitClassIndex = INDEX_NONE;
	};

	const int32 MaxTotalPower = FMath::Max(0, MaxUnitPower * SafeUnitCount);
	const int32 KeyStride = MaxTotalPower + 1;

	auto MakeStateKey = [KeyStride](int32 Power, int32 ChampionCount) -> int32
		{
			return ChampionCount * KeyStride + Power;
		};

	auto DecodeStatePower = [KeyStride](int32 Key) -> int32
		{
			return Key % KeyStride;
		};

	auto DecodeChampionCount = [KeyStride](int32 Key) -> int32
		{
			return Key / KeyStride;
		};

	TArray<TMap<int32, FPowerState>> StatesByUnitCount;
	StatesByUnitCount.SetNum(SafeUnitCount + 1);
	StatesByUnitCount[0].Add(MakeStateKey(0, 0), FPowerState());

	for (int32 SelectedCount = 0; SelectedCount < SafeUnitCount; ++SelectedCount)
	{
		TArray<int32> CurrentKeys;
		StatesByUnitCount[SelectedCount].GetKeys(CurrentKeys);
		Algo::RandomShuffle(CurrentKeys);

		for (const int32 CurrentKey : CurrentKeys)
		{
			const int32 CurrentPower = DecodeStatePower(CurrentKey);
			const int32 CurrentChampionCount = DecodeChampionCount(CurrentKey);

			for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
			{
				const FPowerCandidate& Candidate = Candidates[CandidateIndex];
				const int32 NewChampionCount = CurrentChampionCount + (Candidate.bChampion ? 1 : 0);
				if (NewChampionCount > 1)
				{
					continue;
				}

				const int32 NewPower = CurrentPower + Candidate.Power;
				const int32 NewKey = MakeStateKey(NewPower, NewChampionCount);

				FPowerState NewState;
				NewState.PreviousKey = CurrentKey;
				NewState.UnitClassIndex = CandidateIndex;

				FPowerState* ExistingState = StatesByUnitCount[SelectedCount + 1].Find(NewKey);
				if (!ExistingState)
				{
					StatesByUnitCount[SelectedCount + 1].Add(NewKey, NewState);
				}
				else if (FMath::RandRange(0, 1) == 1)
				{
					// Keep variety between battles when several compositions have the same total power.
					*ExistingState = NewState;
				}
			}
		}
	}

	const TMap<int32, FPowerState>& FinalStates = StatesByUnitCount[SafeUnitCount];
	if (FinalStates.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Enemy power selection failed: no legal %d-unit composition with max one Champion."), SafeUnitCount);
		return false;
	}

	int32 FeasibleMinPower = MAX_int32;
	int32 FeasibleMaxPower = 0;
	TArray<int32> FinalKeys;
	FinalStates.GetKeys(FinalKeys);

	for (const int32 FinalKey : FinalKeys)
	{
		const int32 Power = DecodeStatePower(FinalKey);
		FeasibleMinPower = FMath::Min(FeasibleMinPower, Power);
		FeasibleMaxPower = FMath::Max(FeasibleMaxPower, Power);
	}

	FEnemyArmyPowerBand PowerBand = bUseDifficultyBasedEnemyArmyPower
		? GetEnemyArmyPowerBandForCurrentDifficulty()
		: FEnemyArmyPowerBand(0.0f, 1.0f);

	const float RawMinPercent = FMath::Clamp(PowerBand.MinPowerPercent, 0.0f, 1.0f);
	const float RawMaxPercent = FMath::Clamp(PowerBand.MaxPowerPercent, 0.0f, 1.0f);
	const float MinPercent = FMath::Min(RawMinPercent, RawMaxPercent);
	const float MaxPercent = FMath::Max(RawMinPercent, RawMaxPercent);

	const int32 AutomaticMinPower = FMath::CeilToInt(FMath::Lerp(
		static_cast<float>(FeasibleMinPower),
		static_cast<float>(FeasibleMaxPower),
		MinPercent
	));
	const int32 AutomaticMaxPower = FMath::FloorToInt(FMath::Lerp(
		static_cast<float>(FeasibleMinPower),
		static_cast<float>(FeasibleMaxPower),
		MaxPercent
	));

	const FEnemyArmyAbsolutePowerRange* ExactRange = nullptr;
	switch (SafeUnitCount)
	{
	case 3:
		ExactRange = &PowerBand.ThreeUnits;
		break;
	case 4:
		ExactRange = &PowerBand.FourUnits;
		break;
	case 5:
	default:
		ExactRange = &PowerBand.FiveUnits;
		break;
	}

	int32 RequestedMinPower = ExactRange && ExactRange->MinPower >= 0
		? ExactRange->MinPower
		: AutomaticMinPower;
	int32 RequestedMaxPower = ExactRange && ExactRange->MaxPower >= 0
		? ExactRange->MaxPower
		: AutomaticMaxPower;

	if (RequestedMinPower > RequestedMaxPower)
	{
		Swap(RequestedMinPower, RequestedMaxPower);
	}

	const int32 EffectiveMinPower = FMath::Clamp(RequestedMinPower, FeasibleMinPower, FeasibleMaxPower);
	const int32 EffectiveMaxPower = FMath::Clamp(
		FMath::Max(RequestedMaxPower, EffectiveMinPower),
		EffectiveMinPower,
		FeasibleMaxPower
	);

	auto ReconstructArmy = [&StatesByUnitCount, &Candidates, SafeUnitCount](int32 FinalKey, TArray<TSubclassOf<AHexUnitActor>>& OutArmy) -> bool
		{
			TArray<TSubclassOf<AHexUnitActor>> ReverseArmy;
			ReverseArmy.Reserve(SafeUnitCount);

			int32 CurrentKey = FinalKey;
			for (int32 SelectedCount = SafeUnitCount; SelectedCount > 0; --SelectedCount)
			{
				const FPowerState* State = StatesByUnitCount[SelectedCount].Find(CurrentKey);
				if (!State || !Candidates.IsValidIndex(State->UnitClassIndex))
				{
					return false;
				}

				ReverseArmy.Add(Candidates[State->UnitClassIndex].UnitClass);
				CurrentKey = State->PreviousKey;
			}

			OutArmy.Reset();
			OutArmy.Reserve(ReverseArmy.Num());
			for (int32 Index = ReverseArmy.Num() - 1; Index >= 0; --Index)
			{
				OutArmy.Add(ReverseArmy[Index]);
			}

			return OutArmy.Num() == SafeUnitCount;
		};

	auto AreSameComposition = [](const TArray<TSubclassOf<AHexUnitActor>>& Left, const TArray<TSubclassOf<AHexUnitActor>>& Right) -> bool
		{
			if (Left.Num() != Right.Num())
			{
				return false;
			}

			TMap<UClass*, int32> LeftCounts;
			TMap<UClass*, int32> RightCounts;
			for (const TSubclassOf<AHexUnitActor>& UnitClass : Left)
			{
				if (UClass* RawClass = UnitClass.Get())
				{
					LeftCounts.FindOrAdd(RawClass)++;
				}
			}
			for (const TSubclassOf<AHexUnitActor>& UnitClass : Right)
			{
				if (UClass* RawClass = UnitClass.Get())
				{
					RightCounts.FindOrAdd(RawClass)++;
				}
			}

			if (LeftCounts.Num() != RightCounts.Num())
			{
				return false;
			}

			for (const TPair<UClass*, int32>& Pair : LeftCounts)
			{
				const int32* RightCount = RightCounts.Find(Pair.Key);
				if (!RightCount || *RightCount != Pair.Value)
				{
					return false;
				}
			}

			return true;
		};

	TArray<int32> KeysInsideRange;
	for (const int32 FinalKey : FinalKeys)
	{
		const int32 Power = DecodeStatePower(FinalKey);
		if (Power >= EffectiveMinPower && Power <= EffectiveMaxPower)
		{
			KeysInsideRange.Add(FinalKey);
		}
	}
	Algo::RandomShuffle(KeysInsideRange);

	TArray<TSubclassOf<AHexUnitActor>> SameAsPlayerFallback;
	int32 SameAsPlayerFallbackPower = 0;

	for (const int32 FinalKey : KeysInsideRange)
	{
		TArray<TSubclassOf<AHexUnitActor>> CandidateArmy;
		if (!ReconstructArmy(FinalKey, CandidateArmy))
		{
			continue;
		}

		const int32 CandidatePower = DecodeStatePower(FinalKey);
		if (!AreSameComposition(CandidateArmy, PlayerArmyClasses))
		{
			OutEnemyArmyClasses = MoveTemp(CandidateArmy);
			OutEnemyArmyPower = CandidatePower;
			break;
		}

		if (SameAsPlayerFallback.IsEmpty())
		{
			SameAsPlayerFallback = MoveTemp(CandidateArmy);
			SameAsPlayerFallbackPower = CandidatePower;
		}
	}

	if (OutEnemyArmyClasses.IsEmpty() && !SameAsPlayerFallback.IsEmpty())
	{
		OutEnemyArmyClasses = MoveTemp(SameAsPlayerFallback);
		OutEnemyArmyPower = SameAsPlayerFallbackPower;
	}

	if (OutEnemyArmyClasses.IsEmpty())
	{
		// A broad percentage interval can occasionally contain no exact integer sum.
		// Preserve the requested lower threshold and choose the nearest reachable value above it.
		int32 BestKey = INDEX_NONE;
		int32 BestDistanceAboveMax = MAX_int32;
		bool bBestMatchesPlayer = true;

		for (const int32 FinalKey : FinalKeys)
		{
			const int32 Power = DecodeStatePower(FinalKey);
			if (Power < EffectiveMinPower)
			{
				continue;
			}

			TArray<TSubclassOf<AHexUnitActor>> CandidateArmy;
			if (!ReconstructArmy(FinalKey, CandidateArmy))
			{
				continue;
			}

			const int32 DistanceAboveMax = FMath::Max(0, Power - EffectiveMaxPower);
			const bool bMatchesPlayer = AreSameComposition(CandidateArmy, PlayerArmyClasses);
			if (DistanceAboveMax < BestDistanceAboveMax ||
				(DistanceAboveMax == BestDistanceAboveMax && bBestMatchesPlayer && !bMatchesPlayer))
			{
				BestKey = FinalKey;
				BestDistanceAboveMax = DistanceAboveMax;
				bBestMatchesPlayer = bMatchesPlayer;
				OutEnemyArmyClasses = MoveTemp(CandidateArmy);
				OutEnemyArmyPower = Power;
			}
		}

		if (BestKey == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("Enemy power selection failed: no composition reaches minimum power %d."), EffectiveMinPower);
			return false;
		}

		UE_LOG(LogTemp, Warning, TEXT(
			"Enemy power band contains no exact reachable total. Selected nearest composition above minimum. Requested=%d..%d Selected=%d."
		), EffectiveMinPower, EffectiveMaxPower, OutEnemyArmyPower);
	}

	if (bRandomizeEnemyArmyOrder)
	{
		Algo::RandomShuffle(OutEnemyArmyClasses);
	}

	UE_LOG(LogTemp, Warning, TEXT(
		"Enemy army power selected. Difficulty=%s Units=%d Level=%d Feasible=%d..%d Band=%.2f..%.2f Requested=%d..%d Effective=%d..%d Selected=%d ReachableTotals=%d."
	),
		*UEnum::GetValueAsString(EnemyBotDifficulty),
		SafeUnitCount,
		SafeEnemyLevel,
		FeasibleMinPower,
		FeasibleMaxPower,
		MinPercent,
		MaxPercent,
		RequestedMinPower,
		RequestedMaxPower,
		EffectiveMinPower,
		EffectiveMaxPower,
		OutEnemyArmyPower,
		FinalStates.Num()
	);

	return OutEnemyArmyClasses.Num() == SafeUnitCount;
}

bool AHexGridActor::TrySpawnArmyBuilderArmies()
{
	const UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance());
	const bool bHasGameInstanceSnapshot = LoadingGameInstance && LoadingGameInstance->HasBattleArmySnapshot();

	if (!bHasGameInstanceSnapshot && !UArmyBuilderWidget::HasSavedPlayerArmy())
	{
		UE_LOG(LogTemp, Error, TEXT(
			"Army Builder deployment blocked: both GameInstance battle snapshot and legacy static saved army are empty."
		));
		return false;
	}

	TArray<TSubclassOf<AHexUnitActor>> PlayerArmyClasses = bHasGameInstanceSnapshot
		? LoadingGameInstance->GetBattlePlayerArmyClasses()
		: UArmyBuilderWidget::GetSavedPlayerArmyUnitClasses();
	PlayerArmyClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			return !UnitClass;
		});

	if (PlayerArmyClasses.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Army Builder deployment blocked: saved player army returned zero valid unit classes."));
		return false;
	}

	const TArray<FArmyBuilderUnitProgress> PlayerProgressList = bHasGameInstanceSnapshot
		? LoadingGameInstance->GetBattlePlayerProgress()
		: UArmyBuilderWidget::GetSavedPlayerArmyUnitProgressList();

	if (PlayerArmyClasses.Num() < 3 || PlayerArmyClasses.Num() > 5)
	{
		UE_LOG(LogTemp, Warning, TEXT("Army Builder deployment skipped: player army must contain 3..5 units, current count is %d."), PlayerArmyClasses.Num());
		return false;
	}

	TArray<FHexCoord> PlayerSpawnCoords = PlayerArmyDeploymentCoords;

	// During OpenLevel the GameInstance snapshot is the authoritative state for THIS battle.
	// Never prefer the static ArmyBuilder cache when a snapshot exists: the static cache can
	// still contain a previous same-sized deployment and was the reason different menu
	// formations could enter battle at the same old coordinates.
	TArray<FArmyBuilderDeploymentSlot> SavedDeploymentSlots = bHasGameInstanceSnapshot
		? LoadingGameInstance->GetBattleDeploymentSlots()
		: UArmyBuilderWidget::GetSavedPlayerArmyDeploymentSlots();

	UE_LOG(LogTemp, Warning, TEXT("Battle deployment source=%s Slots=%d PlayerUnits=%d"),
		bHasGameInstanceSnapshot ? TEXT("GameInstanceSnapshot") : TEXT("ArmyBuilderStatic"),
		SavedDeploymentSlots.Num(),
		PlayerArmyClasses.Num());

	for (const FArmyBuilderDeploymentSlot& Slot : SavedDeploymentSlots)
	{
		int32 PhotoColumn = INDEX_NONE;
		int32 PhotoRow = INDEX_NONE;
		FVector2D LegacyPhotoPosition;
		const bool bExactPhotoSlot = DecodeExactPhotoCell(Slot.Q, Slot.R, PhotoColumn, PhotoRow);
		const bool bLegacyPhotoSlot = !bExactPhotoSlot && DecodeLegacyPhotoDeploymentCoord(Slot.Q, Slot.R, LegacyPhotoPosition);
		UE_LOG(LogTemp, Warning, TEXT("Battle deployment slot: UnitIndex=%d Stored=(%d,%d) Source=%s PhotoCell=(%d,%d)"),
			Slot.UnitIndex, Slot.Q, Slot.R,
			bExactPhotoSlot ? TEXT("ExactPhotoCell") : (bLegacyPhotoSlot ? TEXT("LegacyPhotoNormalized") : TEXT("LegacyAxial")),
			PhotoColumn, PhotoRow);
	}
	if (SavedDeploymentSlots.Num() == PlayerArmyClasses.Num())
	{
		TArray<FHexCoord> CustomPlayerSpawnCoords;
		CustomPlayerSpawnCoords.SetNum(PlayerArmyClasses.Num());

		TSet<int32> UsedUnitIndexes;
		TSet<FIntPoint> UsedCoords;
		bool bCustomDeploymentValid = true;

		// v6 exact mapping: build discrete visual rows from the REAL battle cells once,
		// then address them by the same (photo row, photo column) selected in the menu.
		// There is no nearest-cell search for v6 saves. This preserves formation topology.
		struct FVisualBattleCell
		{
			FIntPoint Coord = FIntPoint::ZeroValue;
			FVector Local = FVector::ZeroVector;
		};

		TArray<FVisualBattleCell> SortedCells;
		SortedCells.Reserve(Cells.Num());
		for (const FHexCell& Cell : Cells)
		{
			FVisualBattleCell& VisualCell = SortedCells.AddDefaulted_GetRef();
			VisualCell.Coord = FIntPoint(Cell.Q, Cell.R);
			VisualCell.Local = AxialToLocal(Cell.Q, Cell.R);
		}

		SortedCells.Sort([](const FVisualBattleCell& A, const FVisualBattleCell& B)
		{
			if (!FMath::IsNearlyEqual(A.Local.Y, B.Local.Y, 0.1f))
			{
				return A.Local.Y < B.Local.Y; // top/far -> bottom/near
			}
			return A.Local.X < B.Local.X;
		});

		TArray<TArray<FVisualBattleCell>> VisualRows;
		for (const FVisualBattleCell& Cell : SortedCells)
		{
			if (VisualRows.IsEmpty() || !FMath::IsNearlyEqual(VisualRows.Last()[0].Local.Y, Cell.Local.Y, 0.1f))
			{
				VisualRows.AddDefaulted();
			}
			VisualRows.Last().Add(Cell);
		}
		for (TArray<FVisualBattleCell>& Row : VisualRows)
		{
			Row.Sort([](const FVisualBattleCell& A, const FVisualBattleCell& B)
			{
				return A.Local.X < B.Local.X;
			});
		}

		constexpr int32 PhotoRows = 11;
		constexpr int32 PhotoColumns = 14;
		UE_LOG(LogTemp, Warning, TEXT("Exact deployment topology: PhotoRows=%d BattleVisualRows=%d GridCells=%d"),
			PhotoRows, VisualRows.Num(), Cells.Num());

		// Legacy normalized saves still need bounds for one-time compatibility.
		float MinLocalX = TNumericLimits<float>::Max();
		float MaxLocalX = -TNumericLimits<float>::Max();
		float MinLocalY = TNumericLimits<float>::Max();
		float MaxLocalY = -TNumericLimits<float>::Max();
		for (const FVisualBattleCell& Cell : SortedCells)
		{
			MinLocalX = FMath::Min(MinLocalX, Cell.Local.X);
			MaxLocalX = FMath::Max(MaxLocalX, Cell.Local.X);
			MinLocalY = FMath::Min(MinLocalY, Cell.Local.Y);
			MaxLocalY = FMath::Max(MaxLocalY, Cell.Local.Y);
		}
		const float LocalWidth = FMath::Max(1.0f, MaxLocalX - MinLocalX);
		const float LocalHeight = FMath::Max(1.0f, MaxLocalY - MinLocalY);

		for (const FArmyBuilderDeploymentSlot& Slot : SavedDeploymentSlots)
		{
			if (Slot.UnitIndex < 0 || Slot.UnitIndex >= PlayerArmyClasses.Num() || UsedUnitIndexes.Contains(Slot.UnitIndex))
			{
				bCustomDeploymentValid = false;
				break;
			}

			FIntPoint ResolvedCoord(Slot.Q, Slot.R);
			int32 PhotoColumn = INDEX_NONE;
			int32 PhotoRow = INDEX_NONE;
			FVector2D LegacyPhotoNormalized;

			if (DecodeExactPhotoCell(Slot.Q, Slot.R, PhotoColumn, PhotoRow))
			{
				constexpr int32 PlayerAllowedPhotoColumns = 4;
				if (VisualRows.IsEmpty() || PhotoRow < 0 || PhotoRow >= PhotoRows || PhotoColumn < 0 || PhotoColumn >= PlayerAllowedPhotoColumns)
				{
					bCustomDeploymentValid = false;
					break;
				}

				// If a map uses the same 11-row topology this is literally row-for-row.
				// For a different map shape, keep the semantic vertical rank deterministic.
				const int32 BattleRowIndex = VisualRows.Num() == PhotoRows
					? PhotoRow
					: FMath::Clamp(FMath::RoundToInt(static_cast<float>(PhotoRow) * static_cast<float>(VisualRows.Num() - 1) / static_cast<float>(PhotoRows - 1)), 0, VisualRows.Num() - 1);

				const TArray<FVisualBattleCell>& BattleRow = VisualRows[BattleRowIndex];
				const bool bPhotoShortRow = (PhotoRow % 2) == 0;
				const int32 PhotoCellsThisRow = bPhotoShortRow ? PhotoColumns - 1 : PhotoColumns;
				if (BattleRow.IsEmpty() || PhotoColumn >= PhotoCellsThisRow)
				{
					bCustomDeploymentValid = false;
					break;
				}

				const int32 BattleColumnIndex = BattleRow.Num() == PhotoCellsThisRow
					? PhotoColumn
					: FMath::Clamp(FMath::RoundToInt(static_cast<float>(PhotoColumn) * static_cast<float>(BattleRow.Num() - 1) / static_cast<float>(FMath::Max(1, PhotoCellsThisRow - 1))), 0, BattleRow.Num() - 1);

				ResolvedCoord = BattleRow[BattleColumnIndex].Coord;
				if (UsedCoords.Contains(ResolvedCoord))
				{
					UE_LOG(LogTemp, Warning, TEXT("Exact photo mapping collision: Photo=(%d,%d) -> Grid=(%d,%d)."),
						PhotoColumn, PhotoRow, ResolvedCoord.X, ResolvedCoord.Y);
					bCustomDeploymentValid = false;
					break;
				}

				UE_LOG(LogTemp, Warning, TEXT("Exact photo cell mapped: UnitIndex=%d Photo=(col %d,row %d) -> Battle=(row %d,col %d) -> Grid=(%d,%d)"),
					Slot.UnitIndex, PhotoColumn, PhotoRow, BattleRowIndex, BattleColumnIndex, ResolvedCoord.X, ResolvedCoord.Y);
			}
			else if (DecodeLegacyPhotoDeploymentCoord(Slot.Q, Slot.R, LegacyPhotoNormalized))
			{
				// One-time compatibility for v4/v5 saves. Save a formation again in v6 to remove this path.
				float BestScore = TNumericLimits<float>::Max();
				bool bFoundCell = false;
				for (const FVisualBattleCell& Cell : SortedCells)
				{
					if (UsedCoords.Contains(Cell.Coord))
					{
						continue;
					}
					const float NX = FMath::Clamp((Cell.Local.X - MinLocalX) / LocalWidth, 0.0f, 1.0f);
					const float NY = FMath::Clamp((Cell.Local.Y - MinLocalY) / LocalHeight, 0.0f, 1.0f);
					const float DX = NX - LegacyPhotoNormalized.X;
					const float DY = NY - LegacyPhotoNormalized.Y;
					const float Score = DX * DX + DY * DY;
					if (!bFoundCell || Score < BestScore)
					{
						BestScore = Score;
						ResolvedCoord = Cell.Coord;
						bFoundCell = true;
					}
				}
				if (!bFoundCell)
				{
					bCustomDeploymentValid = false;
					break;
				}
			}
			else if (!HasCell(ResolvedCoord.X, ResolvedCoord.Y) || UsedCoords.Contains(ResolvedCoord))
			{
				bCustomDeploymentValid = false;
				break;
			}

			CustomPlayerSpawnCoords[Slot.UnitIndex] = FHexCoord(ResolvedCoord.X, ResolvedCoord.Y);
			UsedUnitIndexes.Add(Slot.UnitIndex);
			UsedCoords.Add(ResolvedCoord);
		}

		if (bCustomDeploymentValid && UsedUnitIndexes.Num() == PlayerArmyClasses.Num())
		{
			PlayerSpawnCoords = CustomPlayerSpawnCoords;
			UE_LOG(LogTemp, Log, TEXT("Army Builder: using v6 exact photo-cell player deployment."));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Army Builder: photo deployment could not be resolved on this map. Using default PlayerArmyDeploymentCoords."));
		}
	}

	// Deployment data is shared by all battle maps, while maps may have different
	// shapes/crops. Never trust saved/default coordinates blindly: repair the list
	// with real cells from the currently generated grid.
	auto BuildValidSpawnCoords = [this](
		const TArray<FHexCoord>& PreferredCoords,
		int32 DesiredCount,
		bool bPreferLowerQ,
		const TSet<FIntPoint>& ReservedCoords
	) -> TArray<FHexCoord>
		{
			TArray<FHexCoord> Result;
			TSet<FIntPoint> UsedCoords = ReservedCoords;
			const int32 SafeDesiredCount = FMath::Max(0, DesiredCount);
			Result.Reserve(SafeDesiredCount);

			auto TryAddCoord = [this, &Result, &UsedCoords, SafeDesiredCount](int32 Q, int32 R)
				{
					if (Result.Num() >= SafeDesiredCount)
					{
						return;
					}

					const FIntPoint CoordKey(Q, R);
					if (!HasCell(Q, R) || UsedCoords.Contains(CoordKey) || IsCellOccupied(Q, R))
					{
						return;
					}

					Result.Add(FHexCoord(Q, R));
					UsedCoords.Add(CoordKey);
				};

			for (const FHexCoord& PreferredCoord : PreferredCoords)
			{
				TryAddCoord(PreferredCoord.Q, PreferredCoord.R);
			}

			if (Result.Num() >= SafeDesiredCount)
			{
				return Result;
			}

			TArray<FHexCell> SortedCells = Cells;
			SortedCells.Sort([bPreferLowerQ](const FHexCell& Left, const FHexCell& Right)
				{
					if (Left.Q != Right.Q)
					{
						return bPreferLowerQ ? Left.Q < Right.Q : Left.Q > Right.Q;
					}

					const int32 LeftAbsR = FMath::Abs(Left.R);
					const int32 RightAbsR = FMath::Abs(Right.R);
					if (LeftAbsR != RightAbsR)
					{
						return LeftAbsR < RightAbsR;
					}

					return Left.R < Right.R;
				});

			for (const FHexCell& Cell : SortedCells)
			{
				TryAddCoord(Cell.Q, Cell.R);
				if (Result.Num() >= SafeDesiredCount)
				{
					break;
				}
			}

			return Result;
		};

	const TSet<FIntPoint> NoReservedCoords;
	PlayerSpawnCoords = BuildValidSpawnCoords(
		PlayerSpawnCoords,
		PlayerArmyClasses.Num(),
		true,
		NoReservedCoords
	);

	if (PlayerSpawnCoords.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Army Builder deployment blocked: current grid has no valid player spawn cells."));
		return false;
	}

	TArray<TSubclassOf<AHexUnitActor>> EnemyArmyPoolClasses;
	CollectEnemyArmyPoolClasses(EnemyArmyPoolClasses);

	EnemyArmyPoolClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			return !UnitClass;
		});

	if (EnemyArmyPoolClasses.IsEmpty())
	{
		// A missing cached roster must not cancel spawning for both armies. This can
		// happen after Live Coding or when a map is launched with stale BP defaults.
		// Mirroring the saved player classes is a safe last-resort composition.
		EnemyArmyPoolClasses = PlayerArmyClasses;
		UE_LOG(LogTemp, Warning, TEXT("Enemy roster cache is empty. Using saved player army classes as a spawn fallback. Fill AvailableUnitClasses or EnemyArmyPoolUnitClasses to restore random enemies."));
	}

	UE_LOG(LogTemp, Warning, TEXT("Enemy army random pool contains %d unique unit classes."), EnemyArmyPoolClasses.Num());
	for (const TSubclassOf<AHexUnitActor>& PoolClass : EnemyArmyPoolClasses)
	{
		const AHexUnitActor* DefaultUnit = PoolClass ? PoolClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (DefaultUnit)
		{
			UE_LOG(LogTemp, Warning, TEXT("Enemy pool unit: %s, faction=%s"),
				*GetNameSafe(PoolClass.Get()),
				*UEnum::GetValueAsString(DefaultUnit->Faction)
			);
		}
	}

	TSet<FIntPoint> ReservedPlayerCoords;
	for (const FHexCoord& PlayerCoord : PlayerSpawnCoords)
	{
		ReservedPlayerCoords.Add(FIntPoint(PlayerCoord.Q, PlayerCoord.R));
	}

	const int32 RequestedEnemySpawnCount = bUsePlayerArmyUnitCountForRandomEnemyArmy
		? FMath::Clamp(PlayerArmyClasses.Num(), 3, 5)
		: FMath::Clamp(EnemyArmyUnitCount, 3, 5);

	TArray<FHexCoord> EnemySpawnCoords = BuildValidSpawnCoords(
		EnemyArmyDeploymentCoords,
		RequestedEnemySpawnCount,
		false,
		ReservedPlayerCoords
	);

	if (EnemySpawnCoords.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Army Builder deployment blocked: current grid has no valid enemy spawn cells."));
		return false;
	}

	if (bRandomizeEnemyArmyOrder)
	{
		Algo::RandomShuffle(EnemyArmyPoolClasses);
	}

	const int32 PlayerSpawnCount = FMath::Min(PlayerArmyClasses.Num(), PlayerSpawnCoords.Num());
	UE_LOG(LogTemp, Warning, TEXT(
		"Resolved battle spawn cells: Player=%d/%d EnemyCandidates=%d GridCells=%d."
	),
		PlayerSpawnCount,
		PlayerArmyClasses.Num(),
		EnemySpawnCoords.Num(),
		Cells.Num()
	);

	const int32 DesiredEnemyCount = RequestedEnemySpawnCount;
	int32 EnemySpawnCount = FMath::Min(DesiredEnemyCount, EnemySpawnCoords.Num());

	if (EnemySpawnCount < 3)
	{
		UE_LOG(LogTemp, Error, TEXT(
			"Army Builder deployment blocked: enemy army requires 3..5 valid spawn cells, but only %d are available."
		), EnemySpawnCount);
		return false;
	}

	TArray<TSubclassOf<AHexUnitActor>> EnemyArmyClasses;
	int32 SelectedEnemyArmyPower = 0;
	if (!BuildEnemyArmyForCurrentDifficulty(
		EnemyArmyPoolClasses,
		EnemySpawnCount,
		PlayerArmyClasses,
		EnemyArmyClasses,
		SelectedEnemyArmyPower
	))
	{
		UE_LOG(LogTemp, Error, TEXT("Army Builder deployment blocked: failed to build enemy army inside the difficulty power range."));
		return false;
	}

	EnemySpawnCount = EnemyArmyClasses.Num();
	int32 FinalEnemyChampionCount = 0;
	for (const TSubclassOf<AHexUnitActor>& SelectedEnemyClass : EnemyArmyClasses)
	{
		const AHexUnitActor* DefaultUnit = SelectedEnemyClass ? SelectedEnemyClass->GetDefaultObject<AHexUnitActor>() : nullptr;
		if (!DefaultUnit)
		{
			continue;
		}

		if (DefaultUnit->UnitType == EHexUnitType::Champion)
		{
			++FinalEnemyChampionCount;
		}

		UE_LOG(LogTemp, Warning, TEXT("Enemy selected unit: %s, faction=%s, level=%d, power=%d"),
			*GetNameSafe(SelectedEnemyClass.Get()),
			*UEnum::GetValueAsString(DefaultUnit->Faction),
			FMath::Clamp(EnemyArmyLevel, 1, AHexUnitActor::GetMaxProgressionLevel()),
			DefaultUnit->GetArmyPowerValueForLevel(FMath::Clamp(EnemyArmyLevel, 1, AHexUnitActor::GetMaxProgressionLevel()))
		);
	}

	if (FinalEnemyChampionCount > 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Enemy army Champion limit failed after power selection: selected %d Champions."), FinalEnemyChampionCount);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT(
		"Difficulty-based enemy army ready: Units=%d Power=%d Champions=%d/1 Difficulty=%s."
	),
		EnemySpawnCount,
		SelectedEnemyArmyPower,
		FinalEnemyChampionCount,
		*UEnum::GetValueAsString(EnemyBotDifficulty)
	);


	// Build a mirrored enemy deployment zone from the real battlefield topology.
	// Player uses the four leftmost visual columns; enemy uses the four rightmost.
	// The centre is intentionally neutral and cannot contain initial units.
	struct FEnemyFormationCell
	{
		FHexCoord Coord;
		FVector Local = FVector::ZeroVector;
		float FrontRank = 0.0f; // 1 = closest to player/centre, 0 = far back edge.
		float CentreRank = 0.0f; // 1 = near vertical centre of the formation.
	};

	TArray<FEnemyFormationCell> EnemyFormationCandidates;
	{
		struct FVisualCell
		{
			FHexCoord Coord;
			FVector Local = FVector::ZeroVector;
		};

		TArray<FVisualCell> AllVisualCells;
		AllVisualCells.Reserve(Cells.Num());
		for (const FHexCell& Cell : Cells)
		{
			FVisualCell& Entry = AllVisualCells.AddDefaulted_GetRef();
			Entry.Coord = FHexCoord(Cell.Q, Cell.R);
			Entry.Local = AxialToLocal(Cell.Q, Cell.R);
		}

		AllVisualCells.Sort([](const FVisualCell& A, const FVisualCell& B)
		{
			if (!FMath::IsNearlyEqual(A.Local.Y, B.Local.Y, 0.1f))
			{
				return A.Local.Y < B.Local.Y;
			}
			return A.Local.X < B.Local.X;
		});

		TArray<TArray<FVisualCell>> Rows;
		for (const FVisualCell& Cell : AllVisualCells)
		{
			if (Rows.IsEmpty() || !FMath::IsNearlyEqual(Rows.Last()[0].Local.Y, Cell.Local.Y, 0.1f))
			{
				Rows.AddDefaulted();
			}
			Rows.Last().Add(Cell);
		}

		float MinY = TNumericLimits<float>::Max();
		float MaxY = -TNumericLimits<float>::Max();
		for (const FVisualCell& Cell : AllVisualCells)
		{
			MinY = FMath::Min(MinY, Cell.Local.Y);
			MaxY = FMath::Max(MaxY, Cell.Local.Y);
		}
		const float CentreY = (MinY + MaxY) * 0.5f;
		const float HalfHeight = FMath::Max(1.0f, (MaxY - MinY) * 0.5f);
		constexpr int32 EnemyAllowedColumnsPerRow = 4;

		for (TArray<FVisualCell>& Row : Rows)
		{
			Row.Sort([](const FVisualCell& A, const FVisualCell& B)
			{
				return A.Local.X < B.Local.X;
			});

			const int32 FirstEnemyColumn = FMath::Max(0, Row.Num() - EnemyAllowedColumnsPerRow);
			const int32 ZoneWidth = FMath::Max(1, Row.Num() - FirstEnemyColumn - 1);
			for (int32 ColumnIndex = FirstEnemyColumn; ColumnIndex < Row.Num(); ++ColumnIndex)
			{
				const FVisualCell& Cell = Row[ColumnIndex];
				const FIntPoint Key(Cell.Coord.Q, Cell.Coord.R);
				if (ReservedPlayerCoords.Contains(Key))
				{
					continue;
				}

				FEnemyFormationCell& Candidate = EnemyFormationCandidates.AddDefaulted_GetRef();
				Candidate.Coord = Cell.Coord;
				Candidate.Local = Cell.Local;
				// Left edge of the enemy zone faces the player and is therefore the front.
				Candidate.FrontRank = 1.0f - static_cast<float>(ColumnIndex - FirstEnemyColumn) / static_cast<float>(ZoneWidth);
				Candidate.CentreRank = 1.0f - FMath::Clamp(FMath::Abs(Cell.Local.Y - CentreY) / HalfHeight, 0.0f, 1.0f);
			}
		}
	}

	if (EnemyFormationCandidates.Num() >= EnemyArmyClasses.Num())
	{
		TArray<FHexCoord> SmartEnemyCoords;
		SmartEnemyCoords.SetNum(EnemyArmyClasses.Num());
		TSet<FIntPoint> UsedFormationCoords;

		// Place units with the strongest positional requirements first.
		TArray<int32> PlacementOrder;
		for (int32 Index = 0; Index < EnemyArmyClasses.Num(); ++Index)
		{
			PlacementOrder.Add(Index);
		}
		PlacementOrder.Sort([&EnemyArmyClasses](int32 A, int32 B)
		{
			const AHexUnitActor* UA = EnemyArmyClasses[A] ? EnemyArmyClasses[A]->GetDefaultObject<AHexUnitActor>() : nullptr;
			const AHexUnitActor* UB = EnemyArmyClasses[B] ? EnemyArmyClasses[B]->GetDefaultObject<AHexUnitActor>() : nullptr;
			auto Priority = [](const AHexUnitActor* U)
			{
				if (!U) return 0;
				if (U->UnitType == EHexUnitType::Healer) return 50;
				if (U->UnitType == EHexUnitType::Ram) return 45;
				if (U->AttackRange > 1) return 40;
				if (U->UnitType == EHexUnitType::Support) return 35;
				if (U->UnitType == EHexUnitType::Champion) return 30;
				return 20;
			};
			return Priority(UA) > Priority(UB);
		});

		for (const int32 UnitIndex : PlacementOrder)
		{
			const AHexUnitActor* Unit = EnemyArmyClasses[UnitIndex]
				? EnemyArmyClasses[UnitIndex]->GetDefaultObject<AHexUnitActor>()
				: nullptr;

			float BestScore = -TNumericLimits<float>::Max();
			int32 BestCandidateIndex = INDEX_NONE;
			for (int32 CandidateIndex = 0; CandidateIndex < EnemyFormationCandidates.Num(); ++CandidateIndex)
			{
				const FEnemyFormationCell& Candidate = EnemyFormationCandidates[CandidateIndex];
				const FIntPoint Key(Candidate.Coord.Q, Candidate.Coord.R);
				if (UsedFormationCoords.Contains(Key))
				{
					continue;
				}

				float Score = 0.0f;
				if (!Unit)
				{
					Score = Candidate.CentreRank;
				}
				else if (Unit->UnitType == EHexUnitType::Ram)
				{
					Score = Candidate.FrontRank * 5.0f + Candidate.CentreRank * 1.5f;
				}
				else if (Unit->UnitType == EHexUnitType::Healer)
				{
					Score = (1.0f - Candidate.FrontRank) * 5.0f + Candidate.CentreRank * 3.0f;
				}
				else if (Unit->AttackRange > 1)
				{
					Score = (1.0f - Candidate.FrontRank) * 4.0f + Candidate.CentreRank * 1.5f;
				}
				else if (Unit->UnitType == EHexUnitType::Support)
				{
					Score = (1.0f - Candidate.FrontRank) * 3.0f + Candidate.CentreRank * 2.0f;
				}
				else if (Unit->UnitType == EHexUnitType::Champion)
				{
					const float DesiredFront = Unit->AttackRange > 1 ? 0.35f : 0.70f;
					Score = 3.0f - FMath::Abs(Candidate.FrontRank - DesiredFront) * 4.0f + Candidate.CentreRank;
				}
				else
				{
					Score = Candidate.FrontRank * 4.0f + Candidate.CentreRank;
				}

				// Mild spacing bonus prevents the whole army from forming one vertical pile.
				for (const FIntPoint& UsedCoord : UsedFormationCoords)
				{
					const FVector UsedLocal = AxialToLocal(UsedCoord.X, UsedCoord.Y);
					Score += FMath::Clamp(FVector::Dist2D(Candidate.Local, UsedLocal) / FMath::Max(1.0f, HexRadius * 6.0f), 0.0f, 0.35f);
				}

				Score += FMath::FRandRange(0.0f, 0.12f); // small variation between battles.
				if (Score > BestScore)
				{
					BestScore = Score;
					BestCandidateIndex = CandidateIndex;
				}
			}

			if (BestCandidateIndex != INDEX_NONE)
			{
				const FEnemyFormationCell& Chosen = EnemyFormationCandidates[BestCandidateIndex];
				SmartEnemyCoords[UnitIndex] = Chosen.Coord;
				UsedFormationCoords.Add(FIntPoint(Chosen.Coord.Q, Chosen.Coord.R));
				UE_LOG(LogTemp, Log, TEXT("Enemy formation: Unit=%s Type=%s -> Q=%d R=%d Front=%.2f Centre=%.2f"),
					*GetNameSafe(EnemyArmyClasses[UnitIndex].Get()),
					Unit ? *UEnum::GetValueAsString(Unit->UnitType) : TEXT("Unknown"),
					Chosen.Coord.Q, Chosen.Coord.R, Chosen.FrontRank, Chosen.CentreRank);
			}
		}

		bool bAllAssigned = true;
		for (const FHexCoord& Coord : SmartEnemyCoords)
		{
			if (!HasCell(Coord.Q, Coord.R))
			{
				bAllAssigned = false;
				break;
			}
		}
		if (bAllAssigned)
		{
			EnemySpawnCoords = SmartEnemyCoords;
			EnemySpawnCount = EnemyArmyClasses.Num();
			UE_LOG(LogTemp, Log, TEXT("Enemy smart formation applied: %d units in mirrored right deployment zone."), EnemySpawnCount);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy smart formation fallback: only %d mirrored-zone cells for %d units."),
			EnemyFormationCandidates.Num(), EnemyArmyClasses.Num());
	}

	if (PlayerSpawnCount < PlayerArmyClasses.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Army Builder deployment: only %d / %d player units spawned because player deployment coords have not enough cells."),
			PlayerSpawnCount,
			PlayerArmyClasses.Num()
		);
	}

	int32 SpawnedPlayers = 0;
	for (int32 Index = 0; Index < PlayerSpawnCount; ++Index)
	{
		const FHexCoord& Coord = PlayerSpawnCoords[Index];
		AHexUnitActor* SpawnedUnit = SpawnUnitOfClassForTeamAt(PlayerArmyClasses[Index], EHexUnitTeam::Player, Coord.Q, Coord.R);
		if (SpawnedUnit)
		{
			const FArmyBuilderUnitProgress Progress = PlayerProgressList.IsValidIndex(Index)
				? PlayerProgressList[Index]
				: UArmyBuilderWidget::MakeDefaultUnitProgress(PlayerArmyClasses[Index]);
			ApplySavedProgressToSpawnedUnit(SpawnedUnit, Progress.Level, Progress.CurrentExperience);

			const FArmyFactionEffectBonuses FactionBonuses = UArmyBuilderWidget::CalculateFactionEffectBonusesForArmyUnit(
				PlayerArmyClasses,
				PlayerArmyClasses[Index]
			);
			SpawnedUnit->MaxHealth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SpawnedUnit->MaxHealth) * FactionBonuses.MaxHealthMultiplier));
			SpawnedUnit->AttackDamage = FMath::Max(0, FMath::RoundToInt(static_cast<float>(SpawnedUnit->AttackDamage) * FactionBonuses.AttackDamageMultiplier));
			if (SpawnedUnit->bCanHeal)
			{
				SpawnedUnit->HealAmount = FMath::Max(0, FMath::RoundToInt(static_cast<float>(SpawnedUnit->HealAmount) * FactionBonuses.HealAmountMultiplier));
			}
			SpawnedUnit->MovementRange += FMath::Clamp(FactionBonuses.MovementRangeBonus, 0, 1);
			SpawnedUnit->CurrentHealth = SpawnedUnit->MaxHealth;
			SpawnedUnit->UpdateHealthBarWidget();

			RegisterPlayerArmyUnitForProgression(SpawnedUnit, Index);
			++SpawnedPlayers;
		}
	}

	int32 SpawnedEnemies = 0;
	for (int32 Index = 0; Index < EnemySpawnCount; ++Index)
	{
		const FHexCoord& Coord = EnemySpawnCoords[Index];
		const TSubclassOf<AHexUnitActor> EnemyClass = EnemyArmyClasses[Index];

		AHexUnitActor* SpawnedUnit = SpawnUnitOfClassForTeamAt(EnemyClass, EHexUnitTeam::Enemy, Coord.Q, Coord.R);
		if (SpawnedUnit)
		{
			ApplySavedProgressToSpawnedUnit(
				SpawnedUnit,
				FMath::Clamp(EnemyArmyLevel, 1, AHexUnitActor::GetMaxProgressionLevel()),
				0
			);
			++SpawnedEnemies;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Army Builder deployment result: Players=%d/%d Enemies=%d/%d."),
		SpawnedPlayers,
		PlayerSpawnCount,
		SpawnedEnemies,
		EnemySpawnCount
	);

	return SpawnedPlayers > 0 && SpawnedEnemies > 0;
}

void AHexGridActor::CollectEnemyArmyPoolClasses(TArray<TSubclassOf<AHexUnitActor>>& OutEnemyClasses) const
{
	OutEnemyClasses.Reset();

	auto AddEnemyClassIfValid = [&OutEnemyClasses](TSubclassOf<AHexUnitActor> CandidateClass)
		{
			const AHexUnitActor* DefaultUnit = CandidateClass ? CandidateClass->GetDefaultObject<AHexUnitActor>() : nullptr;
			if (!DefaultUnit)
			{
				return;
			}

			OutEnemyClasses.AddUnique(CandidateClass);
		};

	// Important: EnemyArmyPoolUnitClasses is not a hard faction filter.
	// Earlier the function returned right after this array was non-empty, so if this
	// BP array contained only Souls, the enemy could randomly choose only Souls.
	// Now we always append the full Army Builder roster as well, so Kingdom, Animals
	// and Souls are all eligible.
	for (const TSubclassOf<AHexUnitActor>& EnemyClass : EnemyArmyPoolUnitClasses)
	{
		AddEnemyClassIfValid(EnemyClass);
	}

	if (const UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance());
		LoadingGameInstance && LoadingGameInstance->HasBattleArmySnapshot())
	{
		for (const TSubclassOf<AHexUnitActor>& AvailableClass : LoadingGameInstance->GetBattleAvailableUnitClasses())
		{
			AddEnemyClassIfValid(AvailableClass);
		}
	}
	else
	{
		for (const TSubclassOf<AHexUnitActor>& AvailableClass : UArmyBuilderWidget::GetSavedAvailableUnitClasses())
		{
			AddEnemyClassIfValid(AvailableClass);
		}
	}

	if (!OutEnemyClasses.IsEmpty())
	{
		return;
	}

	// Debug fallback only. Real Army Builder battles should use AvailableUnitClasses
	// from WBP_ArmyBuilderWidget. This block is only for direct map tests.
	for (const FHexUnitSpawnInfo& SpawnInfo : InitialUnits)
	{
		if (!SpawnInfo.bEnabled)
		{
			continue;
		}

		TSubclassOf<AHexUnitActor> SpawnClass = SpawnInfo.UnitClass ? SpawnInfo.UnitClass : UnitClass;
		AddEnemyClassIfValid(SpawnClass);
	}

	AddEnemyClassIfValid(UnitClass);
}

void AHexGridActor::StartMatchAfterInitialization()
{
	ApplyBattleInputMode();

	if (bEnableMatchStartSequence)
	{
		StartMatchIntroSequence();
	}
	else
	{
		StartFirstTurn();
	}
}

void AHexGridActor::CreateActionPointsWidget()
{
	if (!ActionPointsWidget && ActionPointsWidgetClass)
	{
		APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PlayerController)
		{
			ActionPointsWidget = CreateWidget<UUserWidget>(PlayerController, ActionPointsWidgetClass);

			if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
			{
				LoadingGameInstance->BindButtonClickSounds(ActionPointsWidget);
			}
			if (ActionPointsWidget)
			{
				ActionPointsWidget->AddToViewport();
			}
		}
	}

	if (ActionPointsWidget)
	{
		ActionPointsWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		if (UButton* EndTurnButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(EndTurnButtonName)))
		{
			EndTurnButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleEndTurnButtonClicked);
			EndTurnButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleEndTurnButtonClicked);
		}

		if (UButton* MoveButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitMoveButtonName)))
		{
			if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
			{
				LoadingGameInstance->UnbindButtonClickSound(MoveButton);
			}

			MoveButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleUnitMoveButtonClicked);
			MoveButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleUnitMoveButtonClicked);
		}

		if (UButton* AttackButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitAttackButtonName)))
		{
			if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
			{
				LoadingGameInstance->UnbindButtonClickSound(AttackButton);
			}

			AttackButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleUnitAttackButtonClicked);
			AttackButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleUnitAttackButtonClicked);
		}

		if (UButton* AbilityButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonName)))
		{
			if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
			{
				LoadingGameInstance->UnbindButtonClickSound(AbilityButton);
			}

			AbilityButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleUnitAbilityButtonClicked);
			AbilityButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleUnitAbilityButtonClicked);

			AbilityButton->OnPressed.RemoveDynamic(this, &AHexGridActor::HandleUnitAbilityButtonPressed);
			AbilityButton->OnPressed.AddDynamic(this, &AHexGridActor::HandleUnitAbilityButtonPressed);

			AbilityButton->OnReleased.RemoveDynamic(this, &AHexGridActor::HandleUnitAbilityButtonReleased);
			AbilityButton->OnReleased.AddDynamic(this, &AHexGridActor::HandleUnitAbilityButtonReleased);

			AbilityButton->OnHovered.RemoveDynamic(this, &AHexGridActor::HandleUnitAbilityButtonHovered);
			AbilityButton->OnHovered.AddDynamic(this, &AHexGridActor::HandleUnitAbilityButtonHovered);

			AbilityButton->OnUnhovered.RemoveDynamic(this, &AHexGridActor::HandleUnitAbilityButtonUnhovered);
			AbilityButton->OnUnhovered.AddDynamic(this, &AHexGridActor::HandleUnitAbilityButtonUnhovered);
		}

		if (UButton* MenuButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(BattleMenuButtonName)))
		{
			MenuButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleBattleMenuButtonClicked);
			MenuButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleBattleMenuButtonClicked);
		}

		if (UButton* SettingsButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(BattleSettingsButtonName)))
		{
			SettingsButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleBattleSettingsButtonClicked);
			SettingsButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleBattleSettingsButtonClicked);
		}

		if (UButton* SurrenderButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(BattleSurrenderButtonName)))
		{
			SurrenderButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleBattleSurrenderButtonClicked);
			SurrenderButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleBattleSurrenderButtonClicked);
		}

		if (UButton* CancelButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(SurrenderCancelButtonName)))
		{
			CancelButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleSurrenderCancelButtonClicked);
			CancelButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleSurrenderCancelButtonClicked);
		}

		if (UButton* ConfirmButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(SurrenderConfirmButtonName)))
		{
			ConfirmButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleSurrenderConfirmButtonClicked);
			ConfirmButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleSurrenderConfirmButtonClicked);
		}

		UpdateBattleMenuWidgetState();
	}

	UpdateActionPointsWidget();
	UpdateSelectedUnitWidget();
}

void AHexGridActor::CreateTurnBannerWidget()
{
	if (!TurnBannerWidget && TurnBannerWidgetClass)
	{
		APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PlayerController)
		{
			TurnBannerWidget = CreateWidget<UUserWidget>(PlayerController, TurnBannerWidgetClass);

			if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
			{
				LoadingGameInstance->BindButtonClickSounds(TurnBannerWidget);
			}
			if (TurnBannerWidget)
			{
				TurnBannerWidget->AddToViewport(TurnBannerZOrder);
				TurnBannerWidget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}


void AHexGridActor::CreateActionWarningWidget()
{
	if (!ActionWarningWidget && ActionWarningWidgetClass)
	{
		APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PlayerController)
		{
			ActionWarningWidget = CreateWidget<UUserWidget>(PlayerController, ActionWarningWidgetClass);

			if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
			{
				LoadingGameInstance->BindButtonClickSounds(ActionWarningWidget);
			}
			if (ActionWarningWidget)
			{
				ActionWarningWidget->AddToViewport(ActionWarningZOrder);
				ActionWarningWidget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void AHexGridActor::ShowActionWarning(const FText& TitleText, const FText& SubtitleText)
{
	CreateActionWarningWidget();

	if (!ActionWarningWidget)
	{
		if (GEngine)
		{
			const FString DebugText = FString::Printf(TEXT("%s\n%s"), *TitleText.ToString(), *SubtitleText.ToString());
			GEngine->AddOnScreenDebugMessage(777030, FMath::Max(0.1f, ActionWarningDuration), FColor::Orange, DebugText);
		}

		return;
	}

	if (UImage* WarningImage = Cast<UImage>(ActionWarningWidget->GetWidgetFromName(ActionWarningImageName)))
	{
		if (ActionWarningTexture)
		{
			WarningImage->SetBrushFromTexture(ActionWarningTexture, true);
		}

		WarningImage->SetColorAndOpacity(ActionWarningImageColor);
		WarningImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (UTextBlock* TitleBlock = Cast<UTextBlock>(ActionWarningWidget->GetWidgetFromName(ActionWarningTitleTextBlockName)))
	{
		TitleBlock->SetText(TitleText);
		TitleBlock->SetColorAndOpacity(FSlateColor(ActionWarningTextColor));
	}

	if (UTextBlock* SubTextBlock = Cast<UTextBlock>(ActionWarningWidget->GetWidgetFromName(ActionWarningSubTextBlockName)))
	{
		SubTextBlock->SetText(SubtitleText);
		SubTextBlock->SetColorAndOpacity(FSlateColor(ActionWarningTextColor));
	}

	const ESlateVisibility NewVisibility = ESlateVisibility::HitTestInvisible;
	ActionWarningWidget->SetVisibility(NewVisibility);

	if (UWidget* RootWidget = ActionWarningWidget->GetWidgetFromName(ActionWarningRootName))
	{
		RootWidget->SetVisibility(NewVisibility);
	}

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(ActionWarningTimerHandle);
		GetWorldTimerManager().SetTimer(
			ActionWarningTimerHandle,
			this,
			&AHexGridActor::HideActionWarning,
			FMath::Max(0.1f, ActionWarningDuration),
			false
		);
	}
}

void AHexGridActor::HideActionWarning()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(ActionWarningTimerHandle);
	}

	if (!ActionWarningWidget)
	{
		return;
	}

	ActionWarningWidget->SetVisibility(ESlateVisibility::Collapsed);

	if (UWidget* RootWidget = ActionWarningWidget->GetWidgetFromName(ActionWarningRootName))
	{
		RootWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AHexGridActor::CreateResultScreenWidget()
{
	if (ResultScreenWidget || !ResultScreenWidgetClass)
	{
		return;
	}

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return;
	}

	ResultScreenWidget = CreateWidget<UUserWidget>(PlayerController, ResultScreenWidgetClass);

	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->BindButtonClickSounds(ResultScreenWidget);
	}
	if (!ResultScreenWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create result screen widget."));
		return;
	}

	ResultScreenWidget->AddToViewport(ResultScreenZOrder);
	ResultScreenWidget->SetVisibility(ESlateVisibility::Collapsed);

	if (UButton* MainMenuButton = Cast<UButton>(ResultScreenWidget->GetWidgetFromName(ResultMainMenuButtonName)))
	{
		MainMenuButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleResultMainMenuButtonClicked);
		MainMenuButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleResultMainMenuButtonClicked);
		MainMenuButton->SetIsEnabled(true);
	}

	if (UButton* ArmyButton = Cast<UButton>(ResultScreenWidget->GetWidgetFromName(ResultArmyButtonName)))
	{
		ArmyButton->OnClicked.RemoveDynamic(this, &AHexGridActor::HandleResultArmyButtonClicked);
		ArmyButton->OnClicked.AddDynamic(this, &AHexGridActor::HandleResultArmyButtonClicked);
		ArmyButton->SetIsEnabled(bEnableResultArmyButton && !ArmyLevelName.IsNone());
	}
}

bool AHexGridActor::HasAlivePlayerUnitsForMatchResult() const
{
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		if (!bCountSummonedUnitsForMatchResult && Unit->bIsSummonedUnit)
		{
			continue;
		}

		return true;
	}

	return false;
}

bool AHexGridActor::HasAliveEnemyUnitsForMatchResult() const
{
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		if (!bCountSummonedUnitsForMatchResult && Unit->bIsSummonedUnit)
		{
			continue;
		}

		return true;
	}

	return false;
}

void AHexGridActor::EvaluateMatchResultAfterUnitDeath()
{
	if (bMatchFinished || !bCheckMatchResultAfterUnitDeath)
	{
		return;
	}

	const bool bHasAlivePlayerUnits = HasAlivePlayerUnitsForMatchResult();
	const bool bHasAliveEnemyUnits = HasAliveEnemyUnitsForMatchResult();

	if (!bHasAliveEnemyUnits && bHasAlivePlayerUnits)
	{
		FinishMatch(EHexMatchResult::Victory);
		return;
	}

	if (!bHasAlivePlayerUnits)
	{
		FinishMatch(EHexMatchResult::Defeat);
		return;
	}

	if (!bHasAliveEnemyUnits)
	{
		FinishMatch(EHexMatchResult::Victory);
	}
}

void AHexGridActor::RegisterPlayerArmyUnitForProgression(AHexUnitActor* Unit, int32 ArmyIndex)
{
	if (!IsValid(Unit) || ArmyIndex < 0)
	{
		return;
	}

	PlayerArmyIndexByUnit.Add(Unit, ArmyIndex);
	RawBattleExperienceByPlayerArmyIndex.FindOrAdd(ArmyIndex);
}

void AHexGridActor::ApplySavedProgressToSpawnedUnit(AHexUnitActor* Unit, int32 Level, int32 CurrentExperience)
{
	if (!IsValid(Unit))
	{
		return;
	}

	Unit->SetProgressionState(Level, CurrentExperience);
	Unit->ApplyProgressionStatsToRuntimeUnit();
	Unit->CurrentHealth = Unit->MaxHealth;
	Unit->UpdateHealthBarWidget();
}

void AHexGridActor::AwardBattleExperience(AHexUnitActor* Unit, int32 RawExperience, const TCHAR* Reason)
{
	if (!IsValid(Unit) || RawExperience <= 0 || Unit->Team != EHexUnitTeam::Player || Unit->bIsSummonedUnit)
	{
		return;
	}

	const int32* FoundArmyIndex = PlayerArmyIndexByUnit.Find(Unit);
	if (!FoundArmyIndex)
	{
		return;
	}

	const int32 SafeArmyIndex = *FoundArmyIndex;
	RawBattleExperienceByPlayerArmyIndex.FindOrAdd(SafeArmyIndex) += RawExperience;

	UE_LOG(LogTemp, Verbose, TEXT("Battle XP +%d Unit=%s ArmyIndex=%d Reason=%s RawTotal=%d"),
		RawExperience,
		*GetNameSafe(Unit),
		SafeArmyIndex,
		Reason ? Reason : TEXT("Unknown"),
		RawBattleExperienceByPlayerArmyIndex.FindRef(SafeArmyIndex)
	);
}

void AHexGridActor::CommitBattleExperienceToSavedArmy(EHexMatchResult MatchResult)
{
	if (bBattleExperienceCommitted)
	{
		return;
	}

	bBattleExperienceCommitted = true;
	LastBattleExperienceRows.Reset();

	const TArray<TSubclassOf<AHexUnitActor>> ArmyClasses = UArmyBuilderWidget::GetSavedPlayerArmyUnitClasses();
	const TArray<FArmyBuilderUnitProgress> ProgressBeforeBattle = UArmyBuilderWidget::GetSavedPlayerArmyUnitProgressList();

	if (!RawBattleExperienceByPlayerArmyIndex.IsEmpty())
	{
		UArmyBuilderWidget::ApplyBattleExperienceToSavedArmy(
			RawBattleExperienceByPlayerArmyIndex,
			GetArmyExperienceOutcome(MatchResult)
		);
	}

	const TArray<FArmyBuilderUnitProgress> ProgressAfterBattle = UArmyBuilderWidget::GetSavedPlayerArmyUnitProgressList();
	LastBattleExperienceRows.Reserve(ArmyClasses.Num());

	for (int32 ArmyIndex = 0; ArmyIndex < ArmyClasses.Num(); ++ArmyIndex)
	{
		const TSubclassOf<AHexUnitActor> ArmyUnitClass = ArmyClasses[ArmyIndex];
		const AHexUnitActor* DefaultUnit = ArmyUnitClass ? ArmyUnitClass->GetDefaultObject<AHexUnitActor>() : nullptr;

		const FArmyBuilderUnitProgress BeforeProgress = ProgressBeforeBattle.IsValidIndex(ArmyIndex)
			? ProgressBeforeBattle[ArmyIndex]
			: UArmyBuilderWidget::MakeDefaultUnitProgress(ArmyUnitClass);
		const FArmyBuilderUnitProgress AfterProgress = ProgressAfterBattle.IsValidIndex(ArmyIndex)
			? ProgressAfterBattle[ArmyIndex]
			: BeforeProgress;

		FHexBattleExperienceResultRow& Row = LastBattleExperienceRows.AddDefaulted_GetRef();
		Row.UnitName = DefaultUnit && !DefaultUnit->UnitDisplayName.IsEmpty()
			? DefaultUnit->UnitDisplayName
			: GetNameSafe(ArmyUnitClass.Get());
		Row.Level = FMath::Clamp(AfterProgress.Level, 1, AHexUnitActor::GetMaxProgressionLevel());
		Row.bMaxLevel = Row.Level >= AHexUnitActor::GetMaxProgressionLevel();
		Row.CurrentExperience = Row.bMaxLevel ? 0 : FMath::Max(0, AfterProgress.CurrentExperience);
		Row.RequiredExperience = Row.bMaxLevel ? 0 : AHexUnitActor::GetExperienceToNextLevelForLevel(Row.Level);
		Row.EarnedExperience = Row.bMaxLevel
			? 0
			: FMath::Max(0, Row.CurrentExperience - FMath::Max(0, BeforeProgress.CurrentExperience));
		Row.bCanUpgrade = !Row.bMaxLevel
			&& Row.RequiredExperience > 0
			&& Row.CurrentExperience >= Row.RequiredExperience;
	}
}

EArmyBattleExperienceOutcome AHexGridActor::GetArmyExperienceOutcome(EHexMatchResult MatchResult) const
{
	if (bPlayerSurrenderedForExperience)
	{
		return EArmyBattleExperienceOutcome::Surrender;
	}

	return MatchResult == EHexMatchResult::Victory
		? EArmyBattleExperienceOutcome::Victory
		: EArmyBattleExperienceOutcome::Defeat;
}

int32 AHexGridActor::CountAliveNonSummonedUnitsForTeam(EHexUnitTeam Team) const
{
	int32 Count = 0;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (IsValid(Unit) && !Unit->GetIsDead() && !Unit->bIsSummonedUnit && Unit->Team == Team)
		{
			++Count;
		}
	}

	return Count;
}

float AHexGridActor::GetBattleCoinDifficultyMultiplier() const
{
	switch (EnemyBotDifficulty)
	{
	case EHexBotDifficulty::WarmUp:
		return FMath::Max(0.0f, WarmUpCoinMultiplier);
	case EHexBotDifficulty::Challenge:
		return FMath::Max(0.0f, ChallengeCoinMultiplier);
	case EHexBotDifficulty::Ordeal:
		return FMath::Max(0.0f, OrdealCoinMultiplier);
	case EHexBotDifficulty::Nightmare:
		return FMath::Max(0.0f, NightmareCoinMultiplier);
	default:
		return 1.0f;
	}
}

float AHexGridActor::GetBattleCoinOutcomeMultiplier(EHexMatchResult MatchResult) const
{
	if (bPlayerSurrenderedForExperience)
	{
		return FMath::Max(0.0f, SurrenderCoinMultiplier);
	}

	return MatchResult == EHexMatchResult::Victory
		? FMath::Max(0.0f, VictoryCoinMultiplier)
		: FMath::Max(0.0f, DefeatCoinMultiplier);
}

int32 AHexGridActor::CalculateBattleCoinReward(EHexMatchResult MatchResult) const
{
	if (!bEnableBattleCoinRewards || MatchResult == EHexMatchResult::None || InitialEnemyUnitCountForRewards <= 0)
	{
		return 0;
	}

	if (bPlayerSurrenderedForExperience && BattleElapsedTime < FMath::Max(0.0f, MinimumBattleTimeForSurrenderReward))
	{
		return 0;
	}

	const float BaseRewardValue = static_cast<float>(FMath::Max(0, BaseBattleCoinReward))
		* GetBattleCoinOutcomeMultiplier(MatchResult)
		* GetBattleCoinDifficultyMultiplier();
	const int32 ScaledBaseReward = FMath::Max(0, FMath::RoundToInt(BaseRewardValue));

	// Surrender gives only the small base reward. It never receives performance bonuses.
	if (bPlayerSurrenderedForExperience)
	{
		return ScaledBaseReward;
	}

	const int32 AliveEnemies = CountAliveNonSummonedUnitsForTeam(EHexUnitTeam::Enemy);
	const int32 AlivePlayers = CountAliveNonSummonedUnitsForTeam(EHexUnitTeam::Player);
	const int32 DefeatedEnemies = FMath::Clamp(
		InitialEnemyUnitCountForRewards - AliveEnemies,
		0,
		InitialEnemyUnitCountForRewards
	);

	int32 PerformanceBonus = 0;
	PerformanceBonus += DefeatedEnemies * FMath::Max(0, CoinsPerDefeatedEnemy);
	PerformanceBonus += AlivePlayers * FMath::Max(0, CoinsPerSurvivingPlayerUnit);

	const bool bVictory = MatchResult == EHexMatchResult::Victory;
	const bool bNoPlayerDeaths = InitialPlayerUnitCountForRewards > 0 && AlivePlayers >= InitialPlayerUnitCountForRewards;
	if (bVictory && bNoPlayerDeaths)
	{
		PerformanceBonus += FMath::Max(0, NoPlayerDeathsCoinBonus);
	}

	if (bVictory && BattleElapsedTime <= FMath::Max(0.0f, FastVictoryTimeLimit))
	{
		PerformanceBonus += FMath::Max(0, FastVictoryCoinBonus);
	}

	PerformanceBonus = FMath::Clamp(PerformanceBonus, 0, FMath::Max(0, MaxPerformanceCoinBonus));
	return FMath::Max(0, ScaledBaseReward + PerformanceBonus);
}

void AHexGridActor::CommitBattleCoinReward(EHexMatchResult MatchResult)
{
	if (bBattleCoinsCommitted)
	{
		return;
	}

	bBattleCoinsCommitted = true;
	LastBattleCoinReward = CalculateBattleCoinReward(MatchResult);
	UArmyBuilderWidget::AddSavedCoins(LastBattleCoinReward);

	UE_LOG(LogTemp, Log, TEXT("Battle coins committed: Reward=%d Total=%d Difficulty=%s Surrender=%s"),
		LastBattleCoinReward,
		UArmyBuilderWidget::GetSavedCoins(),
		*UEnum::GetValueAsString(EnemyBotDifficulty),
		bPlayerSurrenderedForExperience ? TEXT("true") : TEXT("false")
	);
}

FText AHexGridActor::BuildBattleCoinRewardText() const
{
	if (!bEnableBattleCoinRewards)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(FString::Printf(
		TEXT("COINS EARNED: +%d"),
		FMath::Max(0, LastBattleCoinReward)
	));
}

void AHexGridActor::FinishMatch(EHexMatchResult MatchResult)
{
	if (bMatchFinished || MatchResult == EHexMatchResult::None)
	{
		return;
	}

	bMatchFinished = true;
	PendingMatchResult = MatchResult;

	CommitBattleExperienceToSavedArmy(MatchResult);
	CommitBattleCoinReward(MatchResult);

	// Write once after both progression and currency have been updated. This avoids
	// saving the new XP with the old coin balance when a match finishes.
	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(GetGameInstance()))
	{
		LoadingGameInstance->SaveAccountProgression();
	}

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(MatchStartBannerTimerHandle);
		GetWorldTimerManager().ClearTimer(TurnStartBannerTimerHandle);
		GetWorldTimerManager().ClearTimer(ActionWarningTimerHandle);
		GetWorldTimerManager().ClearTimer(AutoEndPlayerTurnTimerHandle);
		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
		GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);
		GetWorldTimerManager().ClearTimer(MatchResultTimerHandle);
	}

	ClearPendingProjectileAttack(true);

	StopPlayerTurnTimer(true);
	StopBattleTimer();
	bBattleMenuOpen = false;
	bSurrenderConfirmOpen = false;

	bMatchRuntimeInitializationInProgress = false;
	bMatchIntroInProgress = false;
	bTurnBannerInProgress = false;
	bPlayerTurnEnding = false;
	bEnemyTurnInProgress = false;
	bPlayerMoveAttackInProgress = false;
	bAbilityButtonHovered = false;

	if (IsValid(PlayerAutoEndMovingUnit))
	{
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
	}
	PlayerAutoEndMovingUnit = nullptr;

	if (IsValid(BotMovingUnit))
	{
		BotMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::ContinueEnemyBotTurn);
	}
	BotMovingUnit = nullptr;

	if (IsValid(PendingMoveAttackUnit))
	{
		PendingMoveAttackUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandleMoveThenAttackMovementFinished);
	}
	PendingMoveAttackUnit = nullptr;
	PendingMoveAttackTarget = nullptr;

	ClearSelectionAndHighlights();
	HideActionWarning();
	HideTurnBanner();

	if (ActionPointsWidget)
	{
		ActionPointsWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	OnMatchResultDecided(MatchResult);

	if (bShowMatchResultDebugText && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			777040,
			4.0f,
			MatchResult == EHexMatchResult::Victory ? FColor::Green : FColor::Red,
			MatchResult == EHexMatchResult::Victory ? TEXT("VICTORY") : TEXT("DEFEAT")
		);
	}

	const float SafeDelay = FMath::Max(0.0f, MatchResultScreenDelay);
	if (!GetWorld() || SafeDelay <= 0.0f)
	{
		ShowMatchResultScreen(MatchResult);
		return;
	}

	FTimerDelegate ResultDelegate;
	ResultDelegate.BindUObject(this, &AHexGridActor::ShowMatchResultScreen, MatchResult);
	GetWorldTimerManager().SetTimer(MatchResultTimerHandle, ResultDelegate, SafeDelay, false);
}

namespace
{
	void ArrangeFallbackResultTextAboveProgressBar(
		UUserWidget* ResultWidget,
		FName ExperienceListName,
		FName ProgressBarName,
		FName InfoTextName,
		FName CoinsTextName
	)
	{
		if (!ResultWidget)
		{
			return;
		}

		// If WBP has a dedicated ResultExperienceList, its own container controls layout.
		// This helper is for the current fallback layout where XP rows are written into ResultInfoText.
		if (Cast<UVerticalBox>(ResultWidget->GetWidgetFromName(ExperienceListName)))
		{
			return;
		}

		UProgressBar* ProgressBar = Cast<UProgressBar>(ResultWidget->GetWidgetFromName(ProgressBarName));
		UTextBlock* InfoText = Cast<UTextBlock>(ResultWidget->GetWidgetFromName(InfoTextName));
		UTextBlock* CoinsText = Cast<UTextBlock>(ResultWidget->GetWidgetFromName(CoinsTextName));
		if (!ProgressBar || !InfoText)
		{
			return;
		}

		UCanvasPanelSlot* ProgressSlot = Cast<UCanvasPanelSlot>(ProgressBar->Slot);
		UCanvasPanelSlot* InfoSlot = Cast<UCanvasPanelSlot>(InfoText->Slot);
		UCanvasPanelSlot* CoinsSlot = CoinsText ? Cast<UCanvasPanelSlot>(CoinsText->Slot) : nullptr;
		if (!ProgressSlot || !InfoSlot || ProgressBar->GetParent() != InfoText->GetParent())
		{
			return;
		}

		if (CoinsSlot && CoinsText->GetParent() != ProgressBar->GetParent())
		{
			CoinsSlot = nullptr;
		}

		const FVector2D ProgressPosition = ProgressSlot->GetPosition();
		const FVector2D ProgressSize = ProgressSlot->GetSize();
		const FVector2D ProgressAlignment = ProgressSlot->GetAlignment();
		const FVector2D ProgressTopLeft = ProgressPosition - ProgressAlignment * ProgressSize;
		const float ContentWidth = FMath::Max(1.0f, ProgressSize.X);

		TArray<FString> InfoLines;
		InfoText->GetText().ToString().ParseIntoArrayLines(InfoLines, false);
		const int32 InfoLineCount = FMath::Max(1, InfoLines.Num());
		const float InfoLineHeight = FMath::Max(15.0f, static_cast<float>(InfoText->GetFont().Size) + 3.0f);
		const float InfoHeight = InfoLineHeight * static_cast<float>(InfoLineCount);
		const float GapAboveProgressBar = 7.0f;
		const float GapBetweenInfoAndCoins = 2.0f;

		float CursorY = ProgressTopLeft.Y - GapAboveProgressBar;

		const bool bShowSeparateCoinsText = CoinsText
			&& CoinsSlot
			&& CoinsText->GetVisibility() != ESlateVisibility::Collapsed
			&& !CoinsText->GetText().IsEmpty();

		if (bShowSeparateCoinsText)
		{
			const float CoinsHeight = FMath::Max(16.0f, static_cast<float>(CoinsText->GetFont().Size) + 4.0f);
			CursorY -= CoinsHeight;

			CoinsSlot->SetAnchors(ProgressSlot->GetAnchors());
			CoinsSlot->SetAlignment(FVector2D::ZeroVector);
			CoinsSlot->SetAutoSize(false);
			CoinsSlot->SetPosition(FVector2D(ProgressTopLeft.X, CursorY));
			CoinsSlot->SetSize(FVector2D(ContentWidth, CoinsHeight));
			CoinsText->SetJustification(ETextJustify::Center);
			CoinsText->SetAutoWrapText(false);

			CursorY -= GapBetweenInfoAndCoins;
		}

		CursorY -= InfoHeight;
		InfoSlot->SetAnchors(ProgressSlot->GetAnchors());
		InfoSlot->SetAlignment(FVector2D::ZeroVector);
		InfoSlot->SetAutoSize(false);
		InfoSlot->SetPosition(FVector2D(ProgressTopLeft.X, CursorY));
		InfoSlot->SetSize(FVector2D(ContentWidth, InfoHeight));
		InfoText->SetJustification(ETextJustify::Center);
		InfoText->SetAutoWrapText(false);
	}
}

void AHexGridActor::ShowMatchResultScreen(EHexMatchResult MatchResult)
{
	CreateResultScreenWidget();

	if (!ResultScreenWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("ResultScreenWidgetClass is not set. Cannot show match result screen."));
		return;
	}

	const bool bVictory = MatchResult == EHexMatchResult::Victory;

	ResultScreenWidget->SetVisibility(ESlateVisibility::Visible);

	if (UWidget* RootWidget = ResultScreenWidget->GetWidgetFromName(ResultRootName))
	{
		RootWidget->SetVisibility(ESlateVisibility::Visible);
	}

	if (UImage* WindowImage = Cast<UImage>(ResultScreenWidget->GetWidgetFromName(ResultWindowImageName)))
	{
		UTexture2D* WindowTexture = bVictory ? VictoryResultWindowTexture : DefeatResultWindowTexture;
		if (WindowTexture)
		{
			WindowImage->SetBrushFromTexture(WindowTexture, true);
		}

		WindowImage->SetColorAndOpacity(ResultWindowImageColor);
		WindowImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (UTextBlock* TitleText = Cast<UTextBlock>(ResultScreenWidget->GetWidgetFromName(ResultTitleTextBlockName)))
	{
		TitleText->SetText(bVictory ? VictoryResultTitle : DefeatResultTitle);
		TitleText->SetColorAndOpacity(FSlateColor(bVictory ? VictoryResultTitleColor : DefeatResultTitleColor));
	}

	if (UTextBlock* SubText = Cast<UTextBlock>(ResultScreenWidget->GetWidgetFromName(ResultSubTextBlockName)))
	{
		SubText->SetText(bVictory ? VictoryResultSubText : DefeatResultSubText);
		SubText->SetColorAndOpacity(FSlateColor(ResultSubTextColor));
	}

	PopulateResultExperienceList();

	const FText CoinRewardText = BuildBattleCoinRewardText();
	bool bUsedDedicatedCoinsText = false;

	if (UTextBlock* CoinsText = Cast<UTextBlock>(ResultScreenWidget->GetWidgetFromName(ResultCoinsTextBlockName)))
	{
		CoinsText->SetText(CoinRewardText);
		CoinsText->SetVisibility(bEnableBattleCoinRewards ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		bUsedDedicatedCoinsText = true;
	}

	if (UTextBlock* InfoText = Cast<UTextBlock>(ResultScreenWidget->GetWidgetFromName(ResultInfoTextBlockName)))
	{
		const bool bHasRuntimeExperienceList = Cast<UVerticalBox>(ResultScreenWidget->GetWidgetFromName(ResultExperienceListName)) != nullptr;
		if (bHasRuntimeExperienceList)
		{
			InfoText->SetText(ResultInfoText);
			InfoText->SetVisibility(ResultInfoText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		}
		else
		{
			FString FallbackText = BuildFallbackBattleExperienceText().ToString();
			if (!ResultInfoText.IsEmpty())
			{
				if (!FallbackText.IsEmpty())
				{
					FallbackText += TEXT("\n");
				}
				FallbackText += ResultInfoText.ToString();
			}

			if (bEnableBattleCoinRewards && !bUsedDedicatedCoinsText)
			{
				if (!FallbackText.IsEmpty())
				{
					FallbackText += TEXT("\n");
				}
				FallbackText += CoinRewardText.ToString();
			}

			InfoText->SetText(FText::FromString(FallbackText));
			InfoText->SetVisibility(FallbackText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		}
	}

	ArrangeFallbackResultTextAboveProgressBar(
		ResultScreenWidget,
		ResultExperienceListName,
		ResultProgressBarName,
		ResultInfoTextBlockName,
		ResultCoinsTextBlockName
	);

	if (UTextBlock* ButtonText = Cast<UTextBlock>(ResultScreenWidget->GetWidgetFromName(ResultMainMenuButtonTextName)))
	{
		ButtonText->SetText(ResultMainMenuButtonText);
	}

	if (UTextBlock* ButtonText = Cast<UTextBlock>(ResultScreenWidget->GetWidgetFromName(ResultArmyButtonTextName)))
	{
		ButtonText->SetText(ResultArmyButtonText);
	}

	if (UButton* MainMenuButton = Cast<UButton>(ResultScreenWidget->GetWidgetFromName(ResultMainMenuButtonName)))
	{
		MainMenuButton->SetIsEnabled(true);
	}

	if (UButton* ArmyButton = Cast<UButton>(ResultScreenWidget->GetWidgetFromName(ResultArmyButtonName)))
	{
		ArmyButton->SetIsEnabled(bEnableResultArmyButton && !ArmyLevelName.IsNone());
	}

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController)
	{
		PlayerController->bShowMouseCursor = true;
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;

		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ResultScreenWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
	}
}


FText AHexGridActor::BuildFallbackBattleExperienceText() const
{
	TArray<FString> Lines;
	Lines.Reserve(LastBattleExperienceRows.Num());

	for (const FHexBattleExperienceResultRow& Row : LastBattleExperienceRows)
	{
		const int32 UpgradeCoinCost = Row.bMaxLevel ? 0 : AHexUnitActor::GetUpgradeCoinCostForLevel(Row.Level);
		const bool bCanUpgradeNow = Row.bCanUpgrade
			&& UpgradeCoinCost > 0
			&& UArmyBuilderWidget::GetSavedCoins() >= UpgradeCoinCost;
		const FString ProgressText = Row.bMaxLevel
			? FString(TEXT("MAX LEVEL"))
			: FString::Printf(TEXT("%d / %d"), Row.CurrentExperience, Row.RequiredExperience);
		const FString UpgradeText = bCanUpgradeNow ? TEXT("   UPGRADE") : TEXT("");

		Lines.Add(FString::Printf(
			TEXT("%s  ----------------  %s   +%d%s"),
			*Row.UnitName,
			*ProgressText,
			Row.EarnedExperience,
			*UpgradeText
		));
	}

	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

void AHexGridActor::PopulateResultExperienceList()
{
	if (!ResultScreenWidget || !ResultScreenWidget->WidgetTree)
	{
		return;
	}

	UVerticalBox* ExperienceList = Cast<UVerticalBox>(ResultScreenWidget->GetWidgetFromName(ResultExperienceListName));
	UProgressBar* LegacyProgressBar = Cast<UProgressBar>(ResultScreenWidget->GetWidgetFromName(ResultProgressBarName));

	if (!ExperienceList)
	{
		if (LegacyProgressBar)
		{
			if (LastBattleExperienceRows.IsEmpty())
			{
				LegacyProgressBar->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				const FHexBattleExperienceResultRow& Row = LastBattleExperienceRows[0];
				const int32 UpgradeCoinCost = Row.bMaxLevel ? 0 : AHexUnitActor::GetUpgradeCoinCostForLevel(Row.Level);
				const bool bCanUpgradeNow = Row.bCanUpgrade
					&& UpgradeCoinCost > 0
					&& UArmyBuilderWidget::GetSavedCoins() >= UpgradeCoinCost;
				const float Percent = Row.bMaxLevel
					? 1.0f
					: (Row.RequiredExperience > 0
						? FMath::Clamp(static_cast<float>(Row.CurrentExperience) / static_cast<float>(Row.RequiredExperience), 0.0f, 1.0f)
						: 0.0f);

				LegacyProgressBar->SetPercent(Percent);
				LegacyProgressBar->SetFillColorAndOpacity(bCanUpgradeNow ? ResultExperienceUpgradeColor : ResultExperienceProgressColor);
				LegacyProgressBar->SetVisibility(ESlateVisibility::Visible);
			}
		}
		return;
	}

	ExperienceList->ClearChildren();
	ExperienceList->SetVisibility(LastBattleExperienceRows.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	if (LegacyProgressBar)
	{
		LegacyProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	}

	const FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle(
		TEXT("Regular"),
		FMath::Clamp(ResultExperienceFontSize, 8, 48)
	);

	for (const FHexBattleExperienceResultRow& Row : LastBattleExperienceRows)
	{
		const int32 UpgradeCoinCost = Row.bMaxLevel ? 0 : AHexUnitActor::GetUpgradeCoinCostForLevel(Row.Level);
		const bool bCanUpgradeNow = Row.bCanUpgrade
			&& UpgradeCoinCost > 0
			&& UArmyBuilderWidget::GetSavedCoins() >= UpgradeCoinCost;

		UHorizontalBox* TextRow = ResultScreenWidget->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* NameText = ResultScreenWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		UTextBlock* LeaderText = ResultScreenWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		UTextBlock* ProgressText = ResultScreenWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		UTextBlock* EarnedText = ResultScreenWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		UTextBlock* UpgradeText = ResultScreenWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

		if (!TextRow || !NameText || !LeaderText || !ProgressText || !EarnedText || !UpgradeText)
		{
			continue;
		}

		NameText->SetText(FText::FromString(Row.UnitName));
		NameText->SetFont(RowFont);
		NameText->SetColorAndOpacity(FSlateColor(ResultExperienceNameColor));

		LeaderText->SetText(FText::FromString(TEXT("  ----------------  ")));
		LeaderText->SetFont(RowFont);
		LeaderText->SetJustification(ETextJustify::Center);
		LeaderText->SetColorAndOpacity(FSlateColor(ResultExperienceLeaderColor));

		ProgressText->SetText(Row.bMaxLevel
			? FText::FromString(TEXT("MAX LEVEL"))
			: FText::FromString(FString::Printf(TEXT("%d / %d"), Row.CurrentExperience, Row.RequiredExperience)));
		ProgressText->SetFont(RowFont);
		ProgressText->SetColorAndOpacity(FSlateColor(bCanUpgradeNow ? ResultExperienceUpgradeColor : ResultExperienceNumberColor));

		EarnedText->SetText(FText::FromString(FString::Printf(TEXT("+%d"), Row.EarnedExperience)));
		EarnedText->SetFont(RowFont);
		EarnedText->SetColorAndOpacity(FSlateColor(bCanUpgradeNow ? ResultExperienceUpgradeColor : ResultExperienceEarnedColor));

		UpgradeText->SetText(ResultExperienceUpgradeText);
		UpgradeText->SetFont(RowFont);
		UpgradeText->SetColorAndOpacity(FSlateColor(ResultExperienceUpgradeColor));
		UpgradeText->SetVisibility(bCanUpgradeNow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

		UHorizontalBoxSlot* NameSlot = TextRow->AddChildToHorizontalBox(NameText);
		UHorizontalBoxSlot* LeaderSlot = TextRow->AddChildToHorizontalBox(LeaderText);
		UHorizontalBoxSlot* ProgressSlot = TextRow->AddChildToHorizontalBox(ProgressText);
		UHorizontalBoxSlot* EarnedSlot = TextRow->AddChildToHorizontalBox(EarnedText);
		UHorizontalBoxSlot* UpgradeSlot = TextRow->AddChildToHorizontalBox(UpgradeText);

		if (NameSlot)
		{
			NameSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			NameSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (LeaderSlot)
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			LeaderSlot->SetSize(FillSize);
			LeaderSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (ProgressSlot)
		{
			ProgressSlot->SetPadding(FMargin(6.0f, 0.0f, 10.0f, 0.0f));
			ProgressSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (EarnedSlot)
		{
			EarnedSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
			EarnedSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UpgradeSlot)
		{
			UpgradeSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* TextRowSlot = ExperienceList->AddChildToVerticalBox(TextRow))
		{
			TextRowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
			TextRowSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		USizeBox* ProgressSizeBox = ResultScreenWidget->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		UProgressBar* ProgressBar = ResultScreenWidget->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		if (!ProgressSizeBox || !ProgressBar)
		{
			continue;
		}

		const float Percent = Row.bMaxLevel
			? 1.0f
			: (Row.RequiredExperience > 0
				? FMath::Clamp(static_cast<float>(Row.CurrentExperience) / static_cast<float>(Row.RequiredExperience), 0.0f, 1.0f)
				: 0.0f);
		ProgressBar->SetPercent(Percent);
		ProgressBar->SetFillColorAndOpacity(bCanUpgradeNow ? ResultExperienceUpgradeColor : ResultExperienceProgressColor);
		ProgressSizeBox->SetHeightOverride(FMath::Max(2.0f, ResultExperienceProgressBarHeight));
		ProgressSizeBox->AddChild(ProgressBar);

		if (UVerticalBoxSlot* ProgressBarSlot = ExperienceList->AddChildToVerticalBox(ProgressSizeBox))
		{
			ProgressBarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ResultExperienceRowSpacing));
			ProgressBarSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void AHexGridActor::HandleResultMainMenuButtonClicked()
{
	if (MainMenuLevelName.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu button failed: MainMenuLevelName is None."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Reuse the same loading WBP that is shown before entering battle, but with
	// main-menu-specific text. The selected screen type is preserved for both
	// the blocking map load and a real post-load shader compilation phase.
	if (UGameLoadingGameInstance* LoadingGameInstance = Cast<UGameLoadingGameInstance>(World->GetGameInstance()))
	{
		LoadingGameInstance->PrepareNextLoadingScreen(
			EGameLoadingScreenType::Battle,
			NSLOCTEXT("GameLoading", "ReturnToMainMenuTitle", "LOADING"),
			NSLOCTEXT("GameLoading", "ReturnToMainMenuStatus", "Loading main menu...")
		);
	}

	UGameplayStatics::OpenLevel(World, MainMenuLevelName, true);
}

void AHexGridActor::HandleResultArmyButtonClicked()
{
	if (!bEnableResultArmyButton || ArmyLevelName.IsNone())
	{
		UE_LOG(LogTemp, Log, TEXT("Army button clicked, but army screen is not enabled yet."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UGameplayStatics::OpenLevel(World, ArmyLevelName, true);
}

void AHexGridActor::ShowNotEnoughActionPointsWarning()
{
	ShowActionWarning(
		NotEnoughActionPointsTitle,
		NotEnoughActionPointsSubText
	);
}

void AHexGridActor::SetTurnBannerWidgetVisibility(bool bVisible)
{
	if (!TurnBannerWidget)
	{
		return;
	}

	const ESlateVisibility NewVisibility = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	TurnBannerWidget->SetVisibility(NewVisibility);

	if (UWidget* RootWidget = TurnBannerWidget->GetWidgetFromName(TurnBannerRootName))
	{
		RootWidget->SetVisibility(NewVisibility);
	}
}

void AHexGridActor::ShowTurnBanner(const FText& TitleText, const FText& SubtitleText, const FLinearColor& TextColor, EHexTurnOwner TurnOwner, bool bIsMatchStart)
{
	CreateTurnBannerWidget();

	if (TurnBannerWidget)
	{
		if (UTextBlock* TitleBlock = Cast<UTextBlock>(TurnBannerWidget->GetWidgetFromName(TurnBannerTitleTextBlockName)))
		{
			TitleBlock->SetText(TitleText);
			TitleBlock->SetColorAndOpacity(FSlateColor(TextColor));
		}

		if (UTextBlock* SubtitleBlock = Cast<UTextBlock>(TurnBannerWidget->GetWidgetFromName(TurnBannerSubTextBlockName)))
		{
			SubtitleBlock->SetText(SubtitleText);
			SubtitleBlock->SetColorAndOpacity(FSlateColor(TextColor));
		}

		if (UImage* LeftLineImage = Cast<UImage>(TurnBannerWidget->GetWidgetFromName(TurnBannerLeftLineImageName)))
		{
			LeftLineImage->SetColorAndOpacity(TextColor);
		}

		if (UImage* RightLineImage = Cast<UImage>(TurnBannerWidget->GetWidgetFromName(TurnBannerRightLineImageName)))
		{
			RightLineImage->SetColorAndOpacity(TextColor);
		}

		if (UImage* IconImage = Cast<UImage>(TurnBannerWidget->GetWidgetFromName(TurnBannerIconImageName)))
		{
			IconImage->SetColorAndOpacity(TextColor);
		}

		if (UImage* BannerImage = Cast<UImage>(TurnBannerWidget->GetWidgetFromName(TurnBannerImageName)))
		{
			UTexture2D* BannerTexture = nullptr;

			if (bIsMatchStart)
			{
				BannerTexture = MatchStartBannerTexture;
			}
			else
			{
				BannerTexture = TurnOwner == EHexTurnOwner::Player
					? PlayerTurnBannerTexture
					: EnemyTurnBannerTexture;
			}

			if (BannerTexture)
			{
				BannerImage->SetBrushFromTexture(BannerTexture, true);
				BannerImage->SetColorAndOpacity(FLinearColor::White);
				BannerImage->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				BannerImage->SetVisibility(ESlateVisibility::Collapsed);
			}
		}

		SetTurnBannerWidgetVisibility(true);
	}

	OnTurnBannerRequested(TitleText, SubtitleText, TurnOwner, bIsMatchStart);

	if (bShowTurnBannerDebugText && GEngine)
	{
		const FString DebugText = FString::Printf(TEXT("%s\n%s"), *TitleText.ToString(), *SubtitleText.ToString());
		GEngine->AddOnScreenDebugMessage(777020, 3.0f, FColor::White, DebugText);
	}
}

void AHexGridActor::HideTurnBanner()
{
	SetTurnBannerWidgetVisibility(false);
	OnTurnBannerHidden();
}

void AHexGridActor::ShowMatchStartBanner()
{
	ShowTurnBanner(
		FText::FromString(TEXT("BATTLE START")),
		FText::FromString(FString::Printf(TEXT("Turn %d"), FMath::Max(1, CurrentRoundNumber))),
		MatchStartBannerColor,
		EHexTurnOwner::Player,
		true
	);
}

void AHexGridActor::ShowTurnOwnerBanner(EHexTurnOwner TurnOwner)
{
	const bool bPlayer = TurnOwner == EHexTurnOwner::Player;
	ShowTurnBanner(
		FText::FromString(bPlayer ? TEXT("PLAYER TURN") : TEXT("ENEMY TURN")),
		FText::FromString(FString::Printf(TEXT("Turn %d"), FMath::Max(1, CurrentRoundNumber))),
		bPlayer ? PlayerTurnBannerColor : EnemyTurnBannerColor,
		TurnOwner,
		false
	);
}

EHexTurnOwner AHexGridActor::ChooseFirstTurnOwner() const
{
	if (!bRandomizeFirstTurn)
	{
		return FixedFirstTurnOwner;
	}

	return FMath::RandBool() ? EHexTurnOwner::Player : EHexTurnOwner::Enemy;
}

void AHexGridActor::StartMatchIntroSequence()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(MatchStartBannerTimerHandle);
		GetWorldTimerManager().ClearTimer(TurnStartBannerTimerHandle);
		GetWorldTimerManager().ClearTimer(AutoEndPlayerTurnTimerHandle);
		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
	}

	bMatchIntroInProgress = true;
	bTurnBannerInProgress = false;
	bPlayerTurnEnding = false;
	bEnemyTurnInProgress = false;
	PendingTurnOwner = EHexTurnOwner::Player;
	CurrentRoundNumber = 1;

	StopPlayerTurnTimer(true);
	ClearSelectionAndHighlights();
	UpdateActionPointsWidget();

	ShowMatchStartBanner();

	const float SafeDuration = FMath::Max(0.0f, MatchStartBannerDuration);
	if (!GetWorld() || SafeDuration <= 0.0f)
	{
		HandleMatchStartBannerFinished();
		return;
	}

	GetWorldTimerManager().SetTimer(
		MatchStartBannerTimerHandle,
		this,
		&AHexGridActor::HandleMatchStartBannerFinished,
		SafeDuration,
		false
	);
}

void AHexGridActor::HandleMatchStartBannerFinished()
{
	if (!bMatchIntroInProgress)
	{
		return;
	}

	bMatchIntroInProgress = false;
	HideTurnBanner();
	StartTurnWithBanner(ChooseFirstTurnOwner());
}

void AHexGridActor::StartFirstTurn()
{
	CurrentRoundNumber = 1;
	StartTurnWithBanner(ChooseFirstTurnOwner());
}

void AHexGridActor::StartTurnWithBanner(EHexTurnOwner NewTurnOwner)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(TurnStartBannerTimerHandle);
	}

	PendingTurnOwner = NewTurnOwner;
	StopPlayerTurnTimer(true);
	ClearSelectionAndHighlights();

	if (!bEnableTurnStartBanners || TurnStartBannerDuration <= 0.0f)
	{
		bTurnBannerInProgress = false;
		HideTurnBanner();

		if (NewTurnOwner == EHexTurnOwner::Player)
		{
			BeginPlayerTurnAfterBanner();
		}
		else
		{
			BeginEnemyTurnAfterBanner();
		}

		return;
	}

	bTurnBannerInProgress = true;
	UpdateActionPointsWidget();
	ShowTurnOwnerBanner(NewTurnOwner);

	if (!GetWorld())
	{
		HandleTurnStartBannerFinished();
		return;
	}

	GetWorldTimerManager().SetTimer(
		TurnStartBannerTimerHandle,
		this,
		&AHexGridActor::HandleTurnStartBannerFinished,
		FMath::Max(0.0f, TurnStartBannerDuration),
		false
	);
}

void AHexGridActor::HandleTurnStartBannerFinished()
{
	if (!bTurnBannerInProgress)
	{
		return;
	}

	bTurnBannerInProgress = false;
	HideTurnBanner();

	if (PendingTurnOwner == EHexTurnOwner::Player)
	{
		BeginPlayerTurnAfterBanner();
	}
	else
	{
		BeginEnemyTurnAfterBanner();
	}
}

bool AHexGridActor::IsTurnIntroInProgress() const
{
	return bMatchIntroInProgress || bTurnBannerInProgress;
}

bool AHexGridActor::HasEnoughActionPoints(int32 Cost) const
{
	const int32 SafeCost = FMath::Max(0, Cost);
	return SafeCost <= 0 || CurrentActionPoints >= SafeCost;
}

bool AHexGridActor::SpendActionPoints(int32 Cost)
{
	const int32 SafeCost = FMath::Max(0, Cost);

	if (!HasEnoughActionPoints(SafeCost))
	{
		UE_LOG(LogTemp, Warning, TEXT("Not enough action points. Current=%d Max=%d Cost=%d"),
			CurrentActionPoints,
			MaxActionPoints,
			SafeCost
		);

		return false;
	}

	if (SafeCost <= 0)
	{
		return true;
	}

	CurrentActionPoints = FMath::Clamp(CurrentActionPoints - SafeCost, 0, FMath::Max(0, MaxActionPoints));
	UpdateActionPointsWidget();

	UE_LOG(LogTemp, Log, TEXT("Action points spent. Cost=%d Current=%d/%d"),
		SafeCost,
		CurrentActionPoints,
		MaxActionPoints
	);

	return true;
}

void AHexGridActor::ResetActionPoints()
{
	MaxActionPoints = FMath::Max(0, MaxActionPoints);
	CurrentActionPoints = MaxActionPoints;

	UpdateActionPointsWidget();

	UE_LOG(LogTemp, Log, TEXT("Action points reset: %d/%d"),
		CurrentActionPoints,
		MaxActionPoints
	);
}

void AHexGridActor::AddActionPoints(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	MaxActionPoints = FMath::Max(0, MaxActionPoints);
	CurrentActionPoints = FMath::Clamp(CurrentActionPoints + Amount, 0, MaxActionPoints);
	UpdateActionPointsWidget();
}

int32 AHexGridActor::CalculateMoveActionPointCost(int32 PathLength) const
{
	const int32 SafeMoveCost = FMath::Max(0, MoveActionPointCost);

	if (bUsePathLengthForMoveCost)
	{
		return SafeMoveCost * FMath::Max(0, PathLength);
	}

	return SafeMoveCost;
}

int32 AHexGridActor::CalculateAttackActionPointCost() const
{
	return FMath::Max(0, AttackActionPointCost);
}

int32 AHexGridActor::CalculateHealActionPointCost(AHexUnitActor* Healer) const
{
	if (!IsValid(Healer))
	{
		return 0;
	}

	return Healer->GetHealActionPointCost();
}

int32 AHexGridActor::CalculateSummonActionPointCost(AHexUnitActor* Summoner) const
{
	if (!IsValid(Summoner))
	{
		return 0;
	}

	return Summoner->GetSummonActionPointCost();
}

int32 AHexGridActor::CalculateChampionAbilityActionPointCost(AHexUnitActor* Champion) const
{
	if (!IsValid(Champion))
	{
		return 0;
	}

	return Champion->GetChampionAbilityActionPointCost();
}

int32 AHexGridActor::GetEffectiveMovementRangeForUnit(AHexUnitActor* Unit) const
{
	if (!IsValid(Unit) || Unit->GetIsDead())
	{
		return 0;
	}

	const int32 RemainingMovementRange = GetRemainingMovementRangeForUnit(Unit);
	if (RemainingMovementRange <= 0)
	{
		return 0;
	}

	const int32 SafeMoveCost = FMath::Max(0, MoveActionPointCost);

	if (!bUsePathLengthForMoveCost)
	{
		return HasEnoughActionPoints(SafeMoveCost) ? RemainingMovementRange : 0;
	}

	if (SafeMoveCost <= 0)
	{
		return RemainingMovementRange;
	}

	return FMath::Clamp(CurrentActionPoints / SafeMoveCost, 0, RemainingMovementRange);
}

int32 AHexGridActor::GetMovementSpentThisTurnForUnit(AHexUnitActor* Unit) const
{
	if (!IsValid(Unit))
	{
		return 0;
	}

	const int32* FoundSpent = MovementSpentByUnitThisTurn.Find(Unit);
	return FoundSpent ? FMath::Max(0, *FoundSpent) : 0;
}

int32 AHexGridActor::GetRemainingMovementRangeForUnit(AHexUnitActor* Unit) const
{
	if (!IsValid(Unit) || Unit->GetIsDead())
	{
		return 0;
	}

	const int32 MaxMoveRange = FMath::Max(0, Unit->MovementRange);
	const int32 SpentMoveRange = FMath::Clamp(GetMovementSpentThisTurnForUnit(Unit), 0, MaxMoveRange);
	return FMath::Max(0, MaxMoveRange - SpentMoveRange);
}

bool AHexGridActor::HasEnoughMovementRangeForUnit(AHexUnitActor* Unit, int32 PathLength) const
{
	const int32 SafePathLength = FMath::Max(0, PathLength);
	return SafePathLength <= GetRemainingMovementRangeForUnit(Unit);
}

void AHexGridActor::SpendMovementRangeForUnit(AHexUnitActor* Unit, int32 PathLength)
{
	if (!IsValid(Unit) || PathLength <= 0)
	{
		return;
	}

	const int32 MaxMoveRange = FMath::Max(0, Unit->MovementRange);
	const int32 CurrentSpent = GetMovementSpentThisTurnForUnit(Unit);
	const int32 NewSpent = FMath::Clamp(CurrentSpent + PathLength, 0, MaxMoveRange);

	MovementSpentByUnitThisTurn.Add(Unit, NewSpent);
	AwardBattleExperience(Unit, FMath::Max(0, NewSpent - CurrentSpent), TEXT("Move"));

	UE_LOG(LogTemp, Log, TEXT("Movement range spent. Unit=%s Spent=%d/%d Remaining=%d"),
		*GetNameSafe(Unit),
		NewSpent,
		MaxMoveRange,
		GetRemainingMovementRangeForUnit(Unit)
	);
}

void AHexGridActor::ResetMovementRangeForTurnOwner(EHexTurnOwner TurnOwner)
{
	MovementSpentByUnitThisTurn.Empty();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead())
		{
			continue;
		}

		const bool bMatchesTurnOwner =
			(TurnOwner == EHexTurnOwner::Player && Unit->Team == EHexUnitTeam::Player) ||
			(TurnOwner == EHexTurnOwner::Enemy && Unit->Team == EHexUnitTeam::Enemy);

		if (bMatchesTurnOwner)
		{
			MovementSpentByUnitThisTurn.Add(Unit, 0);
		}
	}
}

bool AHexGridActor::HasUnitAttackedThisTurn(AHexUnitActor* Unit) const
{
	return IsValid(Unit) && UnitsThatAttackedThisTurn.Contains(Unit);
}

bool AHexGridActor::CanUnitAttackThisTurn(AHexUnitActor* Unit) const
{
	return IsValid(Unit) && !Unit->GetIsDead() && !HasUnitAttackedThisTurn(Unit);
}

void AHexGridActor::MarkUnitAttackedThisTurn(AHexUnitActor* Unit)
{
	if (!IsValid(Unit) || Unit->GetIsDead())
	{
		return;
	}

	UnitsThatAttackedThisTurn.Add(Unit);
	UE_LOG(LogTemp, Log, TEXT("Attack usage spent. Unit=%s"), *GetNameSafe(Unit));
}

void AHexGridActor::ResetAttackUsageForTurnOwner(EHexTurnOwner TurnOwner)
{
	// Only one team acts at a time, so the previous turn's attack usage can be discarded.
	UnitsThatAttackedThisTurn.Empty();

	UE_LOG(LogTemp, Log, TEXT("Attack usage reset for %s turn."),
		TurnOwner == EHexTurnOwner::Player ? TEXT("player") : TEXT("enemy")
	);
}


void AHexGridActor::UpdateActionPointsWidget()
{
	MaxActionPoints = FMath::Max(0, MaxActionPoints);
	CurrentActionPoints = FMath::Clamp(CurrentActionPoints, 0, MaxActionPoints);

	FString TurnLabel;
	FLinearColor TurnColor = FLinearColor(0.45f, 0.78f, 1.0f, 1.0f);

	if (bMatchIntroInProgress)
	{
		TurnLabel = TEXT("BATTLE START");
		TurnColor = FLinearColor(1.0f, 0.86f, 0.35f, 1.0f);
	}
	else if (bTurnBannerInProgress)
	{
		const bool bPendingPlayerTurn = PendingTurnOwner == EHexTurnOwner::Player;
		TurnLabel = bPendingPlayerTurn ? TEXT("PLAYER TURN") : TEXT("ENEMY TURN");
		TurnColor = bPendingPlayerTurn
			? FLinearColor(0.0f, 0.92f, 1.0f, 1.0f)
			: FLinearColor(1.0f, 0.28f, 0.18f, 1.0f);
	}
	else
	{
		if (IsPlayerTurn())
		{
			TurnLabel = bPlayerTurnEnding ? TEXT("PLAYER TURN") : TEXT("PLAYER TURN");
			TurnColor = FLinearColor(0.0f, 0.92f, 1.0f, 1.0f);
		}
		else
		{
			TurnLabel = TEXT("ENEMY TURN");
			TurnColor = FLinearColor(1.0f, 0.28f, 0.18f, 1.0f);
		}
	}

	const bool bShowTimer = bEnablePlayerTurnTimer && IsPlayerTurn() && !IsTurnIntroInProgress();
	const int32 RemainingSeconds = FMath::Max(0, FMath::CeilToInt(PlayerTurnTimeRemaining));

	const FString PointsLabel = FString::Printf(
		TEXT("POINTS %d/%d"),
		CurrentActionPoints,
		MaxActionPoints
	);

	const FString TimerLabel = bShowTimer
		? FString::Printf(TEXT("TIME %d"), RemainingSeconds)
		: FString(TEXT("TIME --"));

	const FString FullStatusString = FString::Printf(
		TEXT("%s | %s | %s"),
		*TurnLabel,
		*PointsLabel,
		*TimerLabel
	);

	if (ActionPointsWidget)
	{
		UTextBlock* PointsTextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(ActionPointsTextBlockName));
		UTextBlock* TurnTextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(TurnTextBlockName));
		UTextBlock* TimerTextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(TurnTimerTextBlockName));

		//        WBP                TurnText / TurnTimerText, ActionPointsText                     .
		//                                                                                        .
		const bool bHasSeparatedTopBarText = TurnTextBlock || TimerTextBlock;

		if (PointsTextBlock)
		{
			PointsTextBlock->SetText(FText::FromString(bHasSeparatedTopBarText ? PointsLabel : FullStatusString));
			PointsTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.88f, 0.46f, 1.0f)));
		}

		if (TurnTextBlock)
		{
			TurnTextBlock->SetText(FText::FromString(TurnLabel));
			TurnTextBlock->SetColorAndOpacity(FSlateColor(TurnColor));
		}

		if (TimerTextBlock)
		{
			TimerTextBlock->SetText(FText::FromString(TimerLabel));
		}

		RefreshBattleTimerDisplay(true);
		UpdateBattleMenuWidgetState();
		UpdateTurnTimerWarningVisuals();

		if (UButton* EndTurnButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(EndTurnButtonName)))
		{
			EndTurnButton->SetIsEnabled(IsPlayerInputAllowed());
		}

		auto SetWidgetVisibilityByName = [this](const FName& WidgetName, ESlateVisibility Visibility) -> bool
			{
				if (UWidget* Widget = ActionPointsWidget->GetWidgetFromName(WidgetName))
				{
					Widget->SetVisibility(Visibility);
					return true;
				}

				return false;
			};

		const bool bHasSelectedLivingUnit = IsPlayerInputAllowed() && IsValid(SelectedUnit) && !SelectedUnit->GetIsDead();
		const bool bSelectedPlayerUnit = bHasSelectedLivingUnit && SelectedUnit->Team == EHexUnitTeam::Player;
		const bool bSelectedEnemyUnit = bHasSelectedLivingUnit && SelectedUnit->Team == EHexUnitTeam::Enemy;
		const bool bCanUsePlayerActionButtons = bSelectedPlayerUnit && SelectedUnit->CanAct();

		const ESlateVisibility PlayerActionVisibility = bSelectedPlayerUnit
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed;

		const bool bSelectedUnitHasChampionAbility = bHasSelectedLivingUnit && SelectedUnit->CanUseChampionAbility();
		const bool bSelectedUnitCanActivateChampionAbility = bSelectedUnitHasChampionAbility && SelectedUnit->CanActivateChampionAbility();

		const ESlateVisibility AbilityVisibility = ((bSelectedEnemyUnit && bSelectedUnitHasChampionAbility) || (bSelectedPlayerUnit && bSelectedUnitHasChampionAbility))
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed;

		SetWidgetVisibilityByName(UnitMoveButtonContainerName, PlayerActionVisibility);
		if (UButton* MoveButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitMoveButtonName)))
		{
			MoveButton->SetVisibility(PlayerActionVisibility);
			MoveButton->SetIsEnabled(bCanUsePlayerActionButtons && GetEffectiveMovementRangeForUnit(SelectedUnit) > 0);
		}

		SetWidgetVisibilityByName(UnitAttackButtonContainerName, PlayerActionVisibility);
		if (UButton* AttackButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitAttackButtonName)))
		{
			//                                              ,                                   WBP-              .
			AttackButton->SetVisibility(PlayerActionVisibility);
			AttackButton->SetIsEnabled(
				bCanUsePlayerActionButtons &&
				(CanUnitAttackThisTurn(SelectedUnit) || SelectedUnit->CanHeal())
			);
		}

		SetWidgetVisibilityByName(UnitAbilityButtonContainerName, AbilityVisibility);
		if (UButton* AbilityButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonName)))
		{
			AbilityButton->SetVisibility(AbilityVisibility);

			//                                     ,    HandleUnitAbilityButtonClicked                          .
			AbilityButton->SetIsEnabled(
				(bSelectedEnemyUnit && bSelectedUnitHasChampionAbility) ||
				(
					bCanUsePlayerActionButtons &&
					bSelectedUnitCanActivateChampionAbility &&
					HasEnoughActionPoints(CalculateChampionAbilityActionPointCost(SelectedUnit))
					)
			);
		}
	}

	if (bShowActionPointsDebugText && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(777001, 999999.0f, FColor::Cyan, FullStatusString);
	}

	OnActionPointsChanged.Broadcast(CurrentActionPoints, MaxActionPoints);
}

bool AHexGridActor::IsPlayerTurn() const
{
	return CurrentTurnOwner == EHexTurnOwner::Player;
}

bool AHexGridActor::IsEnemyTurn() const
{
	return CurrentTurnOwner == EHexTurnOwner::Enemy;
}


bool AHexGridActor::IsPlayerInputAllowed() const
{
	return IsPlayerTurn()
		&& !bMatchFinished
		&& !bMatchRuntimeInitializationInProgress
		&& !bPlayerTurnEnding
		&& !bEnemyTurnInProgress
		&& !bPlayerMoveAttackInProgress
		&& !bAttackProjectileInProgress
		&& !bBattleMenuOpen
		&& !bSurrenderConfirmOpen
		&& !IsTurnIntroInProgress();
}



float AHexGridActor::GetBattleElapsedTime() const
{
	return FMath::Max(0.0f, BattleElapsedTime);
}

FText AHexGridActor::GetBattleElapsedTimeText() const
{
	return FormatBattleElapsedTimeText();
}

FText AHexGridActor::FormatBattleElapsedTimeText() const
{
	const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt(BattleElapsedTime));
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;

	return FText::FromString(FString::Printf(
		TEXT("%s %02d:%02d"),
		*BattleTimerPrefixText.ToString(),
		Minutes,
		Seconds
	));
}

void AHexGridActor::StartBattleTimerIfNeeded()
{
	if (bMatchFinished)
	{
		return;
	}

	if (!bBattleTimerStarted)
	{
		BattleElapsedTime = 0.0f;
		LastDisplayedBattleTimerSecond = INDEX_NONE;
		bBattleTimerStarted = true;
	}

	bBattleTimerRunning = bEnableBattleTimer;
	RefreshBattleTimerDisplay(true);
}

void AHexGridActor::StopBattleTimer()
{
	bBattleTimerRunning = false;
	RefreshBattleTimerDisplay(true);
}

void AHexGridActor::UpdateBattleTimer(float DeltaTime)
{
	if (!bEnableBattleTimer || !bBattleTimerRunning || bMatchFinished)
	{
		return;
	}

	BattleElapsedTime = FMath::Max(0.0f, BattleElapsedTime + FMath::Max(0.0f, DeltaTime));
	RefreshBattleTimerDisplay(false);
}

void AHexGridActor::RefreshBattleTimerDisplay(bool bForceUpdate)
{
	if (!ActionPointsWidget)
	{
		return;
	}

	const int32 CurrentDisplayedSecond = bEnableBattleTimer
		? FMath::Max(0, FMath::FloorToInt(BattleElapsedTime))
		: INDEX_NONE;

	if (!bForceUpdate && CurrentDisplayedSecond == LastDisplayedBattleTimerSecond)
	{
		return;
	}

	LastDisplayedBattleTimerSecond = CurrentDisplayedSecond;

	if (UTextBlock* BattleTimerTextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(BattleTimerTextBlockName)))
	{
		BattleTimerTextBlock->SetText(FormatBattleElapsedTimeText());
	}
}

void AHexGridActor::ToggleBattleMenu()
{
	ShowBattleMenu(!bBattleMenuOpen);
}

void AHexGridActor::ShowBattleMenu(bool bShow)
{
	if (bMatchFinished || !bBattleTimerStarted)
	{
		bShow = false;
	}

	bBattleMenuOpen = bShow;

	if (!bBattleMenuOpen)
	{
		bSurrenderConfirmOpen = false;
	}

	UpdateBattleMenuWidgetState();
	UpdateActionPointsWidget();
}

void AHexGridActor::ShowSurrenderConfirm(bool bShow)
{
	if (bMatchFinished || !bBattleTimerStarted)
	{
		bShow = false;
	}

	bSurrenderConfirmOpen = bShow;
	bBattleMenuOpen = bShow ? false : bBattleMenuOpen;

	UpdateBattleMenuWidgetState();
	UpdateActionPointsWidget();
}

void AHexGridActor::RequestPlayerSurrender()
{
	if (bMatchFinished || !bBattleTimerStarted)
	{
		return;
	}

	ShowSurrenderConfirm(true);
}

void AHexGridActor::ConfirmPlayerSurrender()
{
	if (bMatchFinished || !bBattleTimerStarted)
	{
		return;
	}

	bBattleMenuOpen = false;
	bSurrenderConfirmOpen = false;
	UpdateBattleMenuWidgetState();

	bPlayerSurrenderedForExperience = true;
	UE_LOG(LogTemp, Log, TEXT("Player surrendered."));
	FinishMatch(EHexMatchResult::Defeat);
}

void AHexGridActor::UpdateBattleMenuWidgetState()
{
	if (!ActionPointsWidget)
	{
		return;
	}

	if (UButton* MenuButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(BattleMenuButtonName)))
	{
		MenuButton->SetIsEnabled(!bMatchFinished && bBattleTimerStarted);
	}

	if (UWidget* MenuPanel = ActionPointsWidget->GetWidgetFromName(BattleMenuPanelName))
	{
		MenuPanel->SetVisibility(bBattleMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (UWidget* ConfirmPanel = ActionPointsWidget->GetWidgetFromName(SurrenderConfirmPanelName))
	{
		ConfirmPanel->SetVisibility(bSurrenderConfirmOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(BattleSettingsButtonTextName)))
	{
		TextBlock->SetText(BattleSettingsButtonText);
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(BattleSurrenderButtonTextName)))
	{
		TextBlock->SetText(BattleSurrenderButtonText);
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(SurrenderTitleTextBlockName)))
	{
		TextBlock->SetText(SurrenderTitleText);
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(SurrenderSubTextBlockName)))
	{
		TextBlock->SetText(SurrenderSubText);
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(SurrenderCancelButtonTextName)))
	{
		TextBlock->SetText(SurrenderCancelButtonText);
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(SurrenderConfirmButtonTextName)))
	{
		TextBlock->SetText(SurrenderConfirmButtonText);
	}
}

void AHexGridActor::HandleBattleMenuButtonClicked()
{
	ToggleBattleMenu();
}

void AHexGridActor::HandleBattleSettingsButtonClicked()
{
	ShowBattleMenu(false);
	OnBattleSettingsRequested();
}

void AHexGridActor::HandleBattleSurrenderButtonClicked()
{
	RequestPlayerSurrender();
}

void AHexGridActor::HandleSurrenderCancelButtonClicked()
{
	ShowSurrenderConfirm(false);
}

void AHexGridActor::HandleSurrenderConfirmButtonClicked()
{
	ConfirmPlayerSurrender();
}

void AHexGridActor::UpdateTurnTimerWarningVisuals()
{
	if (!ActionPointsWidget)
	{
		return;
	}

	UTextBlock* TimerTextBlock = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(TurnTimerTextBlockName));
	if (!TimerTextBlock)
	{
		return;
	}

	const bool bShowTimer =
		bEnablePlayerTurnTimer &&
		IsPlayerTurn() &&
		!IsTurnIntroInProgress();

	const bool bWarningTimer =
		bShowTimer &&
		PlayerTurnTimerWarningThreshold > 0.0f &&
		PlayerTurnTimeRemaining > 0.0f &&
		PlayerTurnTimeRemaining <= PlayerTurnTimerWarningThreshold;

	TimerTextBlock->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	if (!bWarningTimer)
	{
		TimerTextBlock->SetColorAndOpacity(FSlateColor(PlayerTurnTimerNormalColor));
		TimerTextBlock->SetRenderScale(FVector2D(1.0f, 1.0f));
		return;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	const float SafeBlinkInterval = FMath::Max(0.05f, PlayerTurnTimerWarningBlinkInterval);
	const int32 BlinkStep = FMath::FloorToInt(CurrentTime / SafeBlinkInterval);
	const bool bUseRedColor = BlinkStep % 2 == 0;

	const FLinearColor CurrentColor = bUseRedColor
		? PlayerTurnTimerWarningRedColor
		: PlayerTurnTimerWarningWhiteColor;

	TimerTextBlock->SetColorAndOpacity(FSlateColor(CurrentColor));

	const float SafePulseSpeed = FMath::Max(0.01f, PlayerTurnTimerWarningPulseSpeed);
	const float PulseAlpha = 0.5f + 0.5f * FMath::Sin(CurrentTime * SafePulseSpeed * 2.0f * PI);

	const float SafeMaxScale = FMath::Max(1.0f, PlayerTurnTimerWarningMaxScale);
	const float CurrentScale = FMath::Lerp(1.0f, SafeMaxScale, PulseAlpha);

	TimerTextBlock->SetRenderScale(FVector2D(CurrentScale, CurrentScale));
}

float AHexGridActor::GetPlayerTurnTimeRemaining() const
{
	return FMath::Max(0.0f, PlayerTurnTimeRemaining);
}

float AHexGridActor::GetPlayerTurnTimePercent() const
{
	if (!bEnablePlayerTurnTimer || PlayerTurnTimeLimit <= KINDA_SMALL_NUMBER || IsTurnIntroInProgress())
	{
		return 0.0f;
	}

	return FMath::Clamp(PlayerTurnTimeRemaining / PlayerTurnTimeLimit, 0.0f, 1.0f);
}

void AHexGridActor::StartPlayerTurnTimer()
{
	LastDisplayedPlayerTurnTimerSecond = INDEX_NONE;

	if (!bEnablePlayerTurnTimer || PlayerTurnTimeLimit <= 0.0f)
	{
		PlayerTurnTimeRemaining = 0.0f;
		RefreshTurnTimerDisplayIfNeeded(true);
		return;
	}

	PlayerTurnTimeLimit = FMath::Max(1.0f, PlayerTurnTimeLimit);
	PlayerTurnTimeRemaining = PlayerTurnTimeLimit;
	RefreshTurnTimerDisplayIfNeeded(true);
}

void AHexGridActor::StopPlayerTurnTimer(bool bResetRemainingTime)
{
	if (bResetRemainingTime)
	{
		PlayerTurnTimeRemaining = 0.0f;
	}

	RefreshTurnTimerDisplayIfNeeded(true);
}

void AHexGridActor::UpdatePlayerTurnTimer(float DeltaTime)
{
	if (!bEnablePlayerTurnTimer || !IsPlayerTurn() || bPlayerTurnEnding || bEnemyTurnInProgress || bPlayerMoveAttackInProgress || IsTurnIntroInProgress())
	{
		return;
	}

	if (PlayerTurnTimeLimit <= 0.0f)
	{
		return;
	}

	if (PlayerTurnTimeRemaining <= 0.0f)
	{
		return;
	}

	PlayerTurnTimeRemaining = FMath::Max(0.0f, PlayerTurnTimeRemaining - FMath::Max(0.0f, DeltaTime));
	RefreshTurnTimerDisplayIfNeeded(false);

	if (PlayerTurnTimeRemaining <= 0.0f)
	{
		HandlePlayerTurnTimerExpired();
	}
}

void AHexGridActor::RefreshTurnTimerDisplayIfNeeded(bool bForceUpdate)
{
	const int32 CurrentDisplayedSecond = bEnablePlayerTurnTimer && IsPlayerTurn() && !IsTurnIntroInProgress()
		? FMath::Max(0, FMath::CeilToInt(PlayerTurnTimeRemaining))
		: INDEX_NONE;

	if (!bForceUpdate && CurrentDisplayedSecond == LastDisplayedPlayerTurnTimerSecond)
	{
		return;
	}

	LastDisplayedPlayerTurnTimerSecond = CurrentDisplayedSecond;
	UpdateActionPointsWidget();
	OnPlayerTurnTimerChanged.Broadcast(PlayerTurnTimeRemaining, PlayerTurnTimeLimit);
}

void AHexGridActor::HandlePlayerTurnTimerExpired()
{
	if (!IsPlayerTurn() || bPlayerTurnEnding)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Player turn timer expired. Auto-ending player turn."));
	EndPlayerTurn();
}

void AHexGridActor::HandleEndTurnButtonClicked()
{
	EndPlayerTurn();
}

bool AHexGridActor::CanUseSelectedPlayerUnitActionHotkeys() const
{
	return IsPlayerInputAllowed()
		&& CurrentTurnOwner == EHexTurnOwner::Player
		&& IsValid(SelectedUnit)
		&& !SelectedUnit->GetIsDead()
		&& SelectedUnit->Team == EHexUnitTeam::Player
		&& SelectedUnit->CanAct();
}

FText AHexGridActor::BuildHotkeyButtonText(const FString& HotkeyLabel, const FText& BaseLabel) const
{
	const FString SafeBaseLabel = BaseLabel.IsEmpty() ? FString(TEXT("ACTION")) : BaseLabel.ToString();
	return FText::FromString(FString::Printf(TEXT("[%s] %s"), *HotkeyLabel, *SafeBaseLabel));
}

void AHexGridActor::HandleActionHotkeys()
{
	if (!CanUseSelectedPlayerUnitActionHotkeys())
	{
		return;
	}

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Q))
	{
		HandleUnitMoveButtonClicked();
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::W))
	{
		HandleUnitAttackButtonClicked();
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::E))
	{
		HandleUnitAbilityButtonClicked();
	}
}

void AHexGridActor::HandleUnitMoveButtonClicked()
{
	if (!IsPlayerInputAllowed() || !IsValid(SelectedUnit) || SelectedUnit->GetIsDead() || SelectedUnit->Team != EHexUnitTeam::Player || !SelectedUnit->CanAct())
	{
		return;
	}

	//                              ,                                                               .
	HideActionWarning();
	bAbilityButtonHovered = false;
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);
	}
	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();

	CurrentSelectedActionMode = EHexSelectedActionMode::Move;
	ClearAttackRangeHighlight();
	ShowMoveRangeForUnit(SelectedUnit);
	UpdateSelectedUnitWidget();

	UE_LOG(LogTemp, Log, TEXT("Selected action mode: Move. Unit=%s"), *GetNameSafe(SelectedUnit));
}

void AHexGridActor::HandleUnitAttackButtonClicked()
{
	if (!IsPlayerInputAllowed() || !IsValid(SelectedUnit) || SelectedUnit->GetIsDead() || SelectedUnit->Team != EHexUnitTeam::Player || !SelectedUnit->CanAct())
	{
		return;
	}

	if (!CanUnitAttackThisTurn(SelectedUnit) && !SelectedUnit->CanHeal())
	{
		ShowActionWarning(AttackAlreadyUsedTitle, AttackAlreadyUsedSubText);
		UE_LOG(LogTemp, Log, TEXT("Attack mode blocked: unit has already attacked this turn. Unit=%s"), *GetNameSafe(SelectedUnit));
		return;
	}

	const int32 AttackPointCost = CalculateAttackActionPointCost();
	if (!HasEnoughActionPoints(AttackPointCost))
	{
		ShowNotEnoughActionPointsWarning();

		UE_LOG(LogTemp, Warning, TEXT("Attack mode blocked: not enough action points. Cost=%d Current=%d/%d"),
			AttackPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return;
	}

	//                              . Attack                                     
	//     move+attack                                         .
	HideActionWarning();
	bAbilityButtonHovered = false;
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);
	}
	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();

	CurrentSelectedActionMode = EHexSelectedActionMode::Attack;
	ClearMoveRangeHighlight();
	ShowAttackRangeForUnit(SelectedUnit);
	UpdateSelectedUnitWidget();

	UE_LOG(LogTemp, Log, TEXT("Selected action mode: Attack. Unit=%s"), *GetNameSafe(SelectedUnit));
}

void AHexGridActor::HandleUnitAbilityButtonClicked()
{
	if (bSuppressNextAbilityButtonClick || bAbilityButtonHoldPreviewTriggered)
	{
		StopAbilityPreviewAndTooltip(true);
		return;
	}

	StopAbilityPreviewAndTooltip(true);

	if (!IsPlayerInputAllowed() || !IsValid(SelectedUnit) || SelectedUnit->GetIsDead())
	{
		return;
	}

	//                                   .        Ability                 ,                    .
	if (SelectedUnit->Team != EHexUnitTeam::Player)
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy ability button clicked in inspect mode. No action. Unit=%s"), *GetNameSafe(SelectedUnit));
		return;
	}

	if (!SelectedUnit->CanAct())
	{
		return;
	}

	if (!SelectedUnit->CanUseChampionAbility())
	{
		UE_LOG(LogTemp, Warning, TEXT("Champion ability mode blocked: selected unit is not a champion ability user. Unit=%s"), *GetNameSafe(SelectedUnit));
		return;
	}

	if (!SelectedUnit->CanActivateChampionAbility())
	{
		ShowChampionAbilityUnavailableWarning(SelectedUnit);

		UE_LOG(LogTemp, Log, TEXT("Champion ability mode blocked: ability is unavailable. Unit=%s RemainingLastStandTurns=%d RemainingCooldown=%d"),
			*GetNameSafe(SelectedUnit),
			SelectedUnit->RemainingLastStandTurns,
			SelectedUnit->GetRemainingChampionAbilityCooldownTurns()
		);
		UpdateSelectedUnitWidget();
		return;
	}

	const int32 AbilityPointCost = CalculateChampionAbilityActionPointCost(SelectedUnit);
	if (!HasEnoughActionPoints(AbilityPointCost))
	{
		ShowNotEnoughActionPointsWarning();

		UE_LOG(LogTemp, Warning, TEXT("Champion ability mode blocked: not enough action points. Cost=%d Current=%d/%d"),
			AbilityPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return;
	}

	HideActionWarning();
	StopAbilityPreviewAndTooltip(true);

	CurrentSelectedActionMode = EHexSelectedActionMode::Ability;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();

	if (SelectedUnit->IsChampionAbilitySummon())
	{
		if (!SelectedUnit->CanSummon())
		{
			CurrentSelectedActionMode = EHexSelectedActionMode::None;
			ClearMoveRangeHighlight();

			ShowActionWarning(
				SummonNotConfiguredTitle,
				SummonNotConfiguredSubText
			);

			UE_LOG(LogTemp, Warning, TEXT("Summon ability mode blocked: summon settings are not valid. Unit=%s Class=%s"),
				*GetNameSafe(SelectedUnit),
				*GetNameSafe(SelectedUnit->SummonedUnitClass.Get())
			);

			UpdateSelectedUnitWidget();
			return;
		}

		if (SelectedUnit->SummonPlacementMode == EHexSummonPlacementMode::SelectedCell)
		{
			ShowSummonRangeForUnit(SelectedUnit);
			UpdateSelectedUnitWidget();

			UE_LOG(LogTemp, Log, TEXT("Selected action mode: Ability/Summon/SelectedCell. Unit=%s Ability=%s Cost=%d Range=%d Count=%d"),
				*GetNameSafe(SelectedUnit),
				*SelectedUnit->ChampionAbilityDisplayName.ToString(),
				AbilityPointCost,
				SelectedUnit->GetSummonRange(),
				SelectedUnit->GetSummonUnitCount()
			);

			return;
		}

		// Random Free Cells   Fixed Relative Cells                         : Ability                       .
		if (!SpawnSummonedUnitsFromSummoner(SelectedUnit))
		{
			CurrentSelectedActionMode = EHexSelectedActionMode::None;
			ClearMoveRangeHighlight();
			UpdateSelectedUnitWidget();
		}

		return;
	}

	//      : PrepareChampionAbility()                           ,          Last Stand,
	//                                              SelectedUnit.                      
	//              SelectedUnit                                                .
	AHexUnitActor* AbilityUnit = SelectedUnit;
	const FString AbilityNameForLog = AbilityUnit
		? AbilityUnit->ChampionAbilityDisplayName.ToString()
		: FString(TEXT("ABILITY"));
	const int32 AbilityRangeForLog = AbilityUnit
		? AbilityUnit->ChampionAbilityRange
		: 0;

	UpdateSelectedUnitWidget();

	//     ChampionAbilityType = Summon                            SpawnSummonedUnitsFromSummoner().
	// Custom/LastStand                       PrepareChampionAbility().
	const bool bPreparedOrExecuted = PrepareChampionAbility(AbilityUnit);

	UE_LOG(LogTemp, Log, TEXT("Selected action mode: Ability/Custom. Unit=%s Ability=%s Cost=%d Range=%d Result=%s"),
		*GetNameSafe(AbilityUnit),
		*AbilityNameForLog,
		AbilityPointCost,
		AbilityRangeForLog,
		bPreparedOrExecuted ? TEXT("true") : TEXT("false")
	);
}

void AHexGridActor::HandleUnitAbilityButtonPressed()
{
	bAbilityButtonPressed = true;
	bAbilityButtonHovered = true;
	bAbilityButtonHoldPreviewTriggered = false;
	bSuppressNextAbilityButtonClick = false;
	AbilityPreviewMouseOutsidePollCount = 0;

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewUnhoverValidationTimerHandle);
	}

	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();
	bAbilityPreviewVisibleFromButton = false;
	StartAbilityPreviewTimer();
}

void AHexGridActor::HandleUnitAbilityButtonReleased()
{
	if (bAbilityButtonHoldPreviewTriggered)
	{
		bSuppressNextAbilityButtonClick = true;
	}

	bAbilityButtonPressed = false;
	bAbilityButtonHoldPreviewTriggered = false;

	// A long press should keep the preview while the cursor is still over the ability area.
	// Do not trust UMG IsHovered only: it can flicker when tooltip visibility changes.
	if (IsMouseInsideAbilityPreviewHoverArea())
	{
		bAbilityButtonHovered = true;
		AbilityPreviewMouseOutsidePollCount = 0;
		if (bAbilityPreviewVisibleFromButton)
		{
			StartAbilityPreviewMousePollTimer();
		}
		return;
	}

	bAbilityButtonHovered = false;
	StopAbilityPreviewAndTooltip(false);
}

void AHexGridActor::HandleUnitAbilityButtonHovered()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewUnhoverValidationTimerHandle);
	}

	bAbilityButtonHovered = true;
	AbilityPreviewMouseOutsidePollCount = 0;

	// If tooltip/layout generated a repeated Hovered event while the preview is already visible,
	// do not clear and restart the preview. Just keep it alive.
	if (bAbilityPreviewVisibleFromButton || CurrentAbilityPreviewRangeSet.Num() > 0)
	{
		bAbilityPreviewVisibleFromButton = true;
		ShowAbilityDescriptionTooltipForSelectedUnit();
		StartAbilityPreviewMousePollTimer();
		return;
	}

	StartAbilityPreviewTimer();
}

void AHexGridActor::HandleUnitAbilityButtonUnhovered()
{
	if (bAbilityButtonPressed)
	{
		return;
	}

	// Critical fix: when SizeBox_AbilityDescription becomes visible, UMG can fire a fake
	// Unhovered on AbilityButton. If the preview is visible, do not hide it here.
	// A separate mouse-position polling timer will hide it only after the mouse really leaves.
	if (bAbilityPreviewVisibleFromButton || CurrentAbilityPreviewRangeSet.Num() > 0)
	{
		StartAbilityPreviewMousePollTimer();
		return;
	}

	if (!GetWorld())
	{
		if (!IsMouseInsideAbilityPreviewHoverArea())
		{
			bAbilityButtonHovered = false;
			StopAbilityPreviewAndTooltip(false);
		}
		return;
	}

	GetWorldTimerManager().ClearTimer(AbilityPreviewUnhoverValidationTimerHandle);
	GetWorldTimerManager().SetTimer(
		AbilityPreviewUnhoverValidationTimerHandle,
		this,
		&AHexGridActor::ValidateAbilityButtonUnhover,
		0.12f,
		false
	);
}

void AHexGridActor::StartAbilityPreviewTimer()
{
	if (!GetWorld())
	{
		ShowDelayedAbilityPreviewAndTooltip();
		return;
	}

	GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);

	const float SafeDelay = FMath::Max(0.0f, AbilityPreviewHoverDelay);
	if (SafeDelay <= 0.0f)
	{
		ShowDelayedAbilityPreviewAndTooltip();
		return;
	}

	GetWorldTimerManager().SetTimer(
		AbilityPreviewTimerHandle,
		this,
		&AHexGridActor::ShowDelayedAbilityPreviewAndTooltip,
		SafeDelay,
		false
	);
}

void AHexGridActor::ValidateAbilityButtonUnhover()
{
	if (bAbilityButtonPressed)
	{
		return;
	}

	if (IsMouseInsideAbilityPreviewHoverArea())
	{
		bAbilityButtonHovered = true;
		AbilityPreviewMouseOutsidePollCount = 0;

		if (bAbilityPreviewVisibleFromButton || CurrentAbilityPreviewRangeSet.Num() > 0)
		{
			bAbilityPreviewVisibleFromButton = true;
			ShowAbilityDescriptionTooltipForSelectedUnit();
			StartAbilityPreviewMousePollTimer();
		}
		return;
	}

	bAbilityButtonHovered = false;
	StopAbilityPreviewAndTooltip(false);
}

void AHexGridActor::StartAbilityPreviewMousePollTimer()
{
	if (!GetWorld())
	{
		return;
	}

	if (GetWorldTimerManager().IsTimerActive(AbilityPreviewMousePollTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		AbilityPreviewMousePollTimerHandle,
		this,
		&AHexGridActor::PollAbilityPreviewMousePosition,
		FMath::Max(0.01f, AbilityPreviewMousePollInterval),
		true
	);
}

void AHexGridActor::StopAbilityPreviewMousePollTimer()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewMousePollTimerHandle);
	}

	AbilityPreviewMouseOutsidePollCount = 0;
}

void AHexGridActor::PollAbilityPreviewMousePosition()
{
	if (bAbilityButtonPressed)
	{
		AbilityPreviewMouseOutsidePollCount = 0;
		return;
	}

	if (!bAbilityPreviewVisibleFromButton && CurrentAbilityPreviewRangeSet.Num() == 0)
	{
		StopAbilityPreviewMousePollTimer();
		return;
	}

	if (IsMouseInsideAbilityPreviewHoverArea())
	{
		bAbilityButtonHovered = true;
		AbilityPreviewMouseOutsidePollCount = 0;
		return;
	}

	bAbilityButtonHovered = false;
	++AbilityPreviewMouseOutsidePollCount;

	if (AbilityPreviewMouseOutsidePollCount >= FMath::Max(1, AbilityPreviewMouseOutsidePollsBeforeHide))
	{
		StopAbilityPreviewAndTooltip(false);
	}
}

bool AHexGridActor::IsAbilityButtonCurrentlyHovered()
{
	if (!ActionPointsWidget)
	{
		return false;
	}

	if (UWidget* AbilityButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonName))
	{
		return AbilityButtonWidget->IsHovered();
	}

	return false;
}

bool AHexGridActor::IsMouseInsideAbilityPreviewHoverArea() const
{
	return IsMouseInsideWidgetByName(UnitAbilityButtonName, AbilityPreviewMouseHoverPadding)
		|| IsMouseInsideWidgetByName(UnitAbilityButtonContainerName, AbilityPreviewMouseHoverPadding)
		|| IsMouseInsideWidgetByName(UnitAbilityDescriptionContainerName, 4.0f);
}

bool AHexGridActor::IsMouseInsideWidgetByName(FName WidgetName, float ExtraPadding) const
{
	if (!ActionPointsWidget || WidgetName.IsNone())
	{
		return false;
	}

	UWidget* Widget = ActionPointsWidget->GetWidgetFromName(WidgetName);
	if (!Widget)
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}

	if (Widget->IsHovered())
	{
		return true;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	const FGeometry Geometry = Widget->GetCachedGeometry();
	const FVector2D LocalSize = Geometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return false;
	}

	const FVector2D CursorAbsolutePosition = FSlateApplication::Get().GetCursorPos();
	const FVector2D LocalMousePosition = Geometry.AbsoluteToLocal(CursorAbsolutePosition);
	const float Padding = FMath::Max(0.0f, ExtraPadding);

	return LocalMousePosition.X >= -Padding
		&& LocalMousePosition.Y >= -Padding
		&& LocalMousePosition.X <= LocalSize.X + Padding
		&& LocalMousePosition.Y <= LocalSize.Y + Padding;
}

void AHexGridActor::ShowDelayedAbilityPreviewAndTooltip()
{
	if (!bAbilityButtonPressed && !bAbilityButtonHovered && !IsMouseInsideAbilityPreviewHoverArea())
	{
		return;
	}

	if (IsMouseInsideAbilityPreviewHoverArea())
	{
		bAbilityButtonHovered = true;
	}

	if (bAbilityButtonPressed)
	{
		bAbilityButtonHoldPreviewTriggered = true;
	}

	bAbilityPreviewVisibleFromButton = true;
	AbilityPreviewMouseOutsidePollCount = 0;

	ShowAbilityPreviewRangeForSelectedUnit();
	ShowAbilityDescriptionTooltipForSelectedUnit();
	StartAbilityPreviewMousePollTimer();
}

void AHexGridActor::StopAbilityPreviewAndTooltip(bool bClearPressState)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);
		GetWorldTimerManager().ClearTimer(AbilityPreviewUnhoverValidationTimerHandle);
	}

	StopAbilityPreviewMousePollTimer();

	bAbilityButtonHovered = false;
	bAbilityPreviewVisibleFromButton = false;

	if (bClearPressState)
	{
		bAbilityButtonPressed = false;
		bAbilityButtonHoldPreviewTriggered = false;
		bSuppressNextAbilityButtonClick = false;
	}

	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();
}

void AHexGridActor::ShowAbilityPreviewRangeForSelectedUnit()
{
	ClearAbilityPreviewRangeHighlight();

	if (!bAbilityButtonPressed && !bAbilityButtonHovered && !IsMouseInsideAbilityPreviewHoverArea())
	{
		return;
	}

	if (IsMouseInsideAbilityPreviewHoverArea())
	{
		bAbilityButtonHovered = true;
	}

	if (!IsPlayerInputAllowed() || !IsValid(SelectedUnit) || SelectedUnit->GetIsDead())
	{
		return;
	}

	if (!SelectedUnit->CanUseChampionAbility() || !SelectedUnit->CanActivateChampionAbility())
	{
		return;
	}

	const FIntPoint UnitCoord = SelectedUnit->GetGridCoord();
	const int32 AbilityRange = FMath::Max(0, SelectedUnit->ChampionAbilityRange);

	// Marked for Death always needs a separate enemy target.
	if (SelectedUnit->IsChampionAbilityMarkedForDeath() && AbilityRange <= 0)
	{
		return;
	}

	// AbilityRange = 0 means that the champion ability targets the champion's own cell.
	// GetAttackRangeCells() does not return the center cell, so we add it manually.
	if (AbilityRange <= 0)
	{
		const int32 UnitInstanceIndex = GetInstanceIndex(UnitCoord.X, UnitCoord.Y);
		if (UnitInstanceIndex == INDEX_NONE || IsDeadUnitInstance(UnitInstanceIndex))
		{
			return;
		}

		CurrentAbilityPreviewRangeCells.Add(FHexCoord(UnitCoord.X, UnitCoord.Y));
		CurrentAbilityPreviewRangeSet.Add(UnitCoord);

		UpdateInstanceVisualState(UnitInstanceIndex);

		if (HexMeshComponent)
		{
			HexMeshComponent->MarkRenderStateDirty();
		}

		UE_LOG(LogTemp, Log, TEXT("Ability preview self-cell shown: Unit=%s Range=%d Cells=%d"),
			*GetNameSafe(SelectedUnit),
			AbilityRange,
			CurrentAbilityPreviewRangeSet.Num()
		);

		return;
	}

	GetAttackRangeCells(UnitCoord.X, UnitCoord.Y, AbilityRange, CurrentAbilityPreviewRangeCells);

	for (const FHexCoord& Coord : CurrentAbilityPreviewRangeCells)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.Q, Coord.R);
		if (InstanceIndex == INDEX_NONE || IsDeadUnitInstance(InstanceIndex))
		{
			continue;
		}

		if (SelectedUnit->IsChampionAbilityMarkedForDeath())
		{
			AHexUnitActor* TargetUnit = GetUnitAtCell(Coord.Q, Coord.R);
			if (!IsValid(TargetUnit) || TargetUnit->GetIsDead() || !SelectedUnit->IsEnemyFor(TargetUnit))
			{
				continue;
			}
		}

		CurrentAbilityPreviewRangeSet.Add(FIntPoint(Coord.Q, Coord.R));
	}

	for (const FIntPoint& Coord : CurrentAbilityPreviewRangeSet)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.X, Coord.Y);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}

	UE_LOG(LogTemp, Log, TEXT("Ability preview range shown: Unit=%s Range=%d Cells=%d"),
		*GetNameSafe(SelectedUnit),
		AbilityRange,
		CurrentAbilityPreviewRangeSet.Num()
	);
}

void AHexGridActor::ShowAbilityDescriptionTooltipForSelectedUnit()
{
	if (!ActionPointsWidget)
	{
		return;
	}

	if (!bAbilityButtonPressed && !bAbilityButtonHovered && !IsMouseInsideAbilityPreviewHoverArea())
	{
		return;
	}

	if (IsMouseInsideAbilityPreviewHoverArea())
	{
		bAbilityButtonHovered = true;
	}

	if (!IsPlayerInputAllowed() || !IsValid(SelectedUnit) || SelectedUnit->GetIsDead())
	{
		ClearAbilityDescriptionTooltip();
		return;
	}

	if (!SelectedUnit->CanUseChampionAbility())
	{
		ClearAbilityDescriptionTooltip();
		return;
	}

	const FString DescriptionString = SelectedUnit->ChampionAbilityDescription.ToString().TrimStartAndEnd();
	if (DescriptionString.IsEmpty())
	{
		ClearAbilityDescriptionTooltip();
		return;
	}

	if (UTextBlock* DescriptionText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitAbilityDescriptionTextName)))
	{
		DescriptionText->SetText(FText::FromString(DescriptionString));
		DescriptionText->SetVisibility(ESlateVisibility::HitTestInvisible);
		DescriptionText->SetAutoWrapText(true);

		if (AbilityDescriptionWrapTextAt > 0.0f)
		{
			DescriptionText->SetWrapTextAt(AbilityDescriptionWrapTextAt);
		}
	}

	if (UWidget* DescriptionContainer = ActionPointsWidget->GetWidgetFromName(UnitAbilityDescriptionContainerName))
	{
		DescriptionContainer->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void AHexGridActor::ClearAbilityDescriptionTooltip()
{
	if (!ActionPointsWidget)
	{
		return;
	}

	if (UWidget* DescriptionContainer = ActionPointsWidget->GetWidgetFromName(UnitAbilityDescriptionContainerName))
	{
		DescriptionContainer->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UTextBlock* DescriptionText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitAbilityDescriptionTextName)))
	{
		DescriptionText->SetText(FText::GetEmpty());
		DescriptionText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AHexGridActor::ClearAbilityPreviewRangeHighlight()
{
	if (CurrentAbilityPreviewRangeSet.Num() == 0 && CurrentAbilityPreviewRangeCells.Num() == 0)
	{
		return;
	}

	TSet<FIntPoint> OldHighlightedCoords = CurrentAbilityPreviewRangeSet;

	CurrentAbilityPreviewRangeCells.Empty();
	CurrentAbilityPreviewRangeSet.Empty();

	for (const FIntPoint& Coord : OldHighlightedCoords)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.X, Coord.Y);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}
}

void AHexGridActor::UpdateSelectedUnitWidget()
{
	const bool bHasSelectedAliveUnit = IsValid(SelectedUnit) && !SelectedUnit->GetIsDead();
	ApplyHexBorderGlow(bHasSelectedAliveUnit);

	if (!ActionPointsWidget)
	{
		return;
	}

	auto SetWidgetVisibilityByName = [this](const FName& WidgetName, ESlateVisibility Visibility) -> bool
		{
			if (UWidget* Widget = ActionPointsWidget->GetWidgetFromName(WidgetName))
			{
				Widget->SetVisibility(Visibility);
				return true;
			}

			return false;
		};

	const bool bHasUnit = IsPlayerInputAllowed() && IsValid(SelectedUnit) && !SelectedUnit->GetIsDead();

	if (UWidget* PanelRoot = ActionPointsWidget->GetWidgetFromName(UnitPanelRootName))
	{
		PanelRoot->SetVisibility(bHasUnit ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (bHasUnit)
		{
			const bool bSelectedPlayerUnit = SelectedUnit->Team == EHexUnitTeam::Player;
			const bool bSelectedEnemyUnit = SelectedUnit->Team == EHexUnitTeam::Enemy;
			const bool bSelectedUnitHasChampionAbility = SelectedUnit->CanUseChampionAbility();

			FVector2D DesiredPanelSize = SelectedUnitPanelNormalSize;

			if (bSelectedPlayerUnit)
			{
				DesiredPanelSize = bSelectedUnitHasChampionAbility
					? SelectedUnitPanelChampionSize
					: SelectedUnitPanelNormalSize;
			}
			else if (bSelectedEnemyUnit)
			{
				DesiredPanelSize = bSelectedUnitHasChampionAbility
					? SelectedEnemyUnitPanelChampionSize
					: SelectedEnemyUnitPanelNormalSize;
			}

			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PanelRoot->Slot))
			{
				CanvasSlot->SetSize(DesiredPanelSize);
			}
		}
	}

	if (!bHasUnit)
	{
		SetWidgetVisibilityByName(UnitMoveButtonContainerName, ESlateVisibility::Collapsed);
		SetWidgetVisibilityByName(UnitAttackButtonContainerName, ESlateVisibility::Collapsed);
		SetWidgetVisibilityByName(UnitAbilityButtonContainerName, ESlateVisibility::Collapsed);
		SetWidgetVisibilityByName(UnitAbilityCooldownColumnName, ESlateVisibility::Collapsed);
		SetWidgetVisibilityByName(UnitAbilityDescriptionContainerName, ESlateVisibility::Collapsed);
		SetWidgetVisibilityByName(UnitFactionContainerName, ESlateVisibility::Collapsed);

		if (UWidget* FactionTextWidget = ActionPointsWidget->GetWidgetFromName(UnitFactionTextBlockName))
		{
			FactionTextWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UWidget* CooldownTextWidget = ActionPointsWidget->GetWidgetFromName(UnitAbilityCooldownTextBlockName))
		{
			CooldownTextWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UWidget* DescriptionTextWidget = ActionPointsWidget->GetWidgetFromName(UnitAbilityDescriptionTextName))
		{
			DescriptionTextWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UWidget* MoveButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitMoveButtonName))
		{
			MoveButtonWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UWidget* AttackButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitAttackButtonName))
		{
			AttackButtonWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UWidget* AbilityButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonName))
		{
			AbilityButtonWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		return;
	}

	if (UTextBlock* NameText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitNameTextBlockName)))
	{
		const FString DisplayName = SelectedUnit->UnitDisplayName.IsEmpty()
			? SelectedUnit->GetName()
			: SelectedUnit->UnitDisplayName;

		NameText->SetText(FText::FromString(DisplayName));
	}

	SetWidgetVisibilityByName(UnitFactionContainerName, ESlateVisibility::Visible);

	if (UWidget* FactionTextWidget = ActionPointsWidget->GetWidgetFromName(UnitFactionTextBlockName))
	{
		FactionTextWidget->SetVisibility(ESlateVisibility::Visible);
	}

	if (UTextBlock* FactionText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitFactionTextBlockName)))
	{
		FactionText->SetText(
			FText::FromString(FString::Printf(TEXT("Faction: %s"), *SelectedUnit->GetFactionDisplayText().ToString()))
		);
	}

	//                                               Border.
	//   UMG Content Color and Opacity                                       ,
	//              Border      -               ,                            .
	if (UBorder* PortraitFrame = Cast<UBorder>(ActionPointsWidget->GetWidgetFromName(UnitPortraitFrameName)))
	{
		PortraitFrame->SetContentColorAndOpacity(FLinearColor::White);
		PortraitFrame->SetRenderOpacity(1.0f);
	}

	if (UImage* PortraitImage = Cast<UImage>(ActionPointsWidget->GetWidgetFromName(UnitPortraitImageName)))
	{
		PortraitImage->SetColorAndOpacity(FLinearColor::White);
		PortraitImage->SetRenderOpacity(1.0f);

		if (SelectedUnit->UnitPortrait)
		{
			PortraitImage->SetBrushFromTexture(SelectedUnit->UnitPortrait, true);
			PortraitImage->SetColorAndOpacity(FLinearColor::White);
			PortraitImage->SetRenderOpacity(1.0f);
			PortraitImage->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			PortraitImage->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	if (UTextBlock* HealthText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitHealthTextBlockName)))
	{
		HealthText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d HP"), SelectedUnit->CurrentHealth, SelectedUnit->MaxHealth)));
	}

	if (UProgressBar* HealthBar = Cast<UProgressBar>(ActionPointsWidget->GetWidgetFromName(UnitHealthProgressBarName)))
	{
		HealthBar->SetPercent(SelectedUnit->GetHealthPercent());
	}

	if (UTextBlock* DamageText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitDamageTextBlockName)))
	{
		DamageText->SetText(FText::FromString(FString::Printf(TEXT("Damage: %d"), SelectedUnit->AttackDamage)));
	}

	if (UTextBlock* AttackRangeText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitAttackRangeTextBlockName)))
	{
		AttackRangeText->SetText(FText::FromString(FString::Printf(TEXT("Attack range: %d"), SelectedUnit->AttackRange)));
	}

	if (UTextBlock* MoveRangeText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitMoveRangeTextBlockName)))
	{
		const int32 RemainingMovementRange = GetRemainingMovementRangeForUnit(SelectedUnit);
		MoveRangeText->SetText(FText::FromString(FString::Printf(TEXT("Movement: %d/%d"), RemainingMovementRange, SelectedUnit->MovementRange)));
	}

	if (UTextBlock* OccupiedSlotsText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitOccupiedSlotsTextBlockName)))
	{
		OccupiedSlotsText->SetText(FText::FromString(FString::Printf(TEXT("Unit Size: %d"), SelectedUnit->OccupiedSlots)));
	}

	const bool bSelectedUnitHasChampionAbilityForStats = SelectedUnit->CanUseChampionAbility();
	const ESlateVisibility CooldownColumnVisibility = bSelectedUnitHasChampionAbilityForStats
		? ESlateVisibility::Visible
		: ESlateVisibility::Collapsed;

	SetWidgetVisibilityByName(UnitAbilityCooldownColumnName, CooldownColumnVisibility);

	if (UWidget* CooldownTextWidget = ActionPointsWidget->GetWidgetFromName(UnitAbilityCooldownTextBlockName))
	{
		CooldownTextWidget->SetVisibility(CooldownColumnVisibility);
	}

	if (UTextBlock* AbilityCooldownText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitAbilityCooldownTextBlockName)))
	{
		if (bSelectedUnitHasChampionAbilityForStats)
		{
			const int32 RemainingCooldown = SelectedUnit->GetRemainingChampionAbilityCooldownTurns();
			const int32 MaxCooldown = FMath::Max(0, SelectedUnit->ChampionAbilityCooldownTurns);

			AbilityCooldownText->SetText(
				FText::FromString(FString::Printf(TEXT("Cooldown: %d/%d"), RemainingCooldown, MaxCooldown))
			);
		}
		else
		{
			AbilityCooldownText->SetText(FText::GetEmpty());
		}
	}

	const bool bSelectedPlayerUnit = SelectedUnit->Team == EHexUnitTeam::Player;
	const bool bSelectedEnemyUnit = SelectedUnit->Team == EHexUnitTeam::Enemy;

	const ESlateVisibility PlayerActionVisibility = bSelectedPlayerUnit
		? ESlateVisibility::Visible
		: ESlateVisibility::Collapsed;

	const bool bSelectedUnitHasChampionAbility = SelectedUnit->CanUseChampionAbility();
	const bool bSelectedUnitCanActivateChampionAbility = bSelectedUnitHasChampionAbility && SelectedUnit->CanActivateChampionAbility();

	const ESlateVisibility AbilityVisibility = ((bSelectedEnemyUnit && bSelectedUnitHasChampionAbility) || (bSelectedPlayerUnit && bSelectedUnitHasChampionAbility))
		? ESlateVisibility::Visible
		: ESlateVisibility::Collapsed;

	SetWidgetVisibilityByName(UnitMoveButtonContainerName, PlayerActionVisibility);
	if (UWidget* MoveButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitMoveButtonName))
	{
		MoveButtonWidget->SetVisibility(PlayerActionVisibility);
	}

	SetWidgetVisibilityByName(UnitAttackButtonContainerName, PlayerActionVisibility);
	if (UWidget* AttackButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitAttackButtonName))
	{
		AttackButtonWidget->SetVisibility(PlayerActionVisibility);
	}

	SetWidgetVisibilityByName(UnitAbilityButtonContainerName, AbilityVisibility);
	if (UWidget* AbilityButtonWidget = ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonName))
	{
		AbilityButtonWidget->SetVisibility(AbilityVisibility);
	}

	if (UTextBlock* MoveText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitMoveButtonTextName)))
	{
		MoveText->SetText(BuildHotkeyButtonText(TEXT("Q"), FText::FromString(TEXT("MOVE"))));
	}

	if (UTextBlock* AttackText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitAttackButtonTextName)))
	{
		AttackText->SetText(BuildHotkeyButtonText(TEXT("W"), FText::FromString(TEXT("ATTACK"))));
	}

	if (UButton* AbilityButton = Cast<UButton>(ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonName)))
	{
		AbilityButton->SetIsEnabled(
			(bSelectedEnemyUnit && bSelectedUnitHasChampionAbility) ||
			(
				bSelectedPlayerUnit &&
				SelectedUnit->CanAct() &&
				bSelectedUnitCanActivateChampionAbility &&
				HasEnoughActionPoints(CalculateChampionAbilityActionPointCost(SelectedUnit))
				)
		);
	}

	if (UTextBlock* AbilityText = Cast<UTextBlock>(ActionPointsWidget->GetWidgetFromName(UnitAbilityButtonTextName)))
	{
		const FText AbilityButtonText = bSelectedUnitHasChampionAbility
			? SelectedUnit->ChampionAbilityDisplayName
			: FText::FromString(TEXT("ABILITY"));

		AbilityText->SetText(BuildHotkeyButtonText(TEXT("E"), AbilityButtonText));
	}

	if (!bAbilityButtonHovered && !bAbilityButtonPressed)
	{
		ClearAbilityDescriptionTooltip();
	}
}

void AHexGridActor::ClearSelectionAndHighlights()
{
	StopAbilityPreviewAndTooltip(true);

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();
}

void AHexGridActor::AwardKillActionPointBonus(AHexUnitActor* Killer, AHexUnitActor* KilledUnit)
{
	if (!bEnableKillActionPointBonus)
	{
		return;
	}

	if (!IsValid(Killer) || !IsValid(KilledUnit))
	{
		return;
	}

	if (!Killer->IsEnemyFor(KilledUnit))
	{
		return;
	}

	//                                                 .
	if (KilledUnit->ShouldSkipKillActionPointBonus())
	{
		UE_LOG(LogTemp, Log, TEXT("Kill AP bonus skipped: killed unit is summoned. Killer=%s Killed=%s"),
			*GetNameSafe(Killer),
			*GetNameSafe(KilledUnit)
		);
		return;
	}

	const int32 SafeBonus = FMath::Max(0, KillActionPointBonus);
	if (SafeBonus <= 0)
	{
		return;
	}

	bool* BonusGrantedFlag = nullptr;
	if (Killer->Team == EHexUnitTeam::Player)
	{
		BonusGrantedFlag = &bPlayerKillActionPointBonusGrantedThisTurn;
	}
	else if (Killer->Team == EHexUnitTeam::Enemy)
	{
		BonusGrantedFlag = &bEnemyKillActionPointBonusGrantedThisTurn;
	}

	if (!BonusGrantedFlag)
	{
		return;
	}

	if (bLimitKillActionPointBonusOncePerTurn && *BonusGrantedFlag)
	{
		UE_LOG(LogTemp, Log, TEXT("Kill AP bonus skipped: bonus already granted this turn. Killer=%s Killed=%s"),
			*GetNameSafe(Killer),
			*GetNameSafe(KilledUnit)
		);
		return;
	}

	const int32 PointsBefore = CurrentActionPoints;
	AddActionPoints(SafeBonus);
	*BonusGrantedFlag = true;

	UE_LOG(LogTemp, Log, TEXT("Kill AP bonus: Killer=%s Killed=%s Bonus=%d AP=%d->%d/%d"),
		*GetNameSafe(Killer),
		*GetNameSafe(KilledUnit),
		SafeBonus,
		PointsBefore,
		CurrentActionPoints,
		MaxActionPoints
	);
}

void AHexGridActor::UpdateSummonedUnitLifetimeForTurnOwner(EHexTurnOwner TurnOwner)
{
	const EHexUnitTeam OwnerTeam = TurnOwner == EHexTurnOwner::Player
		? EHexUnitTeam::Player
		: EHexUnitTeam::Enemy;

	TArray<AHexUnitActor*> UnitsToExpire;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || !Unit->bIsSummonedUnit || Unit->Team != OwnerTeam)
		{
			continue;
		}

		if (Unit->ConsumeSummonedOwnerTurn())
		{
			UnitsToExpire.Add(Unit);
		}
	}

	for (AHexUnitActor* Unit : UnitsToExpire)
	{
		if (!IsValid(Unit) || Unit->GetIsDead())
		{
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("Summoned unit lifetime expired. Unit=%s Team=%s"),
			*GetNameSafe(Unit),
			OwnerTeam == EHexUnitTeam::Player ? TEXT("Player") : TEXT("Enemy")
		);

		Unit->Die();
	}
}

void AHexGridActor::UpdateLastStandDurationForTurnOwner(EHexTurnOwner TurnOwner)
{
	const EHexUnitTeam OwnerTeam = TurnOwner == EHexTurnOwner::Player
		? EHexUnitTeam::Player
		: EHexUnitTeam::Enemy;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != OwnerTeam)
		{
			continue;
		}

		Unit->ReduceLastStandDurationAtOwnerTurnStart();
	}
}

void AHexGridActor::UpdateMarkedForDeathDurationForTurnOwner(EHexTurnOwner TurnOwner)
{
	const EHexUnitTeam OwnerTeam = TurnOwner == EHexTurnOwner::Player
		? EHexUnitTeam::Player
		: EHexUnitTeam::Enemy;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != OwnerTeam)
		{
			continue;
		}

		Unit->ReduceMarkedForDeathDurationAtOwnerTurnStart();
	}
}

void AHexGridActor::UpdateChampionAbilityCooldownForTurnOwner(EHexTurnOwner TurnOwner)
{
	const EHexUnitTeam OwnerTeam = TurnOwner == EHexTurnOwner::Player
		? EHexUnitTeam::Player
		: EHexUnitTeam::Enemy;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != OwnerTeam)
		{
			continue;
		}

		Unit->ReduceChampionAbilityCooldownAtOwnerTurnStart();
	}
}

void AHexGridActor::ShowChampionAbilityUnavailableWarning(AHexUnitActor* Champion)
{
	if (!IsValid(Champion))
	{
		return;
	}

	if (Champion->IsChampionAbilityOnCooldown())
	{
		ShowActionWarning(
			ChampionAbilityOnCooldownTitle,
			FText::FromString(FString::Printf(TEXT("Available in %d owner turn(s)."), Champion->GetRemainingChampionAbilityCooldownTurns()))
		);
		return;
	}

	ShowActionWarning(
		ChampionAbilityAlreadyActiveTitle,
		ChampionAbilityAlreadyActiveSubText
	);
}

bool AHexGridActor::HasAnyAffordablePlayerHealAction() const
{
	if (!IsPlayerTurn())
	{
		return false;
	}

	for (const TPair<FIntPoint, AHexUnitActor*>& HealerPair : UnitsByCoord)
	{
		AHexUnitActor* Healer = HealerPair.Value;
		if (!IsValid(Healer) || Healer->GetIsDead() || Healer->Team != EHexUnitTeam::Player || !Healer->CanAct() || !Healer->CanHeal())
		{
			continue;
		}

		const int32 HealCost = CalculateHealActionPointCost(Healer);
		if (!HasEnoughActionPoints(HealCost))
		{
			continue;
		}

		const FIntPoint HealerCoord = Healer->GetGridCoord();
		const int32 HealRange = FMath::Max(1, Healer->AttackRange);

		for (const TPair<FIntPoint, AHexUnitActor*>& TargetPair : UnitsByCoord)
		{
			AHexUnitActor* Target = TargetPair.Value;
			if (!Healer->CanHealTarget(Target))
			{
				continue;
			}

			const FIntPoint TargetCoord = Target->GetGridCoord();
			const int32 Distance = GetHexDistance(HealerCoord.X, HealerCoord.Y, TargetCoord.X, TargetCoord.Y);
			if (Distance > 0 && Distance <= HealRange)
			{
				return true;
			}
		}
	}

	return false;
}

bool AHexGridActor::HasAnyAffordablePlayerSummonAction() const
{
	if (!IsPlayerTurn())
	{
		return false;
	}

	for (const TPair<FIntPoint, AHexUnitActor*>& SummonerPair : UnitsByCoord)
	{
		AHexUnitActor* Summoner = SummonerPair.Value;
		if (!IsValid(Summoner) || Summoner->GetIsDead() || Summoner->Team != EHexUnitTeam::Player || !Summoner->CanAct() || !Summoner->CanSummon())
		{
			continue;
		}

		if (Summoner->IsChampionAbilitySummon() && !Summoner->CanActivateChampionAbility())
		{
			continue;
		}

		const int32 SummonCost = CalculateSummonActionPointCost(Summoner);
		if (!HasEnoughActionPoints(SummonCost))
		{
			continue;
		}

		const FIntPoint SummonerCoord = Summoner->GetGridCoord();
		const int32 SafeSummonRange = Summoner->GetSummonRange();

		for (const FHexCell& Cell : Cells)
		{
			if (IsCellOccupied(Cell.Q, Cell.R))
			{
				continue;
			}

			const int32 Distance = GetHexDistance(SummonerCoord.X, SummonerCoord.Y, Cell.Q, Cell.R);
			if (Distance > 0 && Distance <= SafeSummonRange)
			{
				return true;
			}
		}
	}

	return false;
}

bool AHexGridActor::HasAnyAffordablePlayerChampionAbilityAction() const
{
	if (!IsPlayerTurn())
	{
		return false;
	}

	for (const TPair<FIntPoint, AHexUnitActor*>& UnitPair : UnitsByCoord)
	{
		AHexUnitActor* Champion = UnitPair.Value;
		if (!IsValid(Champion) || Champion->GetIsDead() || Champion->Team != EHexUnitTeam::Player || !Champion->CanAct() || !Champion->CanActivateChampionAbility())
		{
			continue;
		}

		// ChampionAbilityType = Summon                 HasAnyAffordablePlayerSummonAction(),
		//                                                                              .
		if (Champion->IsChampionAbilitySummon())
		{
			continue;
		}

		const int32 AbilityCost = CalculateChampionAbilityActionPointCost(Champion);
		if (HasEnoughActionPoints(AbilityCost))
		{
			return true;
		}
	}

	return false;
}

bool AHexGridActor::ShouldAutoEndPlayerTurn() const
{
	if (bMatchFinished)
	{
		return false;
	}

	if (!bAutoEndPlayerTurnWhenActionPointsEmpty || !IsPlayerTurn() || bEnemyTurnInProgress)
	{
		return false;
	}

	//                      :
	//                                1,                           0,
	//                                    ,             ,          ,             ,                               .
	const int32 MinimalMoveCost = FMath::Max(0, MoveActionPointCost);
	const int32 AttackCost = CalculateAttackActionPointCost();

	const bool bCanAffordMove = MinimalMoveCost <= 0 || HasEnoughActionPoints(MinimalMoveCost);

	bool bHasPlayerUnitWithUnusedAttack = false;
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (IsValid(Unit) && !Unit->GetIsDead() && Unit->Team == EHexUnitTeam::Player && Unit->CanAct() && CanUnitAttackThisTurn(Unit))
		{
			bHasPlayerUnitWithUnusedAttack = true;
			break;
		}
	}

	const bool bCanAffordAttack = bHasPlayerUnitWithUnusedAttack && (AttackCost <= 0 || HasEnoughActionPoints(AttackCost));
	const bool bCanAffordHeal = HasAnyAffordablePlayerHealAction();
	const bool bCanAffordSummon = HasAnyAffordablePlayerSummonAction();
	const bool bCanAffordChampionAbility = HasAnyAffordablePlayerChampionAbilityAction();

	return !bCanAffordMove && !bCanAffordAttack && !bCanAffordHeal && !bCanAffordSummon && !bCanAffordChampionAbility;
}

void AHexGridActor::ScheduleAutoEndPlayerTurnAfterAction(AHexUnitActor* ActingUnit, bool bWaitForMovement)
{
	if (!ShouldAutoEndPlayerTurn())
	{
		return;
	}

	ClearSelectionAndHighlights();

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AutoEndPlayerTurnTimerHandle);
		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
	}

	if (IsValid(PlayerAutoEndMovingUnit))
	{
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
		PlayerAutoEndMovingUnit = nullptr;
	}

	if (bWaitForMovement && IsValid(ActingUnit) && ActingUnit->bIsMoving)
	{
		PlayerAutoEndMovingUnit = ActingUnit;
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
		PlayerAutoEndMovingUnit->OnMovementFinished.AddDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);

		UE_LOG(LogTemp, Log, TEXT("Player AP is empty. Enemy turn will start after movement finishes."));
		return;
	}

	if (!GetWorld())
	{
		HandleDelayedAutoEndPlayerTurn();
		return;
	}

	if (AutoEndPlayerTurnAfterAttackDelay <= 0.0f)
	{
		FTimerDelegate AutoEndDelegate;
		AutoEndDelegate.BindUObject(this, &AHexGridActor::HandleDelayedAutoEndPlayerTurn);
		GetWorldTimerManager().SetTimerForNextTick(AutoEndDelegate);
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			AutoEndPlayerTurnTimerHandle,
			this,
			&AHexGridActor::HandleDelayedAutoEndPlayerTurn,
			AutoEndPlayerTurnAfterAttackDelay,
			false
		);
	}

	UE_LOG(LogTemp, Log, TEXT("Player AP is empty. Enemy turn scheduled."));
}

void AHexGridActor::HandlePlayerAutoEndMovementFinished()
{
	if (IsValid(PlayerAutoEndMovingUnit))
	{
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
	}
	PlayerAutoEndMovingUnit = nullptr;

	if (!ShouldAutoEndPlayerTurn())
	{
		return;
	}

	if (!GetWorld())
	{
		HandleDelayedAutoEndPlayerTurn();
		return;
	}

	FTimerDelegate AutoEndDelegate;
	AutoEndDelegate.BindUObject(this, &AHexGridActor::HandleDelayedAutoEndPlayerTurn);
	GetWorldTimerManager().SetTimerForNextTick(AutoEndDelegate);
}

void AHexGridActor::HandleDelayedAutoEndPlayerTurn()
{
	if (!ShouldAutoEndPlayerTurn())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Player AP is empty. Auto-ending player turn."));
	EndPlayerTurn();
}

void AHexGridActor::StartPlayerTurn()
{
	StartTurnWithBanner(EHexTurnOwner::Player);
}

void AHexGridActor::BeginPlayerTurnAfterBanner()
{
	ApplyBattleInputMode();

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AutoEndPlayerTurnTimerHandle);
		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
	}

	if (IsValid(PlayerAutoEndMovingUnit))
	{
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
	}

	CurrentTurnOwner = EHexTurnOwner::Player;
	ResetMovementRangeForTurnOwner(EHexTurnOwner::Player);
	ResetAttackUsageForTurnOwner(EHexTurnOwner::Player);
	UpdateLastStandDurationForTurnOwner(EHexTurnOwner::Player);
	UpdateMarkedForDeathDurationForTurnOwner(EHexTurnOwner::Player);
	UpdateChampionAbilityCooldownForTurnOwner(EHexTurnOwner::Player);
	bEnemyTurnInProgress = false;
	bPlayerTurnEnding = false;
	bPlayerKillActionPointBonusGrantedThisTurn = false;
	BotMovingUnit = nullptr;
	PlayerAutoEndMovingUnit = nullptr;
	StartBattleTimerIfNeeded();
	EnemyBotUnits.Empty();
	EnemyBotUnitIndex = 0;
	EnemyBotMovesDoneThisTurn = 0;
	EnemyBotBusyRetriesDone = 0;

	StartPlayerTurnTimer();
	ClearSelectionAndHighlights();
	ResetActionPoints();
	UpdateActionPointsWidget();
	OnTurnChanged.Broadcast(CurrentTurnOwner);

	UE_LOG(LogTemp, Log, TEXT("Player turn started. AP=%d/%d Time=%.1f"), CurrentActionPoints, MaxActionPoints, PlayerTurnTimeRemaining);
}

void AHexGridActor::EndPlayerTurn()
{
	if (!IsPlayerTurn() || IsTurnIntroInProgress())
	{
		return;
	}

	bPlayerTurnEnding = true;
	StopPlayerTurnTimer(true);
	ClearSelectionAndHighlights();
	UpdateActionPointsWidget();

	//              :                     ,                                      ,
	//                     action-lock.                                           
	//                         .          , bPlayerTurnEnding                            .
	if (bAttackProjectileInProgress || (bWaitForPlayerActionsBeforeEnemyTurn && HasBusyPlayerUnit()))
	{
		UE_LOG(LogTemp, Log, TEXT("EndPlayerTurn delayed: player unit is still busy."));

		if (!GetWorld())
		{
			return;
		}

		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
		GetWorldTimerManager().SetTimer(
			PlayerTurnEndBusyRetryTimerHandle,
			this,
			&AHexGridActor::EndPlayerTurn,
			FMath::Max(0.01f, PlayerTurnEndBusyRetryDelay),
			false
		);

		return;
	}

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AutoEndPlayerTurnTimerHandle);
		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
	}

	if (IsValid(PlayerAutoEndMovingUnit))
	{
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
	}
	PlayerAutoEndMovingUnit = nullptr;

	UpdateSummonedUnitLifetimeForTurnOwner(EHexTurnOwner::Player);
	StartEnemyTurn();
}

void AHexGridActor::StartEnemyTurn()
{
	StartTurnWithBanner(EHexTurnOwner::Enemy);
}

void AHexGridActor::BeginEnemyTurnAfterBanner()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AutoEndPlayerTurnTimerHandle);
		GetWorldTimerManager().ClearTimer(PlayerTurnEndBusyRetryTimerHandle);
	}

	if (IsValid(PlayerAutoEndMovingUnit))
	{
		PlayerAutoEndMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandlePlayerAutoEndMovementFinished);
	}

	StopPlayerTurnTimer(true);

	CurrentTurnOwner = EHexTurnOwner::Enemy;
	ResetMovementRangeForTurnOwner(EHexTurnOwner::Enemy);
	ResetAttackUsageForTurnOwner(EHexTurnOwner::Enemy);
	UpdateLastStandDurationForTurnOwner(EHexTurnOwner::Enemy);
	UpdateMarkedForDeathDurationForTurnOwner(EHexTurnOwner::Enemy);
	UpdateChampionAbilityCooldownForTurnOwner(EHexTurnOwner::Enemy);
	bEnemyTurnInProgress = true;
	bPlayerTurnEnding = false;
	bEnemyKillActionPointBonusGrantedThisTurn = false;
	BotMovingUnit = nullptr;
	PlayerAutoEndMovingUnit = nullptr;
	StartBattleTimerIfNeeded();
	EnemyBotUnits.Empty();
	EnemyBotUnitIndex = 0;
	EnemyBotMovesDoneThisTurn = 0;
	EnemyBotBusyRetriesDone = 0;
	EnemyBotActionCountThisTurn.Empty();
	EnemyBotPlannedFocusTarget.Reset();
	EnemyBotPlanTurnsRemaining = 0;
	EnemyBotPlannedHorizon = GetEnemyBotPlanningDepth();
	EnemyBotPlannedFrontCell = FIntPoint(0, 0);
	RefreshEnemyBotPlan(true);

	ClearSelectionAndHighlights();
	ResetActionPoints();
	UpdateActionPointsWidget();
	OnTurnChanged.Broadcast(CurrentTurnOwner);

	UE_LOG(LogTemp, Log, TEXT("Enemy turn started. AP=%d/%d"), CurrentActionPoints, MaxActionPoints);

	if (!bEnableEnemyBot)
	{
		FinishEnemyTurn();
		return;
	}

	if (GetWorld())
	{
		FTimerDelegate BotTurnDelegate;
		BotTurnDelegate.BindUObject(this, &AHexGridActor::RunEnemyBotTurn);
		GetWorldTimerManager().SetTimerForNextTick(BotTurnDelegate);
	}
}

void AHexGridActor::FinishEnemyTurn()
{
	if (!IsEnemyTurn())
	{
		return;
	}

	if (IsValid(BotMovingUnit))
	{
		BotMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::ContinueEnemyBotTurn);
	}

	BotMovingUnit = nullptr;

	UpdateSummonedUnitLifetimeForTurnOwner(EHexTurnOwner::Enemy);

	bEnemyTurnInProgress = false;
	StartPlayerTurn();
}

void AHexGridActor::CollectAliveEnemyUnits(TArray<AHexUnitActor*>& OutUnits) const
{
	OutUnits.Empty();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy)
		{
			continue;
		}

		OutUnits.Add(Unit);
	}

	SortEnemyBotUnitsByTacticalPriority(OutUnits);
}

void AHexGridActor::CollectAlivePlayerUnits(TArray<AHexUnitActor*>& OutUnits) const
{
	OutUnits.Empty();

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (IsValid(Unit) && !Unit->GetIsDead() && Unit->Team == EHexUnitTeam::Player)
		{
			OutUnits.Add(Unit);
		}
	}
}

bool AHexGridActor::HasBusyPlayerUnit() const
{
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (IsValid(Unit) && !Unit->GetIsDead() && Unit->Team == EHexUnitTeam::Player && !Unit->CanAct())
		{
			return true;
		}
	}

	return false;
}

bool AHexGridActor::HasBusyEnemyBotUnit() const
{
	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (IsValid(Unit) && !Unit->GetIsDead() && Unit->Team == EHexUnitTeam::Enemy && !Unit->CanAct())
		{
			return true;
		}
	}

	return false;
}

void AHexGridActor::ScheduleEnemyBotRetryAfterBusyUnit()
{
	if (!IsEnemyTurn() || !bEnemyTurnInProgress)
	{
		return;
	}

	EnemyBotBusyRetriesDone++;

	UE_LOG(LogTemp, Log, TEXT("Enemy bot waits for busy unit. Retry=%d/%d"),
		EnemyBotBusyRetriesDone,
		FMath::Max(0, EnemyBotMaxBusyRetries)
	);

	if (!GetWorld() || EnemyBotBusyRetryDelay <= 0.0f)
	{
		RunEnemyBotTurn();
		return;
	}

	FTimerDelegate RetryDelegate;
	RetryDelegate.BindUObject(this, &AHexGridActor::RunEnemyBotTurn);
	FTimerHandle RetryTimerHandle;
	GetWorldTimerManager().SetTimer(
		RetryTimerHandle,
		RetryDelegate,
		EnemyBotBusyRetryDelay,
		false
	);
}

void AHexGridActor::RunEnemyBotTurn()
{
	if (bMatchFinished)
	{
		return;
	}

	if (!IsEnemyTurn() || !bEnemyTurnInProgress || IsTurnIntroInProgress())
	{
		return;
	}

	if (CurrentActionPoints <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy bot turn finished: no action points left."));
		FinishEnemyTurn();
		return;
	}

	if ((HasBusyEnemyBotUnit() || HasBusyPlayerUnit()) && EnemyBotBusyRetriesDone < FMath::Max(0, EnemyBotMaxBusyRetries))
	{
		ScheduleEnemyBotRetryAfterBusyUnit();
		return;
	}

	if (EnemyBotMovesDoneThisTurn >= FMath::Max(1, EnemyBotMaxMovesPerTurn))
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy bot turn stopped by EnemyBotMaxMovesPerTurn=%d. Check action costs if AP does not decrease."), EnemyBotMaxMovesPerTurn);
		FinishEnemyTurn();
		return;
	}

	RefreshEnemyBotPlan(false);

	// Rebuild and re-sort after EVERY action. The previous round-robin behavior gave a huge
	// priority bonus to units that had not acted yet, so the bot often did move(A), move(B)
	// even when A had just entered attack range. The planner below chooses the best action
	// for the whole army at the current board state instead.
	CollectAliveEnemyUnits(EnemyBotUnits);
	EnemyBotUnitIndex = 0;

	if (EnemyBotUnits.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy bot turn finished: no alive enemies."));
		FinishEnemyTurn();
		return;
	}

	auto FindBestImmediateAttacker = [this](bool bKillsOnly) -> AHexUnitActor*
	{
		AHexUnitActor* BestAttacker = nullptr;
		float BestScore = -1000000000.0f;

		for (AHexUnitActor* Unit : EnemyBotUnits)
		{
			if (!IsValid(Unit) || Unit->GetIsDead() || !Unit->CanAct() || !CanUnitAttackThisTurn(Unit) ||
				!HasEnoughActionPoints(CalculateAttackActionPointCost()))
			{
				continue;
			}

			AHexUnitActor* Target = FindBestEnemyBotAttackTarget(Unit);
			if (!IsValid(Target))
			{
				continue;
			}

			const bool bCanKill = Target->GetModifiedIncomingDamage(Unit->AttackDamage) >= Target->CurrentHealth;
			if (bKillsOnly && !bCanKill)
			{
				continue;
			}

			float Score = ScoreEnemyBotAttackTarget(Unit, Target);
			if (bCanKill)
			{
				Score += EnemyBotExecuteKillBonus * 2.0f;
			}
			if (EnemyBotPlannedFocusTarget.Get() == Target)
			{
				Score += EnemyBotPlanCommitmentScore;
			}
			// After a unit moved into range, do not punish it so heavily for taking the follow-up attack.
			Score -= static_cast<float>(FMath::Max(0, GetEnemyBotActionCountThisTurn(Unit) - 1)) * EnemyBotRepeatActionPenalty * 0.20f;

			if (Score > BestScore)
			{
				BestScore = Score;
				BestAttacker = Unit;
			}
		}

		return BestAttacker;
	};

	// 1. Never walk away from a free kill.
	if (AHexUnitActor* Killer = FindBestImmediateAttacker(true))
	{
		if (TryEnemyBotAttack(Killer))
		{
			return;
		}
	}

	// 2. Emergency healing can interrupt the attack sequence only when it prevents a likely death
	// or when an important ally is already critically low.
	AHexUnitActor* EmergencyHealer = nullptr;
	float BestEmergencyHealScore = -1000000000.0f;
	for (AHexUnitActor* Unit : EnemyBotUnits)
	{
		if (!IsValid(Unit) || !Unit->CanAct() || !Unit->CanHeal() || !HasEnoughActionPoints(CalculateHealActionPointCost(Unit)))
		{
			continue;
		}

		float HealScore = -1000000000.0f;
		AHexUnitActor* HealTarget = FindBestEnemyBotHealTargetFromCell(Unit, Unit->GetGridCoord(), &HealScore);
		if (!IsValid(HealTarget))
		{
			continue;
		}

		const int32 EffectiveHeal = FMath::Min(FMath::Max(0, Unit->HealAmount), FMath::Max(0, HealTarget->MaxHealth - HealTarget->CurrentHealth));
		const float TargetHp = HealTarget->MaxHealth > 0
			? static_cast<float>(HealTarget->CurrentHealth) / static_cast<float>(HealTarget->MaxHealth)
			: 1.0f;
		const bool bSavesFromLikelyDeath = IsEnemyBotAllyDoomed(HealTarget, 0) && !IsEnemyBotAllyDoomed(HealTarget, EffectiveHeal);
		const bool bCritical = bSavesFromLikelyDeath || TargetHp <= 0.40f;

		if (bCritical && HealScore > BestEmergencyHealScore)
		{
			BestEmergencyHealScore = HealScore;
			EmergencyHealer = Unit;
		}
	}

	if (IsValid(EmergencyHealer) && TryEnemyBotHeal(EmergencyHealer))
	{
		return;
	}

	// 3. PRE-EMPTIVE REINFORCEMENT.
	// If a frontline engagement is forecast to be losing at FULL-health assumptions,
	// spend one action pulling in a free unit that currently has no player in attack range.
	// We still reserve AP for one already-available attack, so this does not turn into
	// "everyone walks and nobody hits".
	{
		AHexUnitActor* ImmediateAttackerToPreserve = FindBestImmediateAttacker(false);
		const int32 ReservedAttackAP = IsValid(ImmediateAttackerToPreserve)
			? CalculateAttackActionPointCost()
			: 0;

		AHexUnitActor* BestHelper = nullptr;
		AHexUnitActor* BestSupportTarget = nullptr;
		FIntPoint BestHelperTargetCoord = FIntPoint::ZeroValue;
		TArray<FHexCoord> BestHelperPath;
		float BestReinforcementPlannerScore = -1000000000.0f;

		for (AHexUnitActor* Unit : EnemyBotUnits)
		{
			if (!IsValid(Unit) || Unit->GetIsDead() || !Unit->CanAct())
			{
				continue;
			}

			// A unit already fighting should keep fighting. Reinforcement duty is for pieces
			// that otherwise have no enemy in their current attack zone.
			if (HasEnemyBotCurrentAttackTarget(Unit))
			{
				continue;
			}

			// One proactive relocation per helper per enemy turn. This prevents a single unit
			// from consuming the whole shared AP pool while other idle helpers remain unused.
			if (GetEnemyBotActionCountThisTurn(Unit) > 0)
			{
				continue;
			}

			float ReinforcementNeed = 0.0f;
			AHexUnitActor* SupportTarget = FindEnemyBotPreemptiveSupportTarget(Unit, &ReinforcementNeed);
			if (!IsValid(SupportTarget) || ReinforcementNeed <= 0.0f)
			{
				continue;
			}

			FIntPoint CandidateTarget;
			TArray<FHexCoord> CandidatePath;
			float CandidateMoveScore = -1000000000.0f;
			if (!FindBestEnemyBotMove(Unit, CandidateTarget, CandidatePath, &CandidateMoveScore) ||
				CandidatePath.IsEmpty())
			{
				continue;
			}

			const FIntPoint UnitCoord = Unit->GetGridCoord();
			const FIntPoint SupportCoord = SupportTarget->GetGridCoord();
			const int32 CurrentDistanceToSupport = GetHexDistance(
				UnitCoord.X, UnitCoord.Y,
				SupportCoord.X, SupportCoord.Y
			);
			const int32 CandidateDistanceToSupport = GetHexDistance(
				CandidateTarget.X, CandidateTarget.Y,
				SupportCoord.X, SupportCoord.Y
			);

			// Do not label an unrelated movement as "reinforcement".
			if (CandidateDistanceToSupport >= CurrentDistanceToSupport)
			{
				continue;
			}

			const int32 MoveCost = CalculateMoveActionPointCost(CandidatePath.Num());
			if (CurrentActionPoints < MoveCost + ReservedAttackAP)
			{
				continue;
			}

			const int32 ClosedDistance = CurrentDistanceToSupport - CandidateDistanceToSupport;
			float PlannerScore = CandidateMoveScore + ReinforcementNeed;
			PlannerScore += static_cast<float>(ClosedDistance) * EnemyBotPreemptiveReinforcementMoveScore;

			if (CandidateDistanceToSupport <= FMath::Max(1, EnemyBotReinforcementArrivalRange))
			{
				PlannerScore += EnemyBotPreemptiveReinforcementArrivalScore;
			}

			if (PlannerScore > BestReinforcementPlannerScore)
			{
				BestReinforcementPlannerScore = PlannerScore;
				BestHelper = Unit;
				BestSupportTarget = SupportTarget;
				BestHelperTargetCoord = CandidateTarget;
				BestHelperPath = CandidatePath;
			}
		}

		if (IsValid(BestHelper) && IsValid(BestSupportTarget) && !BestHelperPath.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT(
				"Enemy bot pre-emptive reinforcement: Helper=%s Support=%s Score=%.1f AP=%d"),
				*GetNameSafe(BestHelper),
				*GetNameSafe(BestSupportTarget),
				BestReinforcementPlannerScore,
				CurrentActionPoints
			);

			if (ExecuteEnemyBotMove(BestHelper, BestHelperTargetCoord, BestHelperPath))
			{
				return;
			}
		}
	}

	// 4. If ANY unit can attack now, attack now. The move->attack rule still applies,
	// but only after the AI has secured a forecasted losing flank with an idle helper.
	if (AHexUnitActor* Attacker = FindBestImmediateAttacker(false))
	{
		if (TryEnemyBotAttack(Attacker))
		{
			return;
		}
	}

	// 5. With no current attack, use a champion ability only if its existing tactical rules approve it.
	for (AHexUnitActor* Unit : EnemyBotUnits)
	{
		if (TryEnemyBotChampionAbility(Unit))
		{
			return;
		}
	}

	// 6. Useful normal healing before spending AP on another positional move.
	AHexUnitActor* BestHealer = nullptr;
	float BestHealScore = -1000000000.0f;
	for (AHexUnitActor* Unit : EnemyBotUnits)
	{
		if (!IsValid(Unit) || !Unit->CanAct() || !Unit->CanHeal() || !HasEnoughActionPoints(CalculateHealActionPointCost(Unit)))
		{
			continue;
		}

		float HealScore = -1000000000.0f;
		if (IsValid(FindBestEnemyBotHealTargetFromCell(Unit, Unit->GetGridCoord(), &HealScore)) && HealScore > BestHealScore)
		{
			BestHealScore = HealScore;
			BestHealer = Unit;
		}
	}

	if (IsValid(BestHealer) && BestHealScore >= 650.0f && TryEnemyBotHeal(BestHealer))
	{
		return;
	}

	// 7. Pick ONE best movement for the whole army. Moves that create an immediate attack and
	// leave enough AP for that attack receive a dominant bonus. This prevents spending the turn
	// moving several pieces while a first mover is already ready to contribute damage.
	AHexUnitActor* BestMover = nullptr;
	FIntPoint BestMoveTarget = FIntPoint::ZeroValue;
	TArray<FHexCoord> BestMovePath;
	float BestMovePlannerScore = -1000000000.0f;

	const bool bApproachPhase = IsEnemyBotArmyInApproachPhase();
	const int32 UnactedUnitsDuringApproach = bApproachPhase ? GetEnemyBotUnactedUnitCount(nullptr) : 0;

	int32 ArmyFrontDistance = MAX_int32;
	if (bApproachPhase)
	{
		for (AHexUnitActor* MarchUnit : EnemyBotUnits)
		{
			if (!IsValid(MarchUnit) || MarchUnit->GetIsDead())
			{
				continue;
			}

			ArmyFrontDistance = FMath::Min(
				ArmyFrontDistance,
				GetNearestPlayerDistanceFromCell(MarchUnit->GetGridCoord())
			);
		}
	}

	for (AHexUnitActor* Unit : EnemyBotUnits)
	{
		if (!IsValid(Unit) || Unit->GetIsDead() || !Unit->CanAct())
		{
			continue;
		}

		// In approach mode, give every available unit one marching action before allowing
		// the same piece to move again. With the shared AP pool this makes the whole army
		// translate forward instead of one or two characters creeping ahead repeatedly.
		if (bApproachPhase &&
			UnactedUnitsDuringApproach > 0 &&
			GetEnemyBotActionCountThisTurn(Unit) > 0)
		{
			continue;
		}

		FIntPoint CandidateTarget;
		TArray<FHexCoord> CandidatePath;
		float CandidateScore = -1000000000.0f;
		if (!FindBestEnemyBotMove(Unit, CandidateTarget, CandidatePath, &CandidateScore))
		{
			continue;
		}

		const int32 MoveCost = CalculateMoveActionPointCost(CandidatePath.Num());
		bool bCanKillAfterMove = false;
		const bool bCreatesAttack = CanEnemyBotAttackAnyPlayerFromCell(Unit, CandidateTarget, &bCanKillAfterMove);
		const bool bCanFollowWithAttack = bCreatesAttack &&
			CanUnitAttackThisTurn(Unit) &&
			CurrentActionPoints >= MoveCost + CalculateAttackActionPointCost();

		float PlannerScore = CandidateScore;

		if (bApproachPhase)
		{
			const int32 CurrentPlayerDistance = GetNearestPlayerDistanceFromCell(Unit->GetGridCoord());
			const int32 CandidatePlayerDistance = GetNearestPlayerDistanceFromCell(CandidateTarget);

			if (CurrentPlayerDistance != MAX_int32 && CandidatePlayerDistance != MAX_int32)
			{
				const int32 AdvanceSteps = CurrentPlayerDistance - CandidatePlayerDistance;
				if (AdvanceSteps > 0)
				{
					PlannerScore += static_cast<float>(AdvanceSteps) * EnemyBotApproachAdvanceScore;
				}
				else
				{
					PlannerScore -= EnemyBotApproachNoProgressPenalty;
				}

				// Move the rear of the formation first. After it catches up, the front can
				// advance without tripping the hard connectivity/depth constraints.
				if (ArmyFrontDistance != MAX_int32)
				{
					const int32 RearLag = FMath::Max(0, CurrentPlayerDistance - ArmyFrontDistance);
					PlannerScore += static_cast<float>(RearLag) * EnemyBotApproachRearPriorityScore;
				}
			}
		}

		if (bCanFollowWithAttack)
		{
			PlannerScore += EnemyBotMoveIntoAttackBonus;
			if (bCanKillAfterMove)
			{
				PlannerScore += EnemyBotExecuteKillBonus;
			}
		}

		const int32 ActionsAlreadyTaken = GetEnemyBotActionCountThisTurn(Unit);
		if (ActionsAlreadyTaken <= 0)
		{
			PlannerScore += EnemyBotFirstActionPriorityBonus * 0.20f;
		}
		else if (!bCanFollowWithAttack)
		{
			PlannerScore -= static_cast<float>(ActionsAlreadyTaken) * EnemyBotRepeatActionPenalty * 0.35f;
		}

		if (PlannerScore > BestMovePlannerScore)
		{
			BestMovePlannerScore = PlannerScore;
			BestMover = Unit;
			BestMoveTarget = CandidateTarget;
			BestMovePath = CandidatePath;
		}
	}

	if (IsValid(BestMover) && BestMovePath.Num() > 0)
	{
		if (ExecuteEnemyBotMove(BestMover, BestMoveTarget, BestMovePath))
		{
			return;
		}
	}

	if (HasBusyEnemyBotUnit() && EnemyBotBusyRetriesDone < FMath::Max(0, EnemyBotMaxBusyRetries))
	{
		ScheduleEnemyBotRetryAfterBusyUnit();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Enemy bot turn finished: no useful global action for remaining AP=%d."), CurrentActionPoints);
	FinishEnemyTurn();
}

void AHexGridActor::ScheduleEnemyBotContinueAfterAction()
{
	if (!IsEnemyTurn() || !bEnemyTurnInProgress)
	{
		return;
	}

	if (!GetWorld())
	{
		RunEnemyBotTurn();
		return;
	}

	const float Delay = FMath::Max(0.0f, EnemyBotStepDelay);

	if (Delay <= 0.0f)
	{
		FTimerDelegate BotTurnDelegate;
		BotTurnDelegate.BindUObject(this, &AHexGridActor::RunEnemyBotTurn);
		GetWorldTimerManager().SetTimerForNextTick(BotTurnDelegate);
		return;
	}

	FTimerHandle DelayHandle;
	GetWorldTimerManager().SetTimer(
		DelayHandle,
		this,
		&AHexGridActor::RunEnemyBotTurn,
		Delay,
		false
	);
}

AHexUnitActor* AHexGridActor::FindBestEnemyBotAttackTarget(AHexUnitActor* EnemyUnit) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return nullptr;
	}

	AHexUnitActor* BestTarget = nullptr;
	float BestScore = -1000000000.0f;

	TArray<AHexUnitActor*> PlayerUnits;
	CollectAlivePlayerUnits(PlayerUnits);

	const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();

	for (AHexUnitActor* PlayerUnit : PlayerUnits)
	{
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead())
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(EnemyCoord.X, EnemyCoord.Y, PlayerCoord.X, PlayerCoord.Y);

		if (Distance <= 0 || Distance > EnemyUnit->AttackRange)
		{
			continue;
		}

		const float Score = ScoreEnemyBotAttackTarget(EnemyUnit, PlayerUnit);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = PlayerUnit;
		}
	}

	return BestTarget;
}

float AHexGridActor::ScoreEnemyBotAttackTarget(AHexUnitActor* EnemyUnit, AHexUnitActor* Target) const
{
	if (!IsValid(EnemyUnit) || !IsValid(Target) || Target->GetIsDead())
	{
		return -1000000000.0f;
	}

	const FIntPoint EnemyCoord = EnemyUnit->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 Distance = GetHexDistance(EnemyCoord.X, EnemyCoord.Y, TargetCoord.X, TargetCoord.Y);

	const float TargetHealthPercent = Target->MaxHealth > 0
		? static_cast<float>(Target->CurrentHealth) / static_cast<float>(Target->MaxHealth)
		: 1.0f;

	float Score = 1000.0f;

	// Difficulty ladder:
	// Warm Up uses a weak current-turn heuristic and can make visible mistakes.
	// Challenge adds stable target priority and obvious danger avoidance.
	// Ordeal adds faction-aware target selection and future threat checks.
	// Nightmare makes those priorities sharper and removes random mistakes.
	Score += GetEnemyBotTargetBaseValue(Target) * GetEnemyBotDifficultyTargetPriorityScale();
	Score += GetEnemyBotFactionTargetBonus(EnemyUnit, Target);

	const bool bCanKill = Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) >= Target->CurrentHealth;
	if (bCanKill)
	{
		Score += EnemyBotKillScoreBonus * GetEnemyBotDifficultyKillScoreScale();
		Score += EnemyBotExecuteKillBonus;
	}

	Score += ScoreEnemyBotTargetPressure(EnemyUnit, Target);
	Score += ScoreEnemyBotTrade(EnemyUnit, Target, bCanKill);

	Score += (1.0f - TargetHealthPercent) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 130.0f : 260.0f);
	Score += (static_cast<float>(Target->AttackDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 1.4f : 3.0f);
	Score += static_cast<float>(Target->AttackRange) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 10.0f : 25.0f);
	Score += static_cast<float>(Target->MovementRange) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 3.0f : 8.0f);

	if ((Target->UnitType == EHexUnitType::Support || Target->UnitType == EHexUnitType::Healer) && Target->CanHeal())
	{
		Score += IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Challenge) ? 220.0f : 60.0f;
		if (IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Ordeal))
		{
			Score += 180.0f;
		}
	}

	if (Target->UnitType == EHexUnitType::Champion)
	{
		Score += IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Challenge) ? 160.0f : 45.0f;
	}

	if (EnemyBotPlannedFocusTarget.Get() == Target)
	{
		Score += EnemyBotPlanCommitmentScore * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 0.55f : 1.0f);
	}

	// When several bot units can hit the same target, prefer spending AP with the higher-damage attacker.
	Score += (static_cast<float>(EnemyUnit->AttackDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 2.0f : 7.0f);

	if (Target->UnitType == EHexUnitType::Ram && Target->CurrentHealth > Target->GetModifiedIncomingDamage(EnemyUnit->AttackDamage))
	{
		if (IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Ordeal))
		{
			Score -= 230.0f;
		}
		else if (EnemyBotDifficulty == EHexBotDifficulty::Challenge)
		{
			Score -= 70.0f;
		}
	}

	if (Target->bIsSummonedUnit && IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Challenge) && !bCanKill)
	{
		Score -= 120.0f;
	}

	Score -= static_cast<float>(Distance) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 26.0f : 6.0f);

	if (EnemyUnit->UnitType == EHexUnitType::Ram)
	{
		Score -= static_cast<float>(Distance) * 14.0f;
	}

	if (EnemyUnit->UnitType == EHexUnitType::Skirmisher)
	{
		const int32 TargetDistanceToRam = GetNearestAliveEnemyRamDistanceFromCell(TargetCoord, nullptr);
		if (TargetDistanceToRam != MAX_int32 && TargetDistanceToRam <= 2)
		{
			Score += EnemyBotSkirmisherTankGuardScore;
		}
	}

	if (ShouldEnemyBotMakeDifficultyMistake(0.50f))
	{
		Score += FMath::FRandRange(-420.0f, 120.0f);
	}

	return Score;
}

AHexUnitActor* AHexGridActor::FindBestEnemyBotHealTarget(AHexUnitActor* Healer) const
{
	if (!IsValid(Healer) || Healer->GetIsDead() || Healer->Team != EHexUnitTeam::Enemy || !Healer->CanHeal())
	{
		return nullptr;
	}

	return FindBestEnemyBotHealTargetFromCell(Healer, Healer->GetGridCoord(), nullptr);
}

AHexUnitActor* AHexGridActor::FindBestEnemyBotHealTargetFromCell(AHexUnitActor* Healer, const FIntPoint& HealerCoord, float* OutScore) const
{
	if (OutScore)
	{
		*OutScore = -1000000000.0f;
	}

	if (!IsValid(Healer) || Healer->GetIsDead() || Healer->Team != EHexUnitTeam::Enemy || !Healer->CanHeal())
	{
		return nullptr;
	}

	AHexUnitActor* BestTarget = nullptr;
	float BestScore = -1000000000.0f;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (!Healer->CanHealTarget(Ally))
		{
			continue;
		}

		const float Score = ScoreEnemyBotHealTargetFromCell(Healer, Ally, HealerCoord);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Ally;
		}
	}

	if (OutScore)
	{
		*OutScore = BestScore;
	}

	return BestTarget;
}

float AHexGridActor::ScoreEnemyBotHealTargetFromCell(AHexUnitActor* Healer, AHexUnitActor* Target, const FIntPoint& HealerCoord) const
{
	if (!IsValid(Healer) || !IsValid(Target) || !Healer->CanHealTarget(Target))
	{
		return -1000000000.0f;
	}

	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 Distance = GetHexDistance(HealerCoord.X, HealerCoord.Y, TargetCoord.X, TargetCoord.Y);
	const int32 HealRange = FMath::Max(1, Healer->AttackRange);

	if (Distance <= 0 || Distance > HealRange)
	{
		return -1000000000.0f;
	}

	const int32 MissingHealth = FMath::Max(0, Target->MaxHealth - Target->CurrentHealth);
	const int32 EffectiveHeal = FMath::Min(FMath::Max(0, Healer->HealAmount), MissingHealth);
	const float TargetHealthPercent = Target->MaxHealth > 0
		? static_cast<float>(Target->CurrentHealth) / static_cast<float>(Target->MaxHealth)
		: 1.0f;

	float Score = EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 260.0f : 700.0f;
	Score += static_cast<float>(EffectiveHeal) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 4.0f : 9.0f);
	Score += (1.0f - TargetHealthPercent) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 150.0f : 360.0f);

	const bool bDoomedWithoutHeal = IsEnemyBotAllyDoomed(Target, 0);
	const bool bStillDoomedAfterHeal = IsEnemyBotAllyDoomed(Target, EffectiveHeal);
	if (bDoomedWithoutHeal && !bStillDoomedAfterHeal)
	{
		Score += GetEnemyBotUnitBoardValue(Target) * 0.65f;
	}
	else if (bStillDoomedAfterHeal && Target->UnitType != EHexUnitType::Champion)
	{
		Score -= EnemyBotDoomedAllySavePenalty;
	}

	// Do not waste AP on tiny overheal unless there is nothing better.
	if (EffectiveHeal < FMath::Max(8 * AHexUnitActor::GetCombatStatScale(), Healer->HealAmount / 3) && !bDoomedWithoutHeal)
	{
		Score -= 260.0f;
	}

	//                                 ,                                       .
	switch (Target->UnitType)
	{
	case EHexUnitType::Ram:
		Score += 320.0f;
		break;

	case EHexUnitType::Champion:
		Score += 230.0f;
		break;

	case EHexUnitType::Skirmisher:
		Score += 90.0f;
		break;

	case EHexUnitType::Healer:
	case EHexUnitType::Support:
		Score += 40.0f;
		break;

	default:
		break;
	}

	int32 TargetThreatDamage = 0;
	if (IsCellThreatenedByPlayerUnits(TargetCoord.X, TargetCoord.Y, TargetThreatDamage))
	{
		Score += (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 25.0f : 90.0f) + static_cast<float>(TargetThreatDamage) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 0.45f : 1.4f);
	}

	Score -= static_cast<float>(Distance) * 12.0f;
	return Score;
}

bool AHexGridActor::IsCellThreatenedByPlayerUnits(int32 Q, int32 R, int32& OutThreatDamage) const
{
	OutThreatDamage = 0;
	bool bThreatened = false;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(PlayerCoord.X, PlayerCoord.Y, Q, R);

		if (Distance > 0 && Distance <= PlayerUnit->AttackRange)
		{
			bThreatened = true;
			OutThreatDamage += FMath::Max(0, PlayerUnit->AttackDamage);
		}
	}

	return bThreatened;
}

bool AHexGridActor::IsCellThreatenedByPlayerUnitsAfterMove(int32 Q, int32 R, int32& OutThreatDamage) const
{
	OutThreatDamage = 0;
	bool bThreatened = false;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(PlayerCoord.X, PlayerCoord.Y, Q, R);
		const int32 PlayerThreatReach = FMath::Max(0, PlayerUnit->MovementRange) + FMath::Max(1, PlayerUnit->AttackRange);

		//                                   ,                                :
		//                    MovementRange                           AttackRange.
		if (Distance > 0 && Distance <= PlayerThreatReach)
		{
			bThreatened = true;
			OutThreatDamage += FMath::Max(0, PlayerUnit->AttackDamage);
		}
	}

	return bThreatened;
}

int32 AHexGridActor::GetNearestPlayerDistanceFromCell(const FIntPoint& CellCoord) const
{
	int32 NearestDistance = MAX_int32;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* PlayerUnit = Pair.Value;
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead() || PlayerUnit->Team != EHexUnitTeam::Player)
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		NearestDistance = FMath::Min(NearestDistance, GetHexDistance(CellCoord.X, CellCoord.Y, PlayerCoord.X, PlayerCoord.Y));
	}

	return NearestDistance;
}

int32 AHexGridActor::GetNearestAliveEnemyRamDistanceFromCell(const FIntPoint& CellCoord, AHexUnitActor* IgnoreUnit) const
{
	int32 NearestDistance = MAX_int32;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit == IgnoreUnit || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy || Unit->UnitType != EHexUnitType::Ram)
		{
			continue;
		}

		const FIntPoint UnitCoord = Unit->GetGridCoord();
		NearestDistance = FMath::Min(NearestDistance, GetHexDistance(CellCoord.X, CellCoord.Y, UnitCoord.X, UnitCoord.Y));
	}

	return NearestDistance;
}

int32 AHexGridActor::GetNearestPlayerDistanceForEnemyRams() const
{
	int32 NearestDistance = MAX_int32;

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Unit = Pair.Value;
		if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy || Unit->UnitType != EHexUnitType::Ram)
		{
			continue;
		}

		NearestDistance = FMath::Min(NearestDistance, GetNearestPlayerDistanceFromCell(Unit->GetGridCoord()));
	}

	return NearestDistance;
}

int32 AHexGridActor::GetRangedTooCloseDistanceForUnit(AHexUnitActor* EnemyUnit) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->AttackRange <= 1)
	{
		return 0;
	}

	//     AttackRange=2                       1.
	//     AttackRange=3+               1-2                             .
	return FMath::Clamp(EnemyBotRangedTooCloseDistance, 1, FMath::Max(1, EnemyUnit->AttackRange - 1));
}

bool AHexGridActor::HasWoundedEnemyAllyForSupport(AHexUnitActor* Healer) const
{
	if (!IsValid(Healer) || !Healer->CanHeal())
	{
		return false;
	}

	for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
	{
		AHexUnitActor* Ally = Pair.Value;
		if (Healer->CanHealTarget(Ally))
		{
			return true;
		}
	}

	return false;
}

bool AHexGridActor::IsRangedEnemyTooClose(AHexUnitActor* EnemyUnit) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->AttackRange <= 1)
	{
		return false;
	}

	const int32 NearestDistance = GetNearestPlayerDistanceFromCell(EnemyUnit->GetGridCoord());
	const int32 TooCloseDistance = GetRangedTooCloseDistanceForUnit(EnemyUnit);
	return NearestDistance != MAX_int32 && TooCloseDistance > 0 && NearestDistance <= TooCloseDistance;
}

bool AHexGridActor::IsMeaningfulEnemyBotRetreat(AHexUnitActor* EnemyUnit, const FIntPoint& TargetCoord) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return false;
	}

	const FIntPoint CurrentCoord = EnemyUnit->GetGridCoord();
	const int32 CurrentNearestDistance = GetNearestPlayerDistanceFromCell(CurrentCoord);
	const int32 TargetNearestDistance = GetNearestPlayerDistanceFromCell(TargetCoord);

	if (CurrentNearestDistance == MAX_int32 || TargetNearestDistance == MAX_int32)
	{
		return false;
	}

	int32 CurrentThreatDamage = 0;
	int32 TargetThreatDamage = 0;
	const bool bCurrentThreatened = IsCellThreatenedByPlayerUnits(CurrentCoord.X, CurrentCoord.Y, CurrentThreatDamage);
	const bool bTargetThreatened = IsCellThreatenedByPlayerUnits(TargetCoord.X, TargetCoord.Y, TargetThreatDamage);

	int32 CurrentFutureThreatDamage = 0;
	int32 TargetFutureThreatDamage = 0;
	const bool bCurrentFutureThreatened = IsCellThreatenedByPlayerUnitsAfterMove(CurrentCoord.X, CurrentCoord.Y, CurrentFutureThreatDamage);
	const bool bTargetFutureThreatened = IsCellThreatenedByPlayerUnitsAfterMove(TargetCoord.X, TargetCoord.Y, TargetFutureThreatDamage);

	const bool bRangedUnit = EnemyUnit->AttackRange > 1;

	//              :                                        2       .
	if (TargetNearestDistance >= CurrentNearestDistance + 2)
	{
		return true;
	}

	//              :                                  .
	if (bCurrentThreatened && !bTargetThreatened)
	{
		return true;
	}

	//              :                                              .
	if (bCurrentFutureThreatened && (!bTargetFutureThreatened || TargetFutureThreatDamage < CurrentFutureThreatDamage))
	{
		return true;
	}

	//                        :                       ,                           
	//                                            ,                           .
	if (bRangedUnit && CurrentNearestDistance <= GetRangedTooCloseDistanceForUnit(EnemyUnit))
	{
		if (TargetNearestDistance >= 2 && TargetNearestDistance <= EnemyUnit->AttackRange)
		{
			return true;
		}
	}

	return false;
}

float AHexGridActor::ScoreEnemyBotMoveCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, int32 PathLength) const
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead())
	{
		return -1000000000.0f;
	}

	TArray<AHexUnitActor*> PlayerUnits;
	CollectAlivePlayerUnits(PlayerUnits);

	if (PlayerUnits.Num() == 0)
	{
		return -1000000000.0f;
	}

	const float HealthPercent = EnemyUnit->MaxHealth > 0
		? static_cast<float>(EnemyUnit->CurrentHealth) / static_cast<float>(EnemyUnit->MaxHealth)
		: 1.0f;

	const bool bRangedUnit = EnemyUnit->AttackRange > 1;
	const bool bRamUnit = EnemyUnit->UnitType == EHexUnitType::Ram;
	const bool bSkirmisherUnit = EnemyUnit->UnitType == EHexUnitType::Skirmisher;
	const bool bSupportUnit = (EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer);
	const bool bChampionUnit = EnemyUnit->UnitType == EHexUnitType::Champion;

	float EffectiveRetreatHealthPercent = EnemyBotRetreatHealthPercent;
	if (bRamUnit)
	{
		//                                             ,                                    .
		EffectiveRetreatHealthPercent *= 0.45f;
	}
	else if (bSupportUnit)
	{
		//                     :                                            .
		EffectiveRetreatHealthPercent *= 1.25f;
	}
	else if (bChampionUnit)
	{
		EffectiveRetreatHealthPercent *= 0.80f;
	}

	const bool bLowHealth = HealthPercent <= EffectiveRetreatHealthPercent;
	const int32 CurrentNearestPlayerDistance = GetNearestPlayerDistanceFromCell(EnemyUnit->GetGridCoord());
	const bool bRangedTooCloseNow = bRangedUnit && CurrentNearestPlayerDistance != MAX_int32 && CurrentNearestPlayerDistance <= GetRangedTooCloseDistanceForUnit(EnemyUnit);

	int32 DirectThreatDamageNow = 0;
	const bool bDirectlyThreatenedNow = IsEnemyBotUnitDirectlyThreatened(EnemyUnit, &DirectThreatDamageNow);
	const bool bBacklineSafeBehindFront = (bSupportUnit || (bRangedUnit && !bRamUnit)) && !bDirectlyThreatenedNow;
	const bool bEffectiveLowHealth = bLowHealth && (!bBacklineSafeBehindFront || HealthPercent <= EffectiveRetreatHealthPercent * 0.65f);

	float BestTargetScore = -1000000000.0f;
	int32 NearestPlayerDistance = MAX_int32;
	bool bCanAttackFromCandidate = false;
	bool bCanKillFromCandidate = false;

	for (AHexUnitActor* PlayerUnit : PlayerUnits)
	{
		if (!IsValid(PlayerUnit) || PlayerUnit->GetIsDead())
		{
			continue;
		}

		const FIntPoint PlayerCoord = PlayerUnit->GetGridCoord();
		const int32 Distance = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, PlayerCoord.X, PlayerCoord.Y);

		NearestPlayerDistance = FMath::Min(NearestPlayerDistance, Distance);

		const float TargetHealthPercent = PlayerUnit->MaxHealth > 0
			? static_cast<float>(PlayerUnit->CurrentHealth) / static_cast<float>(PlayerUnit->MaxHealth)
			: 1.0f;

		float TargetScore = 0.0f;
		const bool bCandidateCanAttackTarget = Distance > 0 && Distance <= EnemyUnit->AttackRange;
		const bool bCandidateCanKillTarget = bCandidateCanAttackTarget && PlayerUnit->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) >= PlayerUnit->CurrentHealth;

		TargetScore += GetEnemyBotTargetBaseValue(PlayerUnit) * GetEnemyBotDifficultyTargetPriorityScale();
		TargetScore += GetEnemyBotFactionTargetBonus(EnemyUnit, PlayerUnit);
		if (IsPlayerUnitIsolatedForEnemyBot(PlayerUnit))
		{
			TargetScore += EnemyBotIsolatedTargetBonus;
		}
		if (bCandidateCanAttackTarget)
		{
			TargetScore += ScoreEnemyBotTargetPressure(EnemyUnit, PlayerUnit) * 0.75f;
			TargetScore += ScoreEnemyBotTrade(EnemyUnit, PlayerUnit, bCandidateCanKillTarget) * 0.65f;
		}
		TargetScore += (1.0f - TargetHealthPercent) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 120.0f : 240.0f);
		TargetScore += (static_cast<float>(PlayerUnit->AttackDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale()))) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 1.2f : 2.5f);
		TargetScore += static_cast<float>(PlayerUnit->AttackRange) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 9.0f : 22.0f);
		TargetScore += static_cast<float>(PlayerUnit->MovementRange) * (EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 3.0f : 7.0f);

		if ((PlayerUnit->UnitType == EHexUnitType::Support || PlayerUnit->UnitType == EHexUnitType::Healer) && PlayerUnit->CanHeal())
		{
			TargetScore += IsEnemyBotDifficultyAtLeast(EHexBotDifficulty::Challenge) ? 180.0f : 45.0f;
		}

		if (PlayerUnit->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) >= PlayerUnit->CurrentHealth)
		{
			TargetScore += EnemyBotKillScoreBonus * GetEnemyBotDifficultyKillScoreScale();
			if (bCandidateCanKillTarget)
			{
				TargetScore += EnemyBotExecuteKillBonus;
			}
			bCanKillFromCandidate = bCanKillFromCandidate || bCandidateCanKillTarget;
		}

		if (bCandidateCanAttackTarget)
		{
			bCanAttackFromCandidate = true;
			TargetScore += EnemyBotAttackOpportunityBonus;

			if (bRangedUnit && !bRamUnit)
			{
				//                                                ,                           .
				const int32 PreferredDistance = FMath::Max(2, EnemyUnit->AttackRange);
				TargetScore += EnemyBotKiteAttackPositionBonus;
				TargetScore -= static_cast<float>(FMath::Abs(Distance - PreferredDistance)) * EnemyBotRangedPreferredDistanceScore;

				if (Distance <= GetRangedTooCloseDistanceForUnit(EnemyUnit))
				{
					TargetScore -= 360.0f;
				}
			}
		}
		else
		{
			const int32 PreferredDistance = FMath::Max(1, EnemyUnit->AttackRange);
			TargetScore += 120.0f - static_cast<float>(FMath::Abs(Distance - PreferredDistance)) * 24.0f;

			//                                           .                      .
			TargetScore -= static_cast<float>(Distance) * (bRangedUnit ? 6.0f : 18.0f);
		}

		BestTargetScore = FMath::Max(BestTargetScore, TargetScore);
	}

	if (NearestPlayerDistance == MAX_int32)
	{
		return -1000000000.0f;
	}

	int32 CurrentThreatDamage = 0;
	const bool bThreatened = IsCellThreatenedByPlayerUnits(CandidateCoord.X, CandidateCoord.Y, CurrentThreatDamage);

	int32 FutureThreatDamage = 0;
	const bool bRawFutureThreatened = IsCellThreatenedByPlayerUnitsAfterMove(CandidateCoord.X, CandidateCoord.Y, FutureThreatDamage);
	float FutureThreatScale = GetEnemyBotDifficultyFutureThreatScale();
	if (bSupportUnit || (bRangedUnit && !bRamUnit))
	{
		// Basic backline survival is enforced on every difficulty. Warm Up may choose worse targets,
		// but it must not ignore obvious next-turn damage and walk archers in front of tanks.
		FutureThreatScale = FMath::Max(FutureThreatScale, bBacklineSafeBehindFront ? 0.85f : 1.10f);
	}
	const bool bFutureThreatened = bRawFutureThreatened && FutureThreatScale > 0.0f;

	float ThreatMultiplier = 1.0f;
	if (bRamUnit)
	{
		ThreatMultiplier = FMath::Max(0.0f, EnemyBotRamThreatPenaltyMultiplier);
	}
	else if (bSupportUnit)
	{
		ThreatMultiplier = bBacklineSafeBehindFront ? 1.10f : 1.55f;
	}
	else if (bRangedUnit)
	{
		ThreatMultiplier = bBacklineSafeBehindFront ? 1.20f : 1.50f;
	}
	else if (bChampionUnit)
	{
		ThreatMultiplier = 0.85f;
	}

	float Score = BestTargetScore;

	//                ,                              AP.
	Score -= static_cast<float>(PathLength) * 9.0f;

	if (bEffectiveLowHealth)
	{
		//                                                                                          .
		//                                                                               .
		Score += static_cast<float>(NearestPlayerDistance) * EnemyBotRetreatDistanceScore;

		if (bThreatened)
		{
			Score -= (EnemyBotThreatenedCellPenalty + static_cast<float>(CurrentThreatDamage) * 2.5f) * EnemyBotLowHealthThreatMultiplier * ThreatMultiplier;
		}

		if (bFutureThreatened)
		{
			Score -= (EnemyBotFutureThreatPenalty + static_cast<float>(FutureThreatDamage) * 1.75f) * EnemyBotLowHealthThreatMultiplier * ThreatMultiplier * FutureThreatScale;
		}
		else
		{
			Score += 180.0f;
		}

		if (bCanAttackFromCandidate)
		{
			//                                                        ,                    /        .
			Score += bRangedUnit ? 220.0f : 80.0f;
		}
	}
	else
	{
		if (bThreatened)
		{
			Score -= (EnemyBotThreatenedCellPenalty + static_cast<float>(CurrentThreatDamage) * 1.25f) * ThreatMultiplier;
		}

		if (bFutureThreatened)
		{
			Score -= (EnemyBotFutureThreatPenalty + static_cast<float>(FutureThreatDamage) * 0.8f) * ThreatMultiplier * FutureThreatScale;
		}

		if (bRangedUnit && !bRamUnit)
		{
			//                       ,                               ,
			//                                          .
			const int32 PreferredNearestDistance = FMath::Max(2, EnemyUnit->AttackRange);
			Score -= static_cast<float>(FMath::Abs(NearestPlayerDistance - PreferredNearestDistance)) * EnemyBotRangedPreferredDistanceScore;

			if (NearestPlayerDistance <= GetRangedTooCloseDistanceForUnit(EnemyUnit))
			{
				Score -= 300.0f;
			}

			if (bRangedTooCloseNow && NearestPlayerDistance >= 2 && NearestPlayerDistance <= EnemyUnit->AttackRange)
			{
				Score += 300.0f;
			}

			if (bCanAttackFromCandidate && NearestPlayerDistance >= 2)
			{
				Score += 130.0f;
			}
		}
	}

	//                  : Ram                                                    .
	if (bRamUnit)
	{
		Score += static_cast<float>(FMath::Max(0, 8 - NearestPlayerDistance)) * EnemyBotRamAdvanceScore;

		if (bCanAttackFromCandidate)
		{
			Score += 180.0f;
		}
	}

	// Skirmisher                                ,                      .
	//                     Ram                              .
	if (bSkirmisherUnit)
	{
		const int32 DistanceToRam = GetNearestAliveEnemyRamDistanceFromCell(CandidateCoord, EnemyUnit);
		if (DistanceToRam != MAX_int32)
		{
			if (DistanceToRam >= 1 && DistanceToRam <= 2)
			{
				Score += EnemyBotSkirmisherTankGuardScore;
			}
			else if (DistanceToRam > 2)
			{
				Score -= static_cast<float>(DistanceToRam - 2) * 70.0f;
			}

			const int32 RamFrontDistance = GetNearestPlayerDistanceForEnemyRams();
			if (RamFrontDistance != MAX_int32 && NearestPlayerDistance < RamFrontDistance - 1 && !bCanAttackFromCandidate && !bCanKillFromCandidate)
			{
				Score -= 180.0f;
			}
		}
	}

	// Support                                                     .
	if (bSupportUnit && EnemyUnit->CanHeal())
	{
		float HealPositionScore = -1000000000.0f;
		AHexUnitActor* HealTargetFromCandidate = FindBestEnemyBotHealTargetFromCell(EnemyUnit, CandidateCoord, &HealPositionScore);
		if (IsValid(HealTargetFromCandidate))
		{
			Score += EnemyBotSupportHealPositionBonus + HealPositionScore;
		}
		else if (HasWoundedEnemyAllyForSupport(EnemyUnit))
		{
			//                   ,                              ,                                     .
			Score += static_cast<float>(NearestPlayerDistance) * 30.0f;
		}

		if (bThreatened || DirectThreatDamageNow >= EnemyBotSupportDirectThreatRetreatDamage)
		{
			Score -= EnemyBotSupportDangerPenalty + static_cast<float>(CurrentThreatDamage) * 1.4f;
		}
		else if (NearestPlayerDistance >= 2 && NearestPlayerDistance <= FMath::Max(2, EnemyUnit->AttackRange + 1))
		{
			// Support is allowed to hold the line behind a tank if nobody can hit it directly.
			Score += 120.0f;
		}

		const int32 DistanceToRam = GetNearestAliveEnemyRamDistanceFromCell(CandidateCoord, EnemyUnit);
		if (DistanceToRam != MAX_int32 && DistanceToRam >= 2 && DistanceToRam <= 4)
		{
			Score += 80.0f;
		}
	}

	if (bCanKillFromCandidate)
	{
		Score += 120.0f * GetEnemyBotDifficultyKillScoreScale();
	}

	const bool bCanHealFromCandidate = bSupportUnit && EnemyUnit->CanHeal() && IsValid(FindBestEnemyBotHealTargetFromCell(EnemyUnit, CandidateCoord, nullptr));
	Score += ScoreEnemyBotGroupTactics(EnemyUnit, CandidateCoord, bCanAttackFromCandidate, bCanHealFromCandidate);
	Score += ScoreEnemyBotCounterplayAtCell(EnemyUnit, CandidateCoord, bCanAttackFromCandidate, bCanKillFromCandidate);
	Score += ScoreEnemyBotFlankPressure(EnemyUnit, CandidateCoord);
	if (IsMeaningfulEnemyBotRetreat(EnemyUnit, CandidateCoord))
	{
		Score += ScoreEnemyBotRetreatQuality(EnemyUnit, CandidateCoord);
	}
	Score += ScoreEnemyBotLookahead(EnemyUnit, CandidateCoord, GetEnemyBotPlanningDepth());

	Score += GetEnemyBotFactionMoveBonus(EnemyUnit, CandidateCoord, NearestPlayerDistance, bCanAttackFromCandidate);

	if (ShouldEnemyBotMakeDifficultyMistake(0.65f))
	{
		Score += FMath::FRandRange(-480.0f, 160.0f);
	}

	if (EnemyBotRandomScoreJitter > 0.0f)
	{
		float JitterMultiplier = 1.0f;
		switch (EnemyBotDifficulty)
		{
		case EHexBotDifficulty::WarmUp:
			JitterMultiplier = 4.0f;
			break;

		case EHexBotDifficulty::Challenge:
			JitterMultiplier = 1.0f;
			break;

		case EHexBotDifficulty::Ordeal:
			JitterMultiplier = 0.35f;
			break;

		case EHexBotDifficulty::Nightmare:
			JitterMultiplier = 0.0f;
			break;

		default:
			JitterMultiplier = 1.0f;
			break;
		}

		if (JitterMultiplier > 0.0f)
		{
			Score += FMath::FRandRange(0.0f, EnemyBotRandomScoreJitter * JitterMultiplier);
		}
	}

	return Score;
}

bool AHexGridActor::FindBestEnemyBotMove(AHexUnitActor* EnemyUnit, FIntPoint& OutTargetCoord, TArray<FHexCoord>& OutPath, float* OutScore) const
{
	OutPath.Empty();

	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	const int32 EffectiveMovementRange = GetEffectiveMovementRangeForUnit(EnemyUnit);
	if (EffectiveMovementRange <= 0)
	{
		return false;
	}

	const FIntPoint StartCoord = EnemyUnit->GetGridCoord();
	const bool bApproachPhase = IsEnemyBotArmyInApproachPhase();

	struct FEnemyFormationMetrics
	{
		int32 NearestAllyDistance = MAX_int32;
		int32 Diameter = 0;
		int32 DepthSpread = 0;

		// Whole-army connectivity. NearestAllyDistance alone is not enough:
		// two separate groups (3 + 2) can both look locally "connected".
		int32 ConnectedComponents = 0;
		int32 LargestConnectedComponentSize = 0;
		int32 DetachedUnitCount = 0;
	};

	auto MeasureFormationWithVirtualUnit = [this, EnemyUnit](const FIntPoint& VirtualCoord) -> FEnemyFormationMetrics
	{
		FEnemyFormationMetrics Metrics;
		TArray<FIntPoint> EnemyCoords;
		EnemyCoords.Reserve(5);
		int32 MinPlayerDistance = MAX_int32;
		int32 MaxPlayerDistance = 0;

		for (const TPair<FIntPoint, AHexUnitActor*>& Pair : UnitsByCoord)
		{
			AHexUnitActor* Unit = Pair.Value;
			if (!IsValid(Unit) || Unit->GetIsDead() || Unit->Team != EHexUnitTeam::Enemy)
			{
				continue;
			}

			const FIntPoint Coord = Unit == EnemyUnit ? VirtualCoord : Unit->GetGridCoord();
			EnemyCoords.Add(Coord);
			const int32 PlayerDistance = GetNearestPlayerDistanceFromCell(Coord);
			if (PlayerDistance != MAX_int32)
			{
				MinPlayerDistance = FMath::Min(MinPlayerDistance, PlayerDistance);
				MaxPlayerDistance = FMath::Max(MaxPlayerDistance, PlayerDistance);
			}
		}

		for (int32 A = 0; A < EnemyCoords.Num(); ++A)
		{
			for (int32 B = A + 1; B < EnemyCoords.Num(); ++B)
			{
				const int32 Distance = GetHexDistance(EnemyCoords[A].X, EnemyCoords[A].Y, EnemyCoords[B].X, EnemyCoords[B].Y);
				Metrics.Diameter = FMath::Max(Metrics.Diameter, Distance);
			}
		}

		// Treat the army as a graph: two units are linked when they are within
		// EnemyBotFormationLinkRange. This catches a 3+2 split even when every
		// individual unit still has a nearby friend.
		if (!EnemyCoords.IsEmpty())
		{
			const int32 LinkRange = FMath::Max(1, EnemyBotFormationLinkRange);
			TArray<bool> Visited;
			Visited.Init(false, EnemyCoords.Num());

			for (int32 StartIndex = 0; StartIndex < EnemyCoords.Num(); ++StartIndex)
			{
				if (Visited[StartIndex])
				{
					continue;
				}

				++Metrics.ConnectedComponents;
				int32 ComponentSize = 0;
				TArray<int32> Queue;
				Queue.Add(StartIndex);
				Visited[StartIndex] = true;

				for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
				{
					const int32 A = Queue[QueueIndex];
					++ComponentSize;

					for (int32 B = 0; B < EnemyCoords.Num(); ++B)
					{
						if (Visited[B])
						{
							continue;
						}

						const int32 Distance = GetHexDistance(
							EnemyCoords[A].X, EnemyCoords[A].Y,
							EnemyCoords[B].X, EnemyCoords[B].Y
						);
						if (Distance <= LinkRange)
						{
							Visited[B] = true;
							Queue.Add(B);
						}
					}
				}

				Metrics.LargestConnectedComponentSize =
					FMath::Max(Metrics.LargestConnectedComponentSize, ComponentSize);
			}

			Metrics.DetachedUnitCount =
				FMath::Max(0, EnemyCoords.Num() - Metrics.LargestConnectedComponentSize);
		}

		for (const FIntPoint& Coord : EnemyCoords)
		{
			if (Coord == VirtualCoord)
			{
				continue;
			}
			Metrics.NearestAllyDistance = FMath::Min(
				Metrics.NearestAllyDistance,
				GetHexDistance(VirtualCoord.X, VirtualCoord.Y, Coord.X, Coord.Y)
			);
		}

		if (MinPlayerDistance != MAX_int32)
		{
			Metrics.DepthSpread = FMath::Max(0, MaxPlayerDistance - MinPlayerDistance);
		}
		return Metrics;
	};

	const FEnemyFormationMetrics CurrentFormation = MeasureFormationWithVirtualUnit(StartCoord);

	int32 ReservedActionPointsForUnactedAllies = 0;
	if (GetEnemyBotActionCountThisTurn(EnemyUnit) <= 0)
	{
		const int32 OtherUnactedUnits = IsEnemyBotFrontlineUnit(EnemyUnit)
			? GetEnemyBotUnactedFrontlineUnitCount(EnemyUnit)
			: GetEnemyBotUnactedUnitCount(EnemyUnit);
		ReservedActionPointsForUnactedAllies = FMath::Clamp(OtherUnactedUnits, 0, FMath::Max(0, CurrentActionPoints - 1));
	}

	TArray<FHexCoord> ReachableCells;
	GetReachableMoveCells(StartCoord.X, StartCoord.Y, EffectiveMovementRange, ReachableCells);

	if (ReachableCells.Num() == 0)
	{
		return false;
	}

	float BestScore = -1000000000.0f;
	FIntPoint BestTargetCoord = StartCoord;
	TArray<FHexCoord> BestPath;

	for (const FHexCoord& CandidateHex : ReachableCells)
	{
		const FIntPoint CandidateCoord(CandidateHex.Q, CandidateHex.R);

		TArray<FHexCoord> CandidatePath;
		if (!FindPath(StartCoord.X, StartCoord.Y, CandidateCoord.X, CandidateCoord.Y, CandidatePath))
		{
			continue;
		}

		if (CandidatePath.Num() <= 0 || CandidatePath.Num() > EffectiveMovementRange)
		{
			continue;
		}

		const int32 MovePointCost = CalculateMoveActionPointCost(CandidatePath.Num());
		if (!HasEnoughActionPoints(MovePointCost))
		{
			continue;
		}

		if (ReservedActionPointsForUnactedAllies > 0 && MovePointCost > CurrentActionPoints - ReservedActionPointsForUnactedAllies)
		{
			continue;
		}

		const bool bBacklineUnit = EnemyUnit->CanHeal() || (EnemyUnit->AttackRange > 1 && EnemyUnit->UnitType != EHexUnitType::Ram);
		if (bBacklineUnit)
		{
			const int32 MeleeFrontDistance = GetNearestPlayerDistanceForEnemyFrontline(EnemyUnit);
			const int32 CandidatePlayerDistance = GetNearestPlayerDistanceFromCell(CandidateCoord);
			const bool bScreened = IsEnemyBotBacklineScreenedAtCell(EnemyUnit, CandidateCoord);
			bool bCanKillFromCandidate = false;
			const bool bCanAttackFromCandidate = CanEnemyBotAttackAnyPlayerFromCell(EnemyUnit, CandidateCoord, &bCanKillFromCandidate);

			// A living melee line must stay between the player and the backline.
			if (MeleeFrontDistance != MAX_int32 && CandidatePlayerDistance != MAX_int32)
			{
				if (CandidatePlayerDistance < MeleeFrontDistance)
				{
					continue;
				}

				if (CandidatePlayerDistance == MeleeFrontDistance && !bScreened)
				{
					continue;
				}
			}

			int32 DirectThreatDamage = 0;
			const bool bDirectlyThreatened = IsCellThreatenedByPlayerUnits(CandidateCoord.X, CandidateCoord.Y, DirectThreatDamage);
			int32 FutureThreatDamage = 0;
			const bool bFutureThreatened = IsCellThreatenedByPlayerUnitsAfterMove(CandidateCoord.X, CandidateCoord.Y, FutureThreatDamage);
			const int32 UnsafeDamageThreshold = FMath::Max(1, FMath::CeilToInt(static_cast<float>(FMath::Max(1, EnemyUnit->CurrentHealth)) * FMath::Clamp(EnemyBotBacklineUnsafeDamagePercent, 0.05f, 1.0f)));

			if (bDirectlyThreatened && DirectThreatDamage >= UnsafeDamageThreshold && !bScreened && !bCanKillFromCandidate)
			{
				continue;
			}

			if (bFutureThreatened && FutureThreatDamage >= UnsafeDamageThreshold && !bScreened && !bCanKillFromCandidate)
			{
				continue;
			}

			// During normal tactical play the backline waits for the screen to establish itself.
			// During the pre-contact march this rule is disabled; otherwise it creates a deadlock:
			// frontline is constrained by formation depth while backline waits for frontline.
			if (!bApproachPhase && !bCanAttackFromCandidate && GetEnemyBotUnactedFrontlineUnitCount(EnemyUnit) > 0)
			{
				const int32 CurrentPlayerDistance = GetNearestPlayerDistanceFromCell(StartCoord);
				if (CurrentPlayerDistance != MAX_int32 && CandidatePlayerDistance < CurrentPlayerDistance)
				{
					continue;
				}
			}
		}

		bool bFormationCanKillAfterMove = false;
		const bool bFormationCanAttackAfterMove = CanEnemyBotAttackAnyPlayerFromCell(EnemyUnit, CandidateCoord, &bFormationCanKillAfterMove);
		const bool bFormationLeavesAttackAP = bFormationCanAttackAfterMove && CanUnitAttackThisTurn(EnemyUnit) &&
			CurrentActionPoints >= MovePointCost + CalculateAttackActionPointCost();
		const bool bFormationCanHealAfterMove = EnemyUnit->CanHeal() &&
			IsValid(FindBestEnemyBotHealTargetFromCell(EnemyUnit, CandidateCoord, nullptr)) &&
			CurrentActionPoints >= MovePointCost + CalculateHealActionPointCost(EnemyUnit);
		const bool bFormationRetreat = IsMeaningfulEnemyBotRetreat(EnemyUnit, CandidateCoord);

		const FEnemyFormationMetrics CandidateFormation = MeasureFormationWithVirtualUnit(CandidateCoord);
		const int32 AliveEnemyCount = GetAliveUnitCountForTeam(EHexUnitTeam::Enemy);
		if (AliveEnemyCount > 1 && !bFormationRetreat)
		{
			const int32 LinkRange = FMath::Max(1, EnemyBotFormationLinkRange);
			const int32 ApproachSlack = bApproachPhase ? FMath::Max(0, EnemyBotApproachFormationSlack) : 0;
			const int32 MaxDepth = FMath::Max(1, EnemyBotMaxFormationDepth) + ApproachSlack;
			const int32 MaxDiameter = FMath::Max(3, EnemyBotMaxFormationDiameter) + ApproachSlack;
			const bool bImmediateContribution = bFormationLeavesAttackAP || bFormationCanHealAfterMove;
			const int32 TacticalSlack = bImmediateContribution ? 1 : 0;

			// HARD RULE: a connected army may not split into two subgroups because of movement.
			// Immediate attack is NOT an exception: the attacker may stretch the line, but must
			// remain connected to the same army graph.
			if (CurrentFormation.ConnectedComponents <= 1 &&
				CandidateFormation.ConnectedComponents > 1)
			{
				continue;
			}

			// If the formation is already broken, movement must repair it. Attacks that are
			// already available can still happen in the global planner, but positional movement
			// cannot make the 3+2 split persist or become worse.
			if (CurrentFormation.ConnectedComponents > 1)
			{
				const bool bRepairsSplit =
					CandidateFormation.ConnectedComponents < CurrentFormation.ConnectedComponents ||
					CandidateFormation.DetachedUnitCount < CurrentFormation.DetachedUnitCount ||
					CandidateFormation.Diameter < CurrentFormation.Diameter ||
					CandidateFormation.DepthSpread < CurrentFormation.DepthSpread;

				if (!bRepairsSplit)
				{
					continue;
				}
			}

			// Local link rule still prevents a single unit from becoming the detached tail/head
			// of an otherwise connected chain.
			if (CandidateFormation.NearestAllyDistance > LinkRange + TacticalSlack &&
				(CurrentFormation.NearestAllyDistance <= LinkRange + TacticalSlack ||
				 CandidateFormation.NearestAllyDistance >= CurrentFormation.NearestAllyDistance))
			{
				continue;
			}

			if (CandidateFormation.DepthSpread > MaxDepth + TacticalSlack &&
				CandidateFormation.DepthSpread >= CurrentFormation.DepthSpread)
			{
				continue;
			}

			if (CandidateFormation.Diameter > MaxDiameter + TacticalSlack &&
				CandidateFormation.Diameter >= CurrentFormation.Diameter)
			{
				continue;
			}
		}

		float Score = ScoreEnemyBotMoveCell(EnemyUnit, CandidateCoord, CandidatePath.Num());

		// Repair a split formation before spending AP on cosmetic forward movement.
		if (CandidateFormation.ConnectedComponents < CurrentFormation.ConnectedComponents)
		{
			Score += static_cast<float>(CurrentFormation.ConnectedComponents - CandidateFormation.ConnectedComponents)
				* EnemyBotFormationRepairScore * 2.50f;
		}
		if (CandidateFormation.DetachedUnitCount < CurrentFormation.DetachedUnitCount)
		{
			Score += static_cast<float>(CurrentFormation.DetachedUnitCount - CandidateFormation.DetachedUnitCount)
				* EnemyBotFormationRepairScore * 1.75f;
		}

		if (CandidateFormation.DepthSpread < CurrentFormation.DepthSpread)
		{
			Score += static_cast<float>(CurrentFormation.DepthSpread - CandidateFormation.DepthSpread) * EnemyBotFormationRepairScore;
		}
		else if (CandidateFormation.DepthSpread > CurrentFormation.DepthSpread && !bFormationLeavesAttackAP)
		{
			Score -= static_cast<float>(CandidateFormation.DepthSpread - CurrentFormation.DepthSpread) * EnemyBotFormationRepairScore * 0.75f;
		}

		if (CandidateFormation.Diameter < CurrentFormation.Diameter)
		{
			Score += static_cast<float>(CurrentFormation.Diameter - CandidateFormation.Diameter) * EnemyBotFormationRepairScore * 0.55f;
		}

		// Candidate selection itself must understand move -> attack. Otherwise the unit-level
		// move search can choose a prettier formation cell and the global planner never even sees
		// the cell that would have enabled an immediate hit.
		bool bCanKillAfterMove = false;
		const bool bCreatesImmediateAttack = CanEnemyBotAttackAnyPlayerFromCell(EnemyUnit, CandidateCoord, &bCanKillAfterMove);
		const bool bLeavesAttackAP = bCreatesImmediateAttack && CanUnitAttackThisTurn(EnemyUnit) &&
			CurrentActionPoints >= MovePointCost + CalculateAttackActionPointCost();
		if (bLeavesAttackAP)
		{
			Score += EnemyBotMoveIntoAttackBonus;
			if (bCanKillAfterMove)
			{
				Score += EnemyBotExecuteKillBonus;
			}
		}

		if (!IsEnemyBotMovePurposeful(EnemyUnit, CandidateCoord, CandidatePath, Score))
		{
			continue;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTargetCoord = CandidateCoord;
			BestPath = CandidatePath;
		}
	}

	if (BestPath.Num() <= 0)
	{
		return false;
	}

	OutTargetCoord = BestTargetCoord;
	OutPath = BestPath;
	if (OutScore)
	{
		*OutScore = BestScore;
	}
	return true;
}

bool AHexGridActor::TryEnemyBotAttack(AHexUnitActor* EnemyUnit)
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if (!EnemyUnit->CanAct() || !CanUnitAttackThisTurn(EnemyUnit))
	{
		return false;
	}

	if (!HasEnoughActionPoints(CalculateAttackActionPointCost()))
	{
		return false;
	}

	AHexUnitActor* Target = FindBestEnemyBotAttackTarget(EnemyUnit);
	if (!IsValid(Target))
	{
		return false;
	}

	if (!AttackUnit(EnemyUnit, Target))
	{
		return false;
	}

	RecordEnemyBotUnitAction(EnemyUnit);
	EnemyBotMovesDoneThisTurn++;
	EnemyBotBusyRetriesDone = 0;
	ResetEnemyBotActionOrder();

	UE_LOG(LogTemp, Log, TEXT("Enemy bot attacked: %s -> %s. AP=%d/%d"),
		*GetNameSafe(EnemyUnit),
		*GetNameSafe(Target),
		CurrentActionPoints,
		MaxActionPoints
	);

	if (!bAttackProjectileInProgress)
	{
		ScheduleEnemyBotContinueAfterAction();
	}
	return true;
}

bool AHexGridActor::TryEnemyBotHeal(AHexUnitActor* Healer)
{
	if (!IsValid(Healer) || Healer->GetIsDead() || Healer->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if (!Healer->CanAct() || !Healer->CanHeal())
	{
		return false;
	}

	if (!HasEnoughActionPoints(CalculateHealActionPointCost(Healer)))
	{
		return false;
	}

	AHexUnitActor* Target = FindBestEnemyBotHealTarget(Healer);
	if (!IsValid(Target))
	{
		return false;
	}

	if (!HealUnit(Healer, Target))
	{
		return false;
	}

	RecordEnemyBotUnitAction(Healer);
	EnemyBotMovesDoneThisTurn++;
	EnemyBotBusyRetriesDone = 0;
	ResetEnemyBotActionOrder();

	UE_LOG(LogTemp, Log, TEXT("Enemy bot healed: %s -> %s. AP=%d/%d"),
		*GetNameSafe(Healer),
		*GetNameSafe(Target),
		CurrentActionPoints,
		MaxActionPoints
	);

	ScheduleEnemyBotContinueAfterAction();
	return true;
}

bool AHexGridActor::ExecuteEnemyBotMove(AHexUnitActor* EnemyUnit, const FIntPoint& TargetCoord, const TArray<FHexCoord>& CoordPath)
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if (!EnemyUnit->CanAct())
	{
		return false;
	}

	if (CoordPath.Num() <= 0)
	{
		return false;
	}

	const FIntPoint StartCoord = EnemyUnit->GetGridCoord();
	if (StartCoord == TargetCoord)
	{
		return false;
	}

	if (!HasEnoughMovementRangeForUnit(EnemyUnit, CoordPath.Num()))
	{
		UE_LOG(LogTemp, Log, TEXT("Enemy bot move blocked: movement budget is empty. Unit=%s Path=%d Remaining=%d"),
			*GetNameSafe(EnemyUnit),
			CoordPath.Num(),
			GetRemainingMovementRangeForUnit(EnemyUnit)
		);

		return false;
	}

	const int32 MovePointCost = CalculateMoveActionPointCost(CoordPath.Num());
	if (!HasEnoughActionPoints(MovePointCost))
	{
		return false;
	}

	TArray<FVector> WorldPath;
	WorldPath.Reserve(CoordPath.Num());

	for (const FHexCoord& Coord : CoordPath)
	{
		WorldPath.Add(GetCellWorldLocation(Coord.Q, Coord.R));
	}

	if (!SpendActionPoints(MovePointCost))
	{
		return false;
	}

	SpendMovementRangeForUnit(EnemyUnit, CoordPath.Num());

	RecordEnemyBotUnitAction(EnemyUnit);
	EnemyBotMovesDoneThisTurn++;
	EnemyBotBusyRetriesDone = 0;
	ResetEnemyBotActionOrder();

	UnitsByCoord.Remove(StartCoord);
	UnitsByCoord.Add(TargetCoord, EnemyUnit);

	EnemyUnit->SetGridCoord(TargetCoord.X, TargetCoord.Y);

	BotMovingUnit = EnemyUnit;
	BotMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::ContinueEnemyBotTurn);
	BotMovingUnit->OnMovementFinished.AddDynamic(this, &AHexGridActor::ContinueEnemyBotTurn);
	BotMovingUnit->MoveAlongWorldPath(WorldPath);

	UE_LOG(LogTemp, Log, TEXT("Enemy bot moved %s from Q=%d R=%d to Q=%d R=%d. Cost=%d AP=%d/%d"),
		*GetNameSafe(EnemyUnit),
		StartCoord.X,
		StartCoord.Y,
		TargetCoord.X,
		TargetCoord.Y,
		MovePointCost,
		CurrentActionPoints,
		MaxActionPoints
	);

	return true;
}

bool AHexGridActor::TryEnemyBotMove(AHexUnitActor* EnemyUnit)
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if (!EnemyUnit->CanAct())
	{
		return false;
	}

	FIntPoint TargetCoord;
	TArray<FHexCoord> CoordPath;
	float MoveScore = -1000000000.0f;

	if (!FindBestEnemyBotMove(EnemyUnit, TargetCoord, CoordPath, &MoveScore))
	{
		return false;
	}

	return ExecuteEnemyBotMove(EnemyUnit, TargetCoord, CoordPath);
}

bool AHexGridActor::TrySpendEnemyBotMove(AHexUnitActor* EnemyUnit)
{
	if (!IsValid(EnemyUnit) || EnemyUnit->GetIsDead() || EnemyUnit->Team != EHexUnitTeam::Enemy)
	{
		return false;
	}

	if (!EnemyUnit->CanAct())
	{
		return false;
	}

	RefreshEnemyBotPlan(false);

	AHexUnitActor* CurrentAttackTarget = FindBestEnemyBotAttackTarget(EnemyUnit);
	const bool bCanAttackNow = CanUnitAttackThisTurn(EnemyUnit) && IsValid(CurrentAttackTarget) && HasEnoughActionPoints(CalculateAttackActionPointCost());
	const bool bCanKillTarget = bCanAttackNow && CurrentAttackTarget->GetModifiedIncomingDamage(EnemyUnit->AttackDamage) >= CurrentAttackTarget->CurrentHealth;

	// Hard rule: if this unit can finish a target, spend the attack before doing fancy setup.
	// This stops the bot from wasting a champion ability or a random move while a kill is available.
	if (bCanKillTarget && !ShouldEnemyBotMakeDifficultyMistake(EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 0.75f : 0.05f))
	{
		return TryEnemyBotAttack(EnemyUnit);
	}

	// Champion ability is no longer player-only. It is used only when it changes the battle state.
	if (TryEnemyBotChampionAbility(EnemyUnit))
	{
		return true;
	}

	float CurrentHealScore = -1000000000.0f;
	AHexUnitActor* CurrentHealTarget = nullptr;
	if (EnemyUnit->CanHeal() && HasEnoughActionPoints(CalculateHealActionPointCost(EnemyUnit)))
	{
		CurrentHealTarget = FindBestEnemyBotHealTargetFromCell(EnemyUnit, EnemyUnit->GetGridCoord(), &CurrentHealScore);
	}

	// Healer supports the front if there is useful healing. It should not run only because a tank is fighting nearby.
	if (IsValid(CurrentHealTarget))
	{
		float AttackScore = -1000000000.0f;
		if (bCanAttackNow)
		{
			AttackScore = ScoreEnemyBotAttackTarget(EnemyUnit, CurrentAttackTarget);
		}

		const bool bHealIsClearlyUseful = CurrentHealScore >= AttackScore + 120.0f || !bCanAttackNow;
		if (!ShouldEnemyBotMakeDifficultyMistake(EnemyBotDifficulty == EHexBotDifficulty::WarmUp ? 1.0f : 0.15f) && bHealIsClearlyUseful)
		{
			return TryEnemyBotHeal(EnemyUnit);
		}
	}

	const float HealthPercent = EnemyUnit->MaxHealth > 0
		? static_cast<float>(EnemyUnit->CurrentHealth) / static_cast<float>(EnemyUnit->MaxHealth)
		: 1.0f;

	float EffectiveRetreatHealthPercent = EnemyBotRetreatHealthPercent;
	if (EnemyUnit->UnitType == EHexUnitType::Ram)
	{
		EffectiveRetreatHealthPercent *= 0.45f;
	}
	else if ((EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer))
	{
		EffectiveRetreatHealthPercent *= 1.25f;
	}
	else if (EnemyUnit->UnitType == EHexUnitType::Champion)
	{
		EffectiveRetreatHealthPercent *= 0.80f;
	}

	int32 DirectThreatDamage = 0;
	const bool bDirectlyThreatened = IsEnemyBotUnitDirectlyThreatened(EnemyUnit, &DirectThreatDamage);
	const bool bBackline = (EnemyUnit->UnitType == EHexUnitType::Support || EnemyUnit->UnitType == EHexUnitType::Healer) || (EnemyUnit->AttackRange > 1 && EnemyUnit->UnitType != EHexUnitType::Ram);
	const bool bLowHealth = HealthPercent <= EffectiveRetreatHealthPercent;
	const bool bRangedTooClose = IsRangedEnemyTooClose(EnemyUnit) && (!bBackline || bDirectlyThreatened);

	const FIntPoint CurrentCoord = EnemyUnit->GetGridCoord();
	const int32 CurrentPlayerDistance = GetNearestPlayerDistanceFromCell(CurrentCoord);
	const int32 MeleeFrontDistance = GetNearestPlayerDistanceForEnemyFrontline(EnemyUnit);
	const bool bCurrentCellScreened = IsEnemyBotBacklineScreenedAtCell(EnemyUnit, CurrentCoord);
	const bool bBacklineAheadOfFront = bBackline && MeleeFrontDistance != MAX_int32 && CurrentPlayerDistance != MAX_int32 &&
		(CurrentPlayerDistance < MeleeFrontDistance || (CurrentPlayerDistance == MeleeFrontDistance && !bCurrentCellScreened));
	const int32 SeriousThreatThreshold = FMath::Max(1, FMath::CeilToInt(static_cast<float>(FMath::Max(1, EnemyUnit->CurrentHealth)) * FMath::Clamp(EnemyBotBacklineUnsafeDamagePercent, 0.05f, 1.0f)));
	const bool bBacklineUnderSeriousThreat = bBackline && bDirectlyThreatened && DirectThreatDamage >= SeriousThreatThreshold;

	FIntPoint BestMoveTargetCoord;
	TArray<FHexCoord> BestMovePath;
	float BestMoveScore = -1000000000.0f;
	const bool bHasMove = FindBestEnemyBotMove(EnemyUnit, BestMoveTargetCoord, BestMovePath, &BestMoveScore);
	const bool bHasMeaningfulRetreat = bHasMove && IsMeaningfulEnemyBotRetreat(EnemyUnit, BestMoveTargetCoord);

	const bool bSupportWantsHealPosition =
		EnemyUnit->CanHeal() &&
		bHasMove &&
		IsValid(FindBestEnemyBotHealTargetFromCell(EnemyUnit, BestMoveTargetCoord, nullptr));

	if (bSupportWantsHealPosition && !bCanAttackNow)
	{
		return ExecuteEnemyBotMove(EnemyUnit, BestMoveTargetCoord, BestMovePath);
	}

	if (bCanAttackNow)
	{
		// A ranged/support unit that is in front of the melee line repositions before taking a non-lethal shot.
		if (!bCanKillTarget && (bBacklineAheadOfFront || bBacklineUnderSeriousThreat) && bHasMove)
		{
			return ExecuteEnemyBotMove(EnemyUnit, BestMoveTargetCoord, BestMovePath);
		}

		// Backline units hold their firing/healing position unless they are directly threatened.
		if ((bLowHealth || bRangedTooClose) && (!bBackline || bDirectlyThreatened || DirectThreatDamage >= EnemyBotSupportDirectThreatRetreatDamage))
		{
			if (bHasMeaningfulRetreat)
			{
				return ExecuteEnemyBotMove(EnemyUnit, BestMoveTargetCoord, BestMovePath);
			}
		}

		return TryEnemyBotAttack(EnemyUnit);
	}

	// No attack now: move only for a real reason: attack setup, heal setup, retreat, plan focus, or ally assist.
	if (bHasMove)
	{
		return ExecuteEnemyBotMove(EnemyUnit, BestMoveTargetCoord, BestMovePath);
	}

	return false;
}

void AHexGridActor::ContinueEnemyBotTurn()
{
	if (IsValid(BotMovingUnit))
	{
		BotMovingUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::ContinueEnemyBotTurn);
	}

	BotMovingUnit = nullptr;

	if (!IsEnemyTurn() || !bEnemyTurnInProgress)
	{
		return;
	}

	if (!GetWorld())
	{
		RunEnemyBotTurn();
		return;
	}

	if (EnemyBotStepDelay <= 0.0f)
	{
		FTimerDelegate BotTurnDelegate;
		BotTurnDelegate.BindUObject(this, &AHexGridActor::RunEnemyBotTurn);
		GetWorldTimerManager().SetTimerForNextTick(BotTurnDelegate);
		return;
	}

	FTimerHandle DelayHandle;
	GetWorldTimerManager().SetTimer(
		DelayHandle,
		this,
		&AHexGridActor::RunEnemyBotTurn,
		EnemyBotStepDelay,
		false
	);
}

void AHexGridActor::GenerateGrid()
{
	Cells.Empty();
	CoordToInstance.Empty();
	InstanceToCoord.Empty();
	UnitsByCoord.Empty();
	CurrentMoveRangeCells.Empty();
	CurrentMoveRangeSet.Empty();
	CurrentAttackRangeCells.Empty();
	CurrentAttackRangeSet.Empty();
	CurrentAttackBorderSet.Empty();
	CurrentAbilityPreviewRangeCells.Empty();
	CurrentAbilityPreviewRangeSet.Empty();

	HoveredInstanceIndex = INDEX_NONE;
	SelectedInstanceIndex = INDEX_NONE;
	SelectedUnit = nullptr;
	PendingMoveAttackUnit = nullptr;
	PendingMoveAttackTarget = nullptr;
	bPlayerMoveAttackInProgress = false;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	UpdateSelectedUnitWidget();

	if (!HexMeshComponent)
	{
		return;
	}

	HexMeshComponent->ClearInstances();
	HexMeshComponent->NumCustomDataFloats = 6;

	if (!HexTileMesh)
	{
		return;
	}

	HexMeshComponent->SetStaticMesh(HexTileMesh);

	switch (GridShape)
	{
	case EHexGridShape::Hexagon:
	{
		for (int32 Q = -GridRadius; Q <= GridRadius; ++Q)
		{
			const int32 RMin = FMath::Max(-GridRadius, -Q - GridRadius);
			const int32 RMax = FMath::Min(GridRadius, -Q + GridRadius);

			for (int32 R = RMin; R <= RMax; ++R)
			{
				AddCell(Q, R);
			}
		}

		break;
	}

	case EHexGridShape::WideHexagon:
	{
		//                     .                            :
		// GridRadius                      ,
		// WideHexExtraWidth                       /      ,
		// WideHexTopBottomCrop                    /     ,
		// WideHexLeftRightCrop                                      ,
		// WideHexVerticalSideCrop                                  /      ,
		// WideHexEdgeStripCrop                                               .
		const int32 WidthRadius = FMath::Max(0, GridRadius + WideHexExtraWidth);
		const int32 HeightRadius = FMath::Max(0, GridRadius - WideHexTopBottomCrop);
		const int32 RowSideCrop = FMath::Max(0, WideHexLeftRightCrop);
		const int32 VerticalSideCrop = FMath::Max(0, WideHexVerticalSideCrop);
		const int32 EdgeStripCrop = FMath::Max(0, WideHexEdgeStripCrop);

		struct FPendingWideHexCell
		{
			int32 Q = 0;
			int32 R = 0;
			int32 ColumnKey = 0;

			FPendingWideHexCell()
			{
			}

			FPendingWideHexCell(int32 InQ, int32 InR, int32 InColumnKey)
				: Q(InQ), R(InR), ColumnKey(InColumnKey)
			{
			}
		};

		TArray<FPendingWideHexCell> PendingCells;
		TSet<int32> UsedColumnKeys;

		for (int32 R = -HeightRadius; R <= HeightRadius; ++R)
		{
			const int32 BaseQMin = FMath::Max(-WidthRadius, -WidthRadius - R);
			const int32 BaseQMax = FMath::Min(WidthRadius, WidthRadius - R);

			//            :                        /                  .
			const int32 QMin = BaseQMin + RowSideCrop;
			const int32 QMax = BaseQMax - RowSideCrop;

			//                                               ,                        .
			if (QMin > QMax)
			{
				continue;
			}

			for (int32 Q = QMin; Q <= QMax; ++Q)
			{
				//                      .
				// EdgeStripCrop = 1:                                                         .
				// EdgeStripCrop = 2:                                                .
				//                                       ,                                                 .
				const bool bInEdgeStripRows = EdgeStripCrop > 0 && FMath::Abs(R) < EdgeStripCrop;
				const bool bIsLeftOrRightEdgeCell = Q == QMin || Q == QMax;

				if (bInEdgeStripRows && bIsLeftOrRightEdgeCell)
				{
					continue;
				}

				//                         ,                            .
				//     FlatTop                                 Q-         .
				//     PointyTop X            Q + R/2,                               2*Q + R.
				const int32 ColumnKey = (Orientation == EHexOrientation::FlatTop)
					? Q
					: (2 * Q + R);

				PendingCells.Add(FPendingWideHexCell(Q, R, ColumnKey));
				UsedColumnKeys.Add(ColumnKey);
			}
		}

		TSet<int32> CroppedColumnKeys;

		if (VerticalSideCrop > 0 && UsedColumnKeys.Num() > 0)
		{
			TArray<int32> SortedColumnKeys = UsedColumnKeys.Array();
			SortedColumnKeys.Sort();

			//                                            ,                                   .
			const int32 MaxSafeCropPerSide = FMath::Max(0, (SortedColumnKeys.Num() - 1) / 2);
			const int32 EffectiveVerticalCrop = FMath::Min(VerticalSideCrop, MaxSafeCropPerSide);

			for (int32 Index = 0; Index < EffectiveVerticalCrop; ++Index)
			{
				CroppedColumnKeys.Add(SortedColumnKeys[Index]);
				CroppedColumnKeys.Add(SortedColumnKeys[SortedColumnKeys.Num() - 1 - Index]);
			}
		}

		for (const FPendingWideHexCell& PendingCell : PendingCells)
		{
			if (CroppedColumnKeys.Contains(PendingCell.ColumnKey))
			{
				continue;
			}

			AddCell(PendingCell.Q, PendingCell.R);
		}

		break;
	}

	case EHexGridShape::Rectangle:
	default:
	{
		const int32 QOffset = GridWidth / 2;
		const int32 ROffset = GridHeight / 2;

		for (int32 Y = 0; Y < GridHeight; ++Y)
		{
			for (int32 X = 0; X < GridWidth; ++X)
			{
				const int32 Q = X - QOffset;
				const int32 R = Y - ROffset;

				AddCell(Q, R);
			}
		}

		break;
	}
	}
}

void AHexGridActor::AddCell(int32 Q, int32 R)
{
	if (!HexMeshComponent)
	{
		return;
	}

	if (IsCellRemoved(Q, R))
	{
		return;
	}

	const FVector LocalLocation = AxialToLocal(Q, R);

	float ScaleXY = ExtraMeshScale;

	if (bScaleMeshToHexRadius && MeshSourceRadius > KINDA_SMALL_NUMBER)
	{
		ScaleXY *= HexRadius / MeshSourceRadius;
	}

	const FTransform InstanceTransform(
		FRotator(0.0f, MeshYawDegrees, 0.0f),
		LocalLocation,
		FVector(ScaleXY, ScaleXY, ExtraMeshScale)
	);

	const int32 InstanceIndex = HexMeshComponent->AddInstance(InstanceTransform);

	Cells.Add(FHexCell(Q, R, InstanceIndex));
	CoordToInstance.Add(FIntPoint(Q, R), InstanceIndex);
	InstanceToCoord.Add(InstanceIndex, FIntPoint(Q, R));

	HexMeshComponent->SetCustomDataValue(InstanceIndex, 0, 0.0f, false);
	HexMeshComponent->SetCustomDataValue(InstanceIndex, 1, 0.0f, false);
	HexMeshComponent->SetCustomDataValue(InstanceIndex, 2, 0.0f, false);
	HexMeshComponent->SetCustomDataValue(InstanceIndex, 3, 0.0f, false);
	HexMeshComponent->SetCustomDataValue(InstanceIndex, 4, 0.0f, false);
}

FVector AHexGridActor::AxialToLocal(int32 Q, int32 R) const
{
	const float S = HexRadius;
	const float Root3 = FMath::Sqrt(3.0f);

	if (Orientation == EHexOrientation::FlatTop)
	{
		const float X = S * 1.5f * static_cast<float>(Q);
		const float Y = S * Root3 * (static_cast<float>(R) + static_cast<float>(Q) * 0.5f);

		return FVector(X, Y, 0.0f);
	}

	const float X = S * Root3 * (static_cast<float>(Q) + static_cast<float>(R) * 0.5f);
	const float Y = S * 1.5f * static_cast<float>(R);

	return FVector(X, Y, 0.0f);
}

FVector AHexGridActor::GetCellWorldLocation(int32 Q, int32 R) const
{
	const FVector LocalLocation = AxialToLocal(Q, R);

	if (!HexMeshComponent)
	{
		return GetActorTransform().TransformPosition(LocalLocation) + FVector(0.0f, 0.0f, UnitHeightOffset);
	}

	const FVector WorldLocation = HexMeshComponent->GetComponentTransform().TransformPosition(LocalLocation);

	return WorldLocation + FVector(0.0f, 0.0f, UnitHeightOffset);
}

bool AHexGridActor::HasCell(int32 Q, int32 R) const
{
	return CoordToInstance.Contains(FIntPoint(Q, R));
}

bool AHexGridActor::IsCellRemoved(int32 Q, int32 R) const
{
	for (const FHexCoord& RemovedCell : RemovedCells)
	{
		if (RemovedCell.Q == Q && RemovedCell.R == R)
		{
			return true;
		}
	}

	return false;
}

int32 AHexGridActor::GetInstanceIndex(int32 Q, int32 R) const
{
	if (const int32* FoundIndex = CoordToInstance.Find(FIntPoint(Q, R)))
	{
		return *FoundIndex;
	}

	return INDEX_NONE;
}

bool AHexGridActor::GetCoordByInstanceIndex(int32 InstanceIndex, FHexCoord& OutCoord) const
{
	if (const FIntPoint* Coord = InstanceToCoord.Find(InstanceIndex))
	{
		OutCoord.Q = Coord->X;
		OutCoord.R = Coord->Y;
		return true;
	}

	return false;
}

AHexUnitActor* AHexGridActor::SpawnUnitAt(int32 Q, int32 R)
{
	return SpawnUnitOfClassAt(UnitClass, Q, R);
}

AHexUnitActor* AHexGridActor::SpawnUnitOfClassAt(TSubclassOf<AHexUnitActor> InUnitClass, int32 Q, int32 R)
{
	return SpawnUnitOfClassAtInternal(InUnitClass, Q, R, false, EHexUnitTeam::Player);
}

AHexUnitActor* AHexGridActor::SpawnUnitOfClassForTeamAt(TSubclassOf<AHexUnitActor> InUnitClass, EHexUnitTeam Team, int32 Q, int32 R)
{
	return SpawnUnitOfClassAtInternal(InUnitClass, Q, R, true, Team);
}

AHexUnitActor* AHexGridActor::SpawnUnitOfClassAtInternal(TSubclassOf<AHexUnitActor> InUnitClass, int32 Q, int32 R, bool bOverrideTeam, EHexUnitTeam OverrideTeam)
{
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnUnitOfClassAt failed: World is null."));
		return nullptr;
	}

	if (!InUnitClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnUnitOfClassAt failed: UnitClass is not set."));
		return nullptr;
	}

	if (!HasCell(Q, R))
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnUnitOfClassAt failed: cell Q=%d R=%d does not exist."), Q, R);
		return nullptr;
	}

	if (IsCellOccupied(Q, R))
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnUnitOfClassAt failed: cell Q=%d R=%d is occupied."), Q, R);
		return nullptr;
	}

	const FVector SpawnLocation = GetCellWorldLocation(Q, R);
	const FRotator SpawnRotation = GetActorRotation();
	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

	AHexUnitActor* SpawnedUnit = GetWorld()->SpawnActorDeferred<AHexUnitActor>(
		InUnitClass,
		SpawnTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	);

	if (!SpawnedUnit)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnUnitOfClassAt failed: SpawnActorDeferred returned nullptr. Class=%s Location=%s"),
			*GetNameSafe(InUnitClass.Get()),
			*SpawnLocation.ToString()
		);

		return nullptr;
	}

	if (bOverrideTeam)
	{
		SpawnedUnit->Team = OverrideTeam;
	}

	SpawnedUnit->SetGridCoord(Q, R);
	UGameplayStatics::FinishSpawningActor(SpawnedUnit, SpawnTransform);
	SpawnedUnit->ApplyInitialTeamRotation();

	// A pending PSO must never leave a successfully spawned unit permanently invisible.
	// The GameInstance disables proxy waiting globally; this local refresh also repairs
	// components created while an old project/Blueprint setting was still active.
	if (SpawnedUnit->UnitMesh)
	{
		SpawnedUnit->UnitMesh->SetHiddenInGame(false, true);
		SpawnedUnit->UnitMesh->SetVisibility(true, true);
		SpawnedUnit->UnitMesh->MarkRenderStateDirty();

		if (!SpawnedUnit->UnitMesh->GetSkeletalMeshAsset())
		{
			UE_LOG(LogTemp, Error, TEXT("Spawned unit has no Skeletal Mesh asset. Unit=%s Class=%s"),
				*GetNameSafe(SpawnedUnit),
				*GetNameSafe(InUnitClass.Get())
			);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Spawned unit has no UnitMesh component. Unit=%s Class=%s"),
			*GetNameSafe(SpawnedUnit),
			*GetNameSafe(InUnitClass.Get())
		);
	}

	// Refresh after the widget component has been fully initialized.
	// This applies the runtime team color consistently to both armies.
	SpawnedUnit->UpdateHealthBarWidget();

	SpawnedUnit->OnUnitDied.AddDynamic(this, &AHexGridActor::HandleUnitDied);
	UnitsByCoord.Add(FIntPoint(Q, R), SpawnedUnit);

	UE_LOG(LogTemp, Warning, TEXT("Spawned unit %s at Q=%d R=%d Location=%s Class=%s Team=%s"),
		*GetNameSafe(SpawnedUnit),
		Q,
		R,
		*SpawnLocation.ToString(),
		*GetNameSafe(InUnitClass.Get()),
		SpawnedUnit->Team == EHexUnitTeam::Enemy ? TEXT("Enemy") : TEXT("Player")
	);

	return SpawnedUnit;
}

AHexUnitActor* AHexGridActor::GetUnitAtCell(int32 Q, int32 R) const
{
	AHexUnitActor* const* FoundUnit = UnitsByCoord.Find(FIntPoint(Q, R));
	if (!FoundUnit)
	{
		return nullptr;
	}

	if (!IsValid(*FoundUnit))
	{
		return nullptr;
	}

	return *FoundUnit;
}

bool AHexGridActor::IsCellOccupied(int32 Q, int32 R) const
{
	return GetUnitAtCell(Q, R) != nullptr;
}

bool AHexGridActor::IsDeadUnitOnCell(int32 Q, int32 R) const
{
	AHexUnitActor* UnitOnCell = GetUnitAtCell(Q, R);
	return IsValid(UnitOnCell) && UnitOnCell->GetIsDead();
}

bool AHexGridActor::IsDeadUnitInstance(int32 InstanceIndex) const
{
	if (InstanceIndex == INDEX_NONE)
	{
		return false;
	}

	FHexCoord Coord;
	if (!GetCoordByInstanceIndex(InstanceIndex, Coord))
	{
		return false;
	}

	return IsDeadUnitOnCell(Coord.Q, Coord.R);
}

bool AHexGridActor::IsEnemyUnitInstance(int32 InstanceIndex) const
{
	if (InstanceIndex == INDEX_NONE)
	{
		return false;
	}

	FHexCoord Coord;
	if (!GetCoordByInstanceIndex(InstanceIndex, Coord))
	{
		return false;
	}

	AHexUnitActor* UnitOnCell = GetUnitAtCell(Coord.Q, Coord.R);
	return IsValid(UnitOnCell) && !UnitOnCell->GetIsDead() && UnitOnCell->Team == EHexUnitTeam::Enemy;
}

void AHexGridActor::HandleCellClicked(int32 Q, int32 R, int32 InstanceIndex)
{
	if (!IsPlayerTurn())
	{
		return;
	}

	//                                                              .
	//                                    ,                                                    .
	HideActionWarning();
	bAbilityButtonHovered = false;
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);
	}
	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();

	AHexUnitActor* UnitOnCell = GetUnitAtCell(Q, R);

	if (UnitOnCell)
	{
		if (UnitOnCell->GetIsDead())
		{
			//                                            .
			//                 UnitsByCoord,                                            
			//                      .
			SelectedUnit = nullptr;
			CurrentSelectedActionMode = EHexSelectedActionMode::None;
			ClearMoveRangeHighlight();
			ClearAttackRangeHighlight();
			SetSelectedInstance(INDEX_NONE);
			SetHoveredInstance(INDEX_NONE);
			UpdateInstanceVisualState(InstanceIndex);
			UpdateSelectedUnitWidget();

			UE_LOG(LogTemp, Log, TEXT("Clicked dead unit at Q=%d R=%d"), Q, R);

			return;
		}

		if (IsValid(SelectedUnit) && !SelectedUnit->GetIsDead())
		{
			//                                                     .
			if (SelectedUnit == UnitOnCell)
			{
				if (CurrentSelectedActionMode == EHexSelectedActionMode::Ability && SelectedUnit->IsChampionAbilityMarkedForDeath())
				{
					ShowActionWarning(InvalidMarkedForDeathTargetTitle, InvalidMarkedForDeathTargetSubText);
					ShowMarkedForDeathRangeForUnit(SelectedUnit);
					return;
				}

				SelectedUnit = nullptr;
				CurrentSelectedActionMode = EHexSelectedActionMode::None;
				ClearMoveRangeHighlight();
				ClearAttackRangeHighlight();
				SetSelectedInstance(INDEX_NONE);
				UpdateSelectedUnitWidget();
				UpdateActionPointsWidget();

				UE_LOG(LogTemp, Log, TEXT("Deselected unit at Q=%d R=%d"), Q, R);

				return;
			}

			// Ability/Summon                                    .
			//                                ,                                      .
			if (SelectedUnit->Team == EHexUnitTeam::Player && CurrentSelectedActionMode == EHexSelectedActionMode::Ability)
			{
				if (SelectedUnit->IsChampionAbilityMarkedForDeath())
				{
					const FIntPoint ChampionCoord = SelectedUnit->GetGridCoord();
					const FIntPoint TargetCoord = UnitOnCell->GetGridCoord();
					const int32 Distance = GetHexDistance(ChampionCoord.X, ChampionCoord.Y, TargetCoord.X, TargetCoord.Y);
					const bool bValidTarget = SelectedUnit->IsEnemyFor(UnitOnCell)
						&& Distance > 0
						&& Distance <= FMath::Max(1, SelectedUnit->ChampionAbilityRange);

					if (!bValidTarget)
					{
						ShowActionWarning(InvalidMarkedForDeathTargetTitle, InvalidMarkedForDeathTargetSubText);
						ShowMarkedForDeathRangeForUnit(SelectedUnit);
						return;
					}

					if (!ExecuteMarkedForDeathChampionAbility(SelectedUnit, UnitOnCell))
					{
						ShowMarkedForDeathRangeForUnit(SelectedUnit);
					}
					return;
				}

				if (SelectedUnit->IsChampionAbilitySummon())
				{
					ShowActionWarning(
						InvalidSummonTargetTitle,
						InvalidSummonTargetSubText
					);

					UE_LOG(LogTemp, Log, TEXT("Summon ability target blocked: clicked occupied cell. Unit=%s Target=%s"),
						*GetNameSafe(SelectedUnit),
						*GetNameSafe(UnitOnCell)
					);

					return;
				}

				ShowActionWarning(
					ChampionAbilityNotConfiguredTitle,
					ChampionAbilityNotConfiguredSubText
				);

				UE_LOG(LogTemp, Log, TEXT("Champion ability target clicked, but concrete ability logic is not configured yet. Unit=%s Target=%s"),
					*GetNameSafe(SelectedUnit),
					*GetNameSafe(UnitOnCell)
				);

				return;
			}

			//                                 Attack,                      .
			//      Attack           ,                                             .
			if (SelectedUnit->Team == EHexUnitTeam::Player && SelectedUnit->IsEnemyFor(UnitOnCell))
			{
				if (CurrentSelectedActionMode == EHexSelectedActionMode::Attack)
				{
					TryAttackSelectedUnitOrMoveIntoRange(UnitOnCell);
					return;
				}

				SelectUnit(UnitOnCell);
				return;
			}

			//                             Attack,                                                           .
			if (SelectedUnit->Team == EHexUnitTeam::Player && CurrentSelectedActionMode == EHexSelectedActionMode::Attack && SelectedUnit->IsAllyFor(UnitOnCell) && SelectedUnit->CanHealTarget(UnitOnCell))
			{
				if (HealUnit(SelectedUnit, UnitOnCell))
				{
					return;
				}
			}

			//                                                                           .
			SelectUnit(UnitOnCell);
			return;
		}

		//                                                   ,        .
		SelectUnit(UnitOnCell);
		return;
	}

	if (SelectedUnit)
	{
		//                           ,                                                         .
		if (SelectedUnit->Team == EHexUnitTeam::Enemy)
		{
			SelectedUnit = nullptr;
			CurrentSelectedActionMode = EHexSelectedActionMode::None;
			ClearMoveRangeHighlight();
			ClearAttackRangeHighlight();
			SetSelectedInstance(InstanceIndex);
			UpdateSelectedUnitWidget();
			UpdateActionPointsWidget();
			return;
		}

		if (CurrentSelectedActionMode == EHexSelectedActionMode::Ability)
		{
			if (SelectedUnit->IsChampionAbilityMarkedForDeath())
			{
				ShowActionWarning(InvalidMarkedForDeathTargetTitle, InvalidMarkedForDeathTargetSubText);
				ShowMarkedForDeathRangeForUnit(SelectedUnit);
				return;
			}

			if (SelectedUnit->IsChampionAbilitySummon())
			{
				SpawnSummonedUnitFromSummonerAt(SelectedUnit, Q, R);
				return;
			}

			ShowActionWarning(
				ChampionAbilityNotConfiguredTitle,
				ChampionAbilityNotConfiguredSubText
			);

			UE_LOG(LogTemp, Log, TEXT("Champion ability cell clicked, but concrete ability logic is not configured yet. Unit=%s Q=%d R=%d"),
				*GetNameSafe(SelectedUnit),
				Q,
				R
			);

			return;
		}

		if (CurrentSelectedActionMode != EHexSelectedActionMode::Move)
		{
			// Clicking an empty cell while a player unit is only selected
			// should close the unit panel and disable the global border glow.
			ClearSelectionAndHighlights();
			UpdateActionPointsWidget();

			UE_LOG(LogTemp, Log, TEXT("Player unit deselected by clicking empty cell at Q=%d R=%d"), Q, R);
			return;
		}

		MoveSelectedUnitToCell(Q, R);
		return;
	}

	//                                                    ,
	//                                                  .
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(InstanceIndex);
	UpdateSelectedUnitWidget();
	UpdateActionPointsWidget();
}

void AHexGridActor::SelectUnit(AHexUnitActor* Unit)
{
	if (!IsValid(Unit) || Unit->GetIsDead())
	{
		SelectedUnit = nullptr;
		CurrentSelectedActionMode = EHexSelectedActionMode::None;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		SetSelectedInstance(INDEX_NONE);
		UpdateSelectedUnitWidget();
		UpdateActionPointsWidget();
		return;
	}

	//         /                                                                                                   /         .
	if (!IsPlayerInputAllowed())
	{
		SelectedUnit = nullptr;
		CurrentSelectedActionMode = EHexSelectedActionMode::None;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		SetSelectedInstance(INDEX_NONE);
		UpdateSelectedUnitWidget();
		UpdateActionPointsWidget();
		return;
	}

	const bool bIsPlayerUnit = Unit->Team == EHexUnitTeam::Player;
	const bool bIsEnemyUnit = Unit->Team == EHexUnitTeam::Enemy;

	if (!bIsPlayerUnit && !bIsEnemyUnit)
	{
		return;
	}

	//                                       ,                     .
	//                                                            hit-reaction.
	if (bIsPlayerUnit && !Unit->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Select blocked: unit is busy at Q=%d R=%d"),
			Unit->GetGridCoord().X,
			Unit->GetGridCoord().Y
		);

		return;
	}

	//                                                      .
	//                                                            .
	if (bIsPlayerUnit)
	{
		const bool bCanMoveWithCurrentPoints = GetEffectiveMovementRangeForUnit(Unit) > 0;
		const bool bCanAttackWithCurrentPoints = CanUnitAttackThisTurn(Unit) && HasEnoughActionPoints(CalculateAttackActionPointCost());
		const bool bCanHealWithCurrentPoints = Unit->CanHeal() && HasEnoughActionPoints(CalculateHealActionPointCost(Unit));
		const bool bCanSummonWithCurrentPoints = Unit->CanSummon() && HasEnoughActionPoints(CalculateSummonActionPointCost(Unit));
		const bool bCanChampionAbilityWithCurrentPoints = Unit->CanUseChampionAbility() && HasEnoughActionPoints(CalculateChampionAbilityActionPointCost(Unit));

		if (!bCanMoveWithCurrentPoints && !bCanAttackWithCurrentPoints && !bCanHealWithCurrentPoints && !bCanSummonWithCurrentPoints && !bCanChampionAbilityWithCurrentPoints)
		{
			UE_LOG(LogTemp, Log, TEXT("Select blocked: no action points left. Current=%d/%d"),
				CurrentActionPoints,
				MaxActionPoints
			);

			return;
		}
	}

	SelectedUnit = Unit;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;

	const FIntPoint UnitCoord = Unit->GetGridCoord();
	const int32 UnitInstanceIndex = GetInstanceIndex(UnitCoord.X, UnitCoord.Y);

	//                                                                          .
	//          ,              preview                                        .
	bAbilityButtonHovered = false;
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AbilityPreviewTimerHandle);
	}
	ClearAbilityPreviewRangeHighlight();
	ClearAbilityDescriptionTooltip();
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(UnitInstanceIndex);
	UpdateSelectedUnitWidget();
	UpdateActionPointsWidget();

	UE_LOG(LogTemp, Log, TEXT("Selected %s unit at Q=%d R=%d MovementRange=%d AttackRange=%d AttackDamage=%d OccupiedSlots=%d"),
		bIsEnemyUnit ? TEXT("enemy") : TEXT("player"),
		UnitCoord.X,
		UnitCoord.Y,
		Unit->MovementRange,
		Unit->AttackRange,
		Unit->AttackDamage,
		Unit->OccupiedSlots
	);
}

void AHexGridActor::HandleUnitDied(AHexUnitActor* DeadUnit)
{
	if (!IsValid(DeadUnit))
	{
		return;
	}

	const FIntPoint DeadCoord = DeadUnit->GetGridCoord();
	const int32 DeadUnitInstanceIndex = GetInstanceIndex(DeadCoord.X, DeadCoord.Y);

	if (HoveredInstanceIndex == DeadUnitInstanceIndex)
	{
		SetHoveredInstance(INDEX_NONE);
	}

	if (SelectedInstanceIndex == DeadUnitInstanceIndex)
	{
		SetSelectedInstance(INDEX_NONE);
	}

	//                                ,                                 :
	//         SelectedUnit,                                          .
	if (SelectedUnit == DeadUnit)
	{
		SelectedUnit = nullptr;
		CurrentSelectedActionMode = EHexSelectedActionMode::None;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		SetSelectedInstance(INDEX_NONE);
		UpdateSelectedUnitWidget();
	}
	else if (IsValid(SelectedUnit) && SelectedUnit->GetIsDead())
	{
		SelectedUnit = nullptr;
		CurrentSelectedActionMode = EHexSelectedActionMode::None;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		SetSelectedInstance(INDEX_NONE);
		UpdateSelectedUnitWidget();
	}

	//                                      ,              .
	//                     UnitsByCoord,                                            .
	if (DeadUnitInstanceIndex != INDEX_NONE)
	{
		UpdateInstanceVisualState(DeadUnitInstanceIndex);
	}

	//                                    -                 :                                                   .
	if (DeadUnit->bIsSummonedUnit)
	{
		const float DespawnDelay = FMath::Max(0.0f, DeadUnit->SummonedDespawnDelay);

		if (!GetWorld() || DespawnDelay <= 0.0f)
		{
			HandleSummonedUnitDespawn(DeadUnit);
		}
		else
		{
			FTimerDelegate DespawnDelegate;
			DespawnDelegate.BindUObject(this, &AHexGridActor::HandleSummonedUnitDespawn, DeadUnit);

			FTimerHandle DespawnTimerHandle;
			GetWorldTimerManager().SetTimer(
				DespawnTimerHandle,
				DespawnDelegate,
				DespawnDelay,
				false
			);
		}
	}

	EvaluateMatchResultAfterUnitDeath();

	UE_LOG(LogTemp, Log, TEXT("Unit died at Q=%d R=%d. Selection and move range were cleared if needed."),
		DeadCoord.X,
		DeadCoord.Y
	);
}

void AHexGridActor::HandleSummonedUnitDespawn(AHexUnitActor* SummonedUnit)
{
	if (!IsValid(SummonedUnit))
	{
		return;
	}

	const FIntPoint UnitCoord = SummonedUnit->GetGridCoord();
	const int32 UnitInstanceIndex = GetInstanceIndex(UnitCoord.X, UnitCoord.Y);

	if (AHexUnitActor** FoundUnit = UnitsByCoord.Find(UnitCoord))
	{
		if (*FoundUnit == SummonedUnit)
		{
			UnitsByCoord.Remove(UnitCoord);
		}
	}

	if (SelectedUnit == SummonedUnit)
	{
		SelectedUnit = nullptr;
		CurrentSelectedActionMode = EHexSelectedActionMode::None;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		SetSelectedInstance(INDEX_NONE);
		UpdateSelectedUnitWidget();
	}

	if (HoveredInstanceIndex == UnitInstanceIndex)
	{
		SetHoveredInstance(INDEX_NONE);
	}

	if (SelectedInstanceIndex == UnitInstanceIndex)
	{
		SetSelectedInstance(INDEX_NONE);
	}

	if (UnitInstanceIndex != INDEX_NONE)
	{
		UpdateInstanceVisualState(UnitInstanceIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("Summoned unit despawned and cell was released. Unit=%s Q=%d R=%d"),
		*GetNameSafe(SummonedUnit),
		UnitCoord.X,
		UnitCoord.Y
	);

	SummonedUnit->Destroy();
}


bool AHexGridActor::GetCheapestMoveAttackActionPointCost(
	AHexUnitActor* Attacker,
	AHexUnitActor* Target,
	int32& OutRequiredActionPoints,
	int32& OutPathLength
) const
{
	OutRequiredActionPoints = 0;
	OutPathLength = 0;

	if (!IsValid(Attacker) || !IsValid(Target) || Attacker->GetIsDead() || Target->GetIsDead())
	{
		return false;
	}

	if (!Attacker->CanAct() || !Attacker->IsEnemyFor(Target) || !CanUnitAttackThisTurn(Attacker))
	{
		return false;
	}

	const int32 AttackPointCost = CalculateAttackActionPointCost();
	const int32 AttackRange = FMath::Max(1, Attacker->AttackRange);
	const int32 RemainingMovementRange = GetRemainingMovementRangeForUnit(Attacker);

	const FIntPoint StartCoord = Attacker->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 CurrentDistanceToTarget = GetHexDistance(StartCoord.X, StartCoord.Y, TargetCoord.X, TargetCoord.Y);

	if (CurrentDistanceToTarget > 0 && CurrentDistanceToTarget <= AttackRange)
	{
		OutRequiredActionPoints = AttackPointCost;
		OutPathLength = 0;
		return true;
	}

	if (RemainingMovementRange <= 0)
	{
		return false;
	}

	if (CurrentDistanceToTarget > RemainingMovementRange + AttackRange)
	{
		return false;
	}

	int32 BestRequiredCost = MAX_int32;
	int32 BestPathLength = MAX_int32;

	for (const FHexCell& Cell : Cells)
	{
		const FIntPoint CandidateCoord(Cell.Q, Cell.R);

		if (CandidateCoord == TargetCoord)
		{
			continue;
		}

		const int32 CandidateDistanceToTarget = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, TargetCoord.X, TargetCoord.Y);
		if (CandidateDistanceToTarget <= 0 || CandidateDistanceToTarget > AttackRange)
		{
			continue;
		}

		if (IsCellOccupied(CandidateCoord.X, CandidateCoord.Y))
		{
			continue;
		}

		TArray<FHexCoord> CandidatePath;
		if (!FindPath(StartCoord.X, StartCoord.Y, CandidateCoord.X, CandidateCoord.Y, CandidatePath))
		{
			continue;
		}

		if (CandidatePath.Num() <= 0 || CandidatePath.Num() > RemainingMovementRange)
		{
			continue;
		}

		const int32 MovePointCost = CalculateMoveActionPointCost(CandidatePath.Num());
		const int32 TotalPointCost = MovePointCost + AttackPointCost;

		if (TotalPointCost < BestRequiredCost || (TotalPointCost == BestRequiredCost && CandidatePath.Num() < BestPathLength))
		{
			BestRequiredCost = TotalPointCost;
			BestPathLength = CandidatePath.Num();
		}
	}

	if (BestRequiredCost == MAX_int32)
	{
		return false;
	}

	OutRequiredActionPoints = BestRequiredCost;
	OutPathLength = BestPathLength;
	return true;
}

bool AHexGridActor::TryAttackSelectedUnitOrMoveIntoRange(AHexUnitActor* Target)
{
	if (!IsPlayerInputAllowed())
	{
		return false;
	}

	if (!IsValid(SelectedUnit) || SelectedUnit->GetIsDead() || !SelectedUnit->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Move+Attack blocked: selected unit is invalid, dead or busy."));
		return false;
	}

	if (!IsValid(Target) || Target->GetIsDead())
	{
		UE_LOG(LogTemp, Log, TEXT("Move+Attack blocked: target is invalid or dead."));
		return false;
	}

	if (!CanUnitAttackThisTurn(SelectedUnit))
	{
		ShowActionWarning(AttackAlreadyUsedTitle, AttackAlreadyUsedSubText);
		UE_LOG(LogTemp, Log, TEXT("Move+Attack blocked: unit has already attacked this turn. Unit=%s"), *GetNameSafe(SelectedUnit));
		return false;
	}

	if (!SelectedUnit->IsEnemyFor(Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("Move+Attack blocked: target is not enemy."));
		return false;
	}

	const FIntPoint AttackerCoord = SelectedUnit->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 DistanceToTarget = GetHexDistance(AttackerCoord.X, AttackerCoord.Y, TargetCoord.X, TargetCoord.Y);

	if (DistanceToTarget > 0 && DistanceToTarget <= SelectedUnit->AttackRange)
	{
		//                   :                                                     .
		HideActionWarning();
		return AttackUnit(SelectedUnit, Target);
	}

	FIntPoint MoveCoord;
	TArray<FHexCoord> CoordPath;
	if (!FindBestMoveAttackPosition(SelectedUnit, Target, MoveCoord, CoordPath))
	{
		int32 RequiredActionPoints = 0;
		int32 RequiredPathLength = 0;
		if (GetCheapestMoveAttackActionPointCost(SelectedUnit, Target, RequiredActionPoints, RequiredPathLength) && RequiredActionPoints > CurrentActionPoints)
		{
			ShowNotEnoughActionPointsWarning();
		}
		else
		{
			ShowActionWarning(
				TooFarForAttackTitle,
				TooFarForAttackSubText
			);
		}

		UE_LOG(LogTemp, Warning, TEXT("Move+Attack blocked: no executable attack position. Target Q=%d R=%d Distance=%d AttackRange=%d AP=%d/%d RequiredAP=%d RequiredPath=%d"),
			TargetCoord.X,
			TargetCoord.Y,
			DistanceToTarget,
			SelectedUnit->AttackRange,
			CurrentActionPoints,
			MaxActionPoints,
			RequiredActionPoints,
			RequiredPathLength
		);

		return false;
	}

	return ExecuteMoveThenAttack(SelectedUnit, Target, MoveCoord, CoordPath);
}

bool AHexGridActor::FindBestMoveAttackPosition(
	AHexUnitActor* Attacker,
	AHexUnitActor* Target,
	FIntPoint& OutMoveCoord,
	TArray<FHexCoord>& OutPath
) const
{
	OutPath.Empty();

	if (!IsValid(Attacker) || !IsValid(Target) || Attacker->GetIsDead() || Target->GetIsDead())
	{
		return false;
	}

	if (!Attacker->CanAct() || !Attacker->IsEnemyFor(Target) || !CanUnitAttackThisTurn(Attacker))
	{
		return false;
	}

	const int32 AttackPointCost = CalculateAttackActionPointCost();
	const int32 AttackRange = FMath::Max(1, Attacker->AttackRange);
	const int32 RemainingMovementRange = GetRemainingMovementRangeForUnit(Attacker);

	if (RemainingMovementRange <= 0)
	{
		return false;
	}

	const FIntPoint StartCoord = Attacker->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 DirectDistanceToTarget = GetHexDistance(StartCoord.X, StartCoord.Y, TargetCoord.X, TargetCoord.Y);

	if (DirectDistanceToTarget > RemainingMovementRange + AttackRange)
	{
		return false;
	}

	int32 BestRequiredActionPointCost = MAX_int32;
	int32 BestPathLength = MAX_int32;
	int32 BestDistanceToTarget = MAX_int32;
	FIntPoint BestMoveCoord = StartCoord;
	TArray<FHexCoord> BestPath;

	for (const FHexCell& Cell : Cells)
	{
		const FIntPoint CandidateCoord(Cell.Q, Cell.R);

		if (CandidateCoord == TargetCoord)
		{
			continue;
		}

		const int32 CandidateDistanceToTarget = GetHexDistance(CandidateCoord.X, CandidateCoord.Y, TargetCoord.X, TargetCoord.Y);
		if (CandidateDistanceToTarget <= 0 || CandidateDistanceToTarget > AttackRange)
		{
			continue;
		}

		if (IsCellOccupied(CandidateCoord.X, CandidateCoord.Y))
		{
			continue;
		}

		TArray<FHexCoord> CandidatePath;
		if (!FindPath(StartCoord.X, StartCoord.Y, CandidateCoord.X, CandidateCoord.Y, CandidatePath))
		{
			continue;
		}

		if (CandidatePath.Num() <= 0 || CandidatePath.Num() > RemainingMovementRange)
		{
			continue;
		}

		const int32 MovePointCost = CalculateMoveActionPointCost(CandidatePath.Num());
		const int32 TotalPointCost = MovePointCost + AttackPointCost;

		if (!HasEnoughActionPoints(TotalPointCost))
		{
			continue;
		}

		const bool bBetterCost = TotalPointCost < BestRequiredActionPointCost;
		const bool bSameCostButShorterPath = TotalPointCost == BestRequiredActionPointCost && CandidatePath.Num() < BestPathLength;
		const bool bSameCostAndPathButCloserToTarget = TotalPointCost == BestRequiredActionPointCost && CandidatePath.Num() == BestPathLength && CandidateDistanceToTarget < BestDistanceToTarget;

		if (bBetterCost || bSameCostButShorterPath || bSameCostAndPathButCloserToTarget)
		{
			BestRequiredActionPointCost = TotalPointCost;
			BestPathLength = CandidatePath.Num();
			BestDistanceToTarget = CandidateDistanceToTarget;
			BestMoveCoord = CandidateCoord;
			BestPath = CandidatePath;
		}
	}

	if (BestPath.Num() <= 0)
	{
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("MOVE_ATTACK selected cell Q=%d R=%d Path=%d RemainingMove=%d TotalCost=%d AP=%d/%d Target Q=%d R=%d"),
		BestMoveCoord.X,
		BestMoveCoord.Y,
		BestPath.Num(),
		RemainingMovementRange,
		BestRequiredActionPointCost,
		CurrentActionPoints,
		MaxActionPoints,
		TargetCoord.X,
		TargetCoord.Y
	);

	OutMoveCoord = BestMoveCoord;
	OutPath = BestPath;
	return true;
}

bool AHexGridActor::ExecuteMoveThenAttack(
	AHexUnitActor* Attacker,
	AHexUnitActor* Target,
	const FIntPoint& MoveCoord,
	const TArray<FHexCoord>& CoordPath
)
{
	if (!IsPlayerInputAllowed())
	{
		return false;
	}

	if (!IsValid(Attacker) || !IsValid(Target) || Attacker->GetIsDead() || Target->GetIsDead())
	{
		return false;
	}

	if (!Attacker->CanAct() || !Attacker->IsEnemyFor(Target) || !CanUnitAttackThisTurn(Attacker))
	{
		return false;
	}

	if (CoordPath.Num() <= 0)
	{
		return false;
	}

	const FIntPoint StartCoord = Attacker->GetGridCoord();
	if (StartCoord == MoveCoord)
	{
		return false;
	}

	if (!HasEnoughMovementRangeForUnit(Attacker, CoordPath.Num()))
	{
		ShowActionWarning(
			TooFarForAttackTitle,
			TooFarForAttackSubText
		);

		UE_LOG(LogTemp, Warning, TEXT("Move+Attack blocked: not enough remaining movement. Path=%d RemainingMove=%d"),
			CoordPath.Num(),
			GetRemainingMovementRangeForUnit(Attacker)
		);

		return false;
	}

	const int32 MovePointCost = CalculateMoveActionPointCost(CoordPath.Num());
	const int32 AttackPointCost = CalculateAttackActionPointCost();
	const int32 TotalPointCost = MovePointCost + AttackPointCost;

	if (!HasEnoughActionPoints(TotalPointCost))
	{
		ShowNotEnoughActionPointsWarning();

		UE_LOG(LogTemp, Warning, TEXT("Move+Attack blocked: not enough action points. MoveCost=%d AttackCost=%d Total=%d Current=%d/%d"),
			MovePointCost,
			AttackPointCost,
			TotalPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return false;
	}

	//                                               ,        WBP-warning                1.5    .
	//              move+attack                         ,                             
	// "      ",                              .
	HideActionWarning();

	TArray<FVector> WorldPath;
	WorldPath.Reserve(CoordPath.Num());

	for (const FHexCoord& Coord : CoordPath)
	{
		WorldPath.Add(GetCellWorldLocation(Coord.Q, Coord.R));
	}

	if (!SpendActionPoints(MovePointCost))
	{
		return false;
	}

	SpendMovementRangeForUnit(Attacker, CoordPath.Num());

	UnitsByCoord.Remove(StartCoord);
	UnitsByCoord.Add(MoveCoord, Attacker);

	Attacker->SetGridCoord(MoveCoord.X, MoveCoord.Y);

	PendingMoveAttackUnit = Attacker;
	PendingMoveAttackTarget = Target;
	bPlayerMoveAttackInProgress = true;

	PendingMoveAttackUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandleMoveThenAttackMovementFinished);
	PendingMoveAttackUnit->OnMovementFinished.AddDynamic(this, &AHexGridActor::HandleMoveThenAttackMovementFinished);
	PendingMoveAttackUnit->MoveAlongWorldPath(WorldPath);

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();
	UpdateActionPointsWidget();

	UE_LOG(LogTemp, Log, TEXT("Move+Attack started: %s moves from Q=%d R=%d to Q=%d R=%d, then attacks %s. MoveCost=%d AttackCost=%d AP=%d/%d"),
		*GetNameSafe(Attacker),
		StartCoord.X,
		StartCoord.Y,
		MoveCoord.X,
		MoveCoord.Y,
		*GetNameSafe(Target),
		MovePointCost,
		AttackPointCost,
		CurrentActionPoints,
		MaxActionPoints
	);

	return true;
}

void AHexGridActor::HandleMoveThenAttackMovementFinished()
{
	AHexUnitActor* Attacker = PendingMoveAttackUnit;
	AHexUnitActor* Target = PendingMoveAttackTarget;

	if (IsValid(PendingMoveAttackUnit))
	{
		PendingMoveAttackUnit->OnMovementFinished.RemoveDynamic(this, &AHexGridActor::HandleMoveThenAttackMovementFinished);
	}

	PendingMoveAttackUnit = nullptr;
	PendingMoveAttackTarget = nullptr;
	bPlayerMoveAttackInProgress = false;

	if (!IsPlayerTurn() || bPlayerTurnEnding || bEnemyTurnInProgress || IsTurnIntroInProgress())
	{
		UpdateActionPointsWidget();
		return;
	}

	if (!IsValid(Attacker) || Attacker->GetIsDead() || !IsValid(Target) || Target->GetIsDead())
	{
		UE_LOG(LogTemp, Warning, TEXT("Move+Attack attack skipped: attacker or target is invalid/dead after movement."));
		UpdateActionPointsWidget();

		if (IsValid(Attacker) && Attacker->Team == EHexUnitTeam::Player)
		{
			ScheduleAutoEndPlayerTurnAfterAction(Attacker, false);
		}

		return;
	}

	const FIntPoint AttackerCoord = Attacker->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 DistanceToTarget = GetHexDistance(AttackerCoord.X, AttackerCoord.Y, TargetCoord.X, TargetCoord.Y);

	if (DistanceToTarget <= 0 || DistanceToTarget > Attacker->AttackRange)
	{
		UE_LOG(LogTemp, Warning, TEXT("Move+Attack attack skipped: target is still outside attack range. Distance=%d AttackRange=%d"),
			DistanceToTarget,
			Attacker->AttackRange
		);

		UpdateActionPointsWidget();
		ScheduleAutoEndPlayerTurnAfterAction(Attacker, false);
		return;
	}

	if (!AttackUnit(Attacker, Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("Move+Attack attack failed after movement. Attacker=%s Target=%s"),
			*GetNameSafe(Attacker),
			*GetNameSafe(Target)
		);

		UpdateActionPointsWidget();
		ScheduleAutoEndPlayerTurnAfterAction(Attacker, false);
	}
}

bool AHexGridActor::MoveSelectedUnitToCell(int32 Q, int32 R)
{
	if (!IsPlayerInputAllowed())
	{
		return false;
	}

	if (!IsValid(SelectedUnit) || SelectedUnit->GetIsDead())
	{
		SelectedUnit = nullptr;
		CurrentSelectedActionMode = EHexSelectedActionMode::None;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		SetSelectedInstance(INDEX_NONE);
		UpdateSelectedUnitWidget();
		return false;
	}

	if (!SelectedUnit->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Move blocked: selected unit is busy."));
		return false;
	}

	if (!HasCell(Q, R))
	{
		return false;
	}

	if (IsCellOccupied(Q, R))
	{
		return false;
	}

	const FIntPoint StartCoord = SelectedUnit->GetGridCoord();
	const FIntPoint TargetCoord(Q, R);

	if (StartCoord == TargetCoord)
	{
		return false;
	}

	//                    :                              MovementRange.
	//                                                                 .
	//                                                              ,                        .
	TSet<FIntPoint> AllowedMoveSet = CurrentMoveRangeSet;

	if (AllowedMoveSet.Num() == 0)
	{
		const int32 EffectiveMovementRange = GetEffectiveMovementRangeForUnit(SelectedUnit);
		if (EffectiveMovementRange <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Move blocked: not enough action points for movement. Current=%d/%d MoveCost=%d"),
				CurrentActionPoints,
				MaxActionPoints,
				CalculateMoveActionPointCost(1)
			);

			return false;
		}

		TArray<FHexCoord> ReachableCells;
		GetReachableMoveCells(
			StartCoord.X,
			StartCoord.Y,
			EffectiveMovementRange,
			ReachableCells
		);

		for (const FHexCoord& Coord : ReachableCells)
		{
			AllowedMoveSet.Add(FIntPoint(Coord.Q, Coord.R));
		}
	}

	if (!AllowedMoveSet.Contains(TargetCoord))
	{
		UE_LOG(LogTemp, Warning, TEXT("Target Q=%d R=%d is outside movement range."),
			TargetCoord.X,
			TargetCoord.Y
		);

		return false;
	}

	TArray<FHexCoord> CoordPath;
	if (!FindPath(StartCoord.X, StartCoord.Y, TargetCoord.X, TargetCoord.Y, CoordPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("No path from Q=%d R=%d to Q=%d R=%d"),
			StartCoord.X,
			StartCoord.Y,
			TargetCoord.X,
			TargetCoord.Y
		);

		return false;
	}

	//                         :                             MovementRange.
	//                                                     .
	if (!HasEnoughMovementRangeForUnit(SelectedUnit, CoordPath.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Path is too long for remaining movement: Path=%d Remaining=%d MaxRange=%d"),
			CoordPath.Num(),
			GetRemainingMovementRangeForUnit(SelectedUnit),
			SelectedUnit->MovementRange
		);

		return false;
	}

	const int32 MovePointCost = CalculateMoveActionPointCost(CoordPath.Num());
	if (!HasEnoughActionPoints(MovePointCost))
	{
		UE_LOG(LogTemp, Warning, TEXT("Move blocked: not enough action points. Cost=%d Current=%d/%d"),
			MovePointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return false;
	}

	HideActionWarning();

	TArray<FVector> WorldPath;
	WorldPath.Reserve(CoordPath.Num());

	for (const FHexCoord& Coord : CoordPath)
	{
		WorldPath.Add(GetCellWorldLocation(Coord.Q, Coord.R));
	}

	if (!SpendActionPoints(MovePointCost))
	{
		return false;
	}

	AHexUnitActor* MovingUnit = SelectedUnit;
	SpendMovementRangeForUnit(MovingUnit, CoordPath.Num());

	UnitsByCoord.Remove(StartCoord);
	UnitsByCoord.Add(TargetCoord, SelectedUnit);

	MovingUnit->SetGridCoord(TargetCoord.X, TargetCoord.Y);
	MovingUnit->MoveAlongWorldPath(WorldPath);

	//                                                                     .
	//       ShowMoveRangeForUnit                                               .
	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();

	UE_LOG(LogTemp, Log, TEXT("Move unit from Q=%d R=%d to Q=%d R=%d. Path length: %d"),
		StartCoord.X,
		StartCoord.Y,
		TargetCoord.X,
		TargetCoord.Y,
		CoordPath.Num()
	);

	ScheduleAutoEndPlayerTurnAfterAction(MovingUnit, true);

	return true;
}

bool AHexGridActor::IsCellInCurrentMoveRange(int32 Q, int32 R) const
{
	return CurrentMoveRangeSet.Contains(FIntPoint(Q, R));
}

void AHexGridActor::ShowMoveRangeForUnit(AHexUnitActor* Unit)
{
	ClearMoveRangeHighlight();

	if (!IsValid(Unit))
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowMoveRangeForUnit failed: Unit is not valid."));
		return;
	}

	if (Unit->GetIsDead())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowMoveRangeForUnit skipped: Unit is dead."));
		return;
	}

	const FIntPoint UnitCoord = Unit->GetGridCoord();
	const int32 EffectiveMovementRange = GetEffectiveMovementRangeForUnit(Unit);

	if (EffectiveMovementRange <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("ShowMoveRangeForUnit skipped: not enough action points for movement. Current=%d/%d MoveCost=%d"),
			CurrentActionPoints,
			MaxActionPoints,
			CalculateMoveActionPointCost(1)
		);

		return;
	}

	GetReachableMoveCells(
		UnitCoord.X,
		UnitCoord.Y,
		EffectiveMovementRange,
		CurrentMoveRangeCells
	);

	for (const FHexCoord& Coord : CurrentMoveRangeCells)
	{
		CurrentMoveRangeSet.Add(FIntPoint(Coord.Q, Coord.R));
	}

	UE_LOG(LogTemp, Warning, TEXT("Move range calculated: Unit=(%d,%d), Range=%d, EffectiveRange=%d, Cells=%d"),
		UnitCoord.X,
		UnitCoord.Y,
		Unit->MovementRange,
		EffectiveMovementRange,
		CurrentMoveRangeCells.Num()
	);

	for (const FHexCoord& Coord : CurrentMoveRangeCells)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.Q, Coord.R);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}
}

void AHexGridActor::ShowMarkedForDeathRangeForUnit(AHexUnitActor* Champion)
{
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	ClearAbilityPreviewRangeHighlight();

	if (!IsValid(Champion) || Champion->GetIsDead() || !Champion->IsChampionAbilityMarkedForDeath())
	{
		return;
	}

	const FIntPoint ChampionCoord = Champion->GetGridCoord();
	const int32 AbilityRange = FMath::Max(1, Champion->ChampionAbilityRange);
	TArray<FHexCoord> RangeCells;
	GetAttackRangeCells(ChampionCoord.X, ChampionCoord.Y, AbilityRange, RangeCells);

	for (const FHexCoord& Coord : RangeCells)
	{
		AHexUnitActor* Target = GetUnitAtCell(Coord.Q, Coord.R);
		if (!IsValid(Target) || Target->GetIsDead() || !Champion->IsEnemyFor(Target))
		{
			continue;
		}

		CurrentAbilityPreviewRangeCells.Add(Coord);
		CurrentAbilityPreviewRangeSet.Add(FIntPoint(Coord.Q, Coord.R));
	}

	for (const FIntPoint& Coord : CurrentAbilityPreviewRangeSet)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.X, Coord.Y);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}

	UE_LOG(LogTemp, Log, TEXT("Marked for Death target range shown. Champion=%s Range=%d Targets=%d"),
		*GetNameSafe(Champion),
		AbilityRange,
		CurrentAbilityPreviewRangeSet.Num()
	);
}

void AHexGridActor::ShowSummonRangeForUnit(AHexUnitActor* Summoner)
{
	ClearMoveRangeHighlight();

	if (!IsValid(Summoner))
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowSummonRangeForUnit failed: Summoner is not valid."));
		return;
	}

	if (Summoner->GetIsDead())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowSummonRangeForUnit skipped: summoner is dead."));
		return;
	}

	if (!Summoner->CanSummon())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowSummonRangeForUnit skipped: summon is not configured. Unit=%s"), *GetNameSafe(Summoner));
		return;
	}

	if (Summoner->IsChampionAbilitySummon() && !Summoner->CanActivateChampionAbility())
	{
		UE_LOG(LogTemp, Log, TEXT("ShowSummonRangeForUnit skipped: champion ability is unavailable. Unit=%s RemainingCooldown=%d"),
			*GetNameSafe(Summoner),
			Summoner->GetRemainingChampionAbilityCooldownTurns()
		);
		return;
	}

	const int32 SummonPointCost = CalculateSummonActionPointCost(Summoner);
	if (!HasEnoughActionPoints(SummonPointCost))
	{
		UE_LOG(LogTemp, Log, TEXT("ShowSummonRangeForUnit skipped: not enough action points. Current=%d/%d Cost=%d"),
			CurrentActionPoints,
			MaxActionPoints,
			SummonPointCost
		);
		return;
	}

	const FIntPoint SummonerCoord = Summoner->GetGridCoord();
	const int32 SafeSummonRange = Summoner->GetSummonRange();

	TArray<FIntPoint> CandidateCells;
	GetSummonCandidateCells(Summoner, CandidateCells);

	for (const FIntPoint& CandidateCell : CandidateCells)
	{
		CurrentMoveRangeCells.Add(FHexCoord(CandidateCell.X, CandidateCell.Y));
		CurrentMoveRangeSet.Add(CandidateCell);
	}

	UE_LOG(LogTemp, Log, TEXT("Summon range calculated: Unit=(%d,%d), Range=%d, Mode=%d, Count=%d, Cells=%d"),
		SummonerCoord.X,
		SummonerCoord.Y,
		SafeSummonRange,
		static_cast<int32>(Summoner->SummonPlacementMode),
		Summoner->GetSummonUnitCount(),
		CurrentMoveRangeCells.Num()
	);

	for (const FHexCoord& Coord : CurrentMoveRangeCells)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.Q, Coord.R);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}
}

void AHexGridActor::ClearMoveRangeHighlight()
{
	TArray<int32> OldHighlightedInstances;
	OldHighlightedInstances.Reserve(CurrentMoveRangeSet.Num());

	for (const FIntPoint& Coord : CurrentMoveRangeSet)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.X, Coord.Y);
		if (InstanceIndex != INDEX_NONE)
		{
			OldHighlightedInstances.Add(InstanceIndex);
		}
	}

	CurrentMoveRangeCells.Empty();
	CurrentMoveRangeSet.Empty();

	for (const int32 InstanceIndex : OldHighlightedInstances)
	{
		UpdateInstanceVisualState(InstanceIndex);
	}
}

bool AHexGridActor::IsCellInCurrentAttackRange(int32 Q, int32 R) const
{
	return CurrentAttackRangeSet.Contains(FIntPoint(Q, R));
}

void AHexGridActor::ShowAttackRangeForUnit(AHexUnitActor* Unit)
{
	ClearAttackRangeHighlight();

	if (!IsValid(Unit))
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowAttackRangeForUnit failed: Unit is not valid."));
		return;
	}

	if (Unit->GetIsDead())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShowAttackRangeForUnit skipped: Unit is dead."));
		return;
	}

	if (!CanUnitAttackThisTurn(Unit) && !Unit->CanHeal())
	{
		UE_LOG(LogTemp, Log, TEXT("ShowAttackRangeForUnit skipped: unit has already attacked this turn. Unit=%s"), *GetNameSafe(Unit));
		return;
	}

	const int32 AttackPointCost = CalculateAttackActionPointCost();
	if (!HasEnoughActionPoints(AttackPointCost))
	{
		UE_LOG(LogTemp, Log, TEXT("ShowAttackRangeForUnit skipped: not enough action points for attack. Current=%d/%d AttackCost=%d"),
			CurrentActionPoints,
			MaxActionPoints,
			AttackPointCost
		);

		return;
	}

	const FIntPoint UnitCoord = Unit->GetGridCoord();
	const int32 EffectiveAttackRange = FMath::Max(1, Unit->AttackRange);

	GetAttackRangeCells(
		UnitCoord.X,
		UnitCoord.Y,
		EffectiveAttackRange,
		CurrentAttackRangeCells
	);

	for (const FHexCoord& Coord : CurrentAttackRangeCells)
	{
		const FIntPoint CellCoord(Coord.Q, Coord.R);
		CurrentAttackRangeSet.Add(CellCoord);

		//                                                            .
		//                                             ,                        .
		if (GetHexDistance(UnitCoord.X, UnitCoord.Y, Coord.Q, Coord.R) == EffectiveAttackRange)
		{
			CurrentAttackBorderSet.Add(CellCoord);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Attack range calculated: Unit=(%d,%d), Range=%d, Cells=%d, BorderCells=%d"),
		UnitCoord.X,
		UnitCoord.Y,
		EffectiveAttackRange,
		CurrentAttackRangeCells.Num(),
		CurrentAttackBorderSet.Num()
	);

	TSet<FIntPoint> CellsToUpdate;
	for (const FIntPoint& Coord : CurrentAttackRangeSet)
	{
		CellsToUpdate.Add(Coord);
	}
	for (const FIntPoint& Coord : CurrentAttackBorderSet)
	{
		CellsToUpdate.Add(Coord);
	}

	for (const FIntPoint& Coord : CellsToUpdate)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.X, Coord.Y);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}
}

void AHexGridActor::ClearAttackRangeHighlight()
{
	TSet<FIntPoint> OldHighlightedCoords;
	for (const FIntPoint& Coord : CurrentAttackRangeSet)
	{
		OldHighlightedCoords.Add(Coord);
	}
	for (const FIntPoint& Coord : CurrentAttackBorderSet)
	{
		OldHighlightedCoords.Add(Coord);
	}

	CurrentAttackRangeCells.Empty();
	CurrentAttackRangeSet.Empty();
	CurrentAttackBorderSet.Empty();

	for (const FIntPoint& Coord : OldHighlightedCoords)
	{
		const int32 InstanceIndex = GetInstanceIndex(Coord.X, Coord.Y);
		if (InstanceIndex != INDEX_NONE)
		{
			UpdateInstanceVisualState(InstanceIndex);
		}
	}

	if (HexMeshComponent)
	{
		HexMeshComponent->MarkRenderStateDirty();
	}
}

bool AHexGridActor::AttackUnit(AHexUnitActor* Attacker, AHexUnitActor* Target)
{
	if (!IsValid(Attacker) || !IsValid(Target))
	{
		return false;
	}

	if (Attacker->GetIsDead() || Target->GetIsDead())
	{
		return false;
	}

	if ((Attacker->Team == EHexUnitTeam::Player && !IsPlayerInputAllowed()) ||
		(Attacker->Team == EHexUnitTeam::Enemy && !IsEnemyTurn()))
	{
		UE_LOG(LogTemp, Log, TEXT("Attack blocked: wrong turn."));
		return false;
	}

	if (!Attacker->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Attack blocked: attacker is busy."));
		return false;
	}

	if (!CanUnitAttackThisTurn(Attacker))
	{
		if (Attacker->Team == EHexUnitTeam::Player)
		{
			ShowActionWarning(AttackAlreadyUsedTitle, AttackAlreadyUsedSubText);
		}

		UE_LOG(LogTemp, Log, TEXT("Attack blocked: unit has already attacked this turn. Unit=%s"), *GetNameSafe(Attacker));
		return false;
	}

	if (!Attacker->IsEnemyFor(Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack blocked: target is not enemy."));
		return false;
	}

	const FIntPoint AttackerCoord = Attacker->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 Distance = GetHexDistance(AttackerCoord.X, AttackerCoord.Y, TargetCoord.X, TargetCoord.Y);

	if (Distance <= 0 || Distance > Attacker->AttackRange)
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack blocked: target distance=%d attack range=%d."),
			Distance,
			Attacker->AttackRange
		);

		return false;
	}

	const int32 AttackPointCost = CalculateAttackActionPointCost();
	if (!HasEnoughActionPoints(AttackPointCost))
	{
		if (Attacker->Team == EHexUnitTeam::Player)
		{
			ShowNotEnoughActionPointsWarning();
		}

		UE_LOG(LogTemp, Warning, TEXT("Attack blocked: not enough action points. Cost=%d Current=%d/%d"),
			AttackPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return false;
	}

	if (Attacker->Team == EHexUnitTeam::Player)
	{
		HideActionWarning();
	}

	const FVector Direction = Target->GetActorLocation() - Attacker->GetActorLocation();
	if (!Direction.IsNearlyZero())
	{
		FRotator LookRotation = Direction.Rotation();
		LookRotation.Pitch = 0.0f;
		LookRotation.Roll = 0.0f;
		Attacker->SetActorRotation(LookRotation);
	}

	if (!Attacker->PlayAttackAnimation())
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack blocked: attack animation did not start."));
		return false;
	}

	if (!SpendActionPoints(AttackPointCost))
	{
		return false;
	}

	MarkUnitAttackedThisTurn(Attacker);

	const bool bProjectileStarted = Attacker->UsesAttackProjectileVFX() && StartAttackProjectile(Attacker, Target);
	if (!bProjectileStarted)
	{
		ApplyAttackDamageAndRewards(Attacker, Target);
	}

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();

	if (Attacker->Team == EHexUnitTeam::Player && !bProjectileStarted)
	{
		ScheduleAutoEndPlayerTurnAfterAction(Attacker, false);
	}

	return true;
}

bool AHexGridActor::StartAttackProjectile(AHexUnitActor* Attacker, AHexUnitActor* Target)
{
	if (bAttackProjectileInProgress || !IsValid(Attacker) || !IsValid(Target) || !Attacker->UsesAttackProjectileVFX())
	{
		return false;
	}

	PendingProjectileAttacker = Attacker;
	PendingProjectileTarget = Target;
	bAttackProjectileInProgress = true;
	Attacker->SetAttackProjectileActionLocked(true);

	if (!GetWorld())
	{
		HandleAttackProjectileImpact(nullptr, Attacker, Target);
		return true;
	}

	GetWorldTimerManager().ClearTimer(AttackProjectileLaunchTimerHandle);

	const float LaunchDelay = FMath::Max(0.0f, Attacker->AttackProjectileLaunchDelay);
	if (LaunchDelay <= 0.0f)
	{
		LaunchPendingAttackProjectile();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			AttackProjectileLaunchTimerHandle,
			this,
			&AHexGridActor::LaunchPendingAttackProjectile,
			LaunchDelay,
			false
		);
	}

	return true;
}

void AHexGridActor::LaunchPendingAttackProjectile()
{
	AHexUnitActor* Attacker = PendingProjectileAttacker;
	AHexUnitActor* Target = PendingProjectileTarget;

	if (!bAttackProjectileInProgress || !IsValid(Attacker) || !IsValid(Target) || !GetWorld())
	{
		HandleAttackProjectileImpact(nullptr, Attacker, Target);
		return;
	}

	const FVector StartLocation = Attacker->GetAttackProjectileStartLocation();

	FVector TargetLocation = Target->UnitMesh ? Target->UnitMesh->Bounds.Origin : Target->GetActorLocation();
	if (Target->UnitMesh
		&& !Attacker->AttackProjectileTargetSocketName.IsNone()
		&& (Target->UnitMesh->DoesSocketExist(Attacker->AttackProjectileTargetSocketName)
			|| Target->UnitMesh->GetBoneIndex(Attacker->AttackProjectileTargetSocketName) != INDEX_NONE))
	{
		TargetLocation = Target->UnitMesh->GetSocketLocation(Attacker->AttackProjectileTargetSocketName);
	}

	TargetLocation += Attacker->AttackProjectileTargetOffset;

	const FVector InitialDirection = TargetLocation - StartLocation;
	const FRotator SpawnRotation = InitialDirection.IsNearlyZero()
		? Attacker->GetActorRotation()
		: InitialDirection.Rotation() + Attacker->AttackProjectileRotationOffset;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ActiveAttackProjectile = GetWorld()->SpawnActor<AHexAttackProjectile>(
		AHexAttackProjectile::StaticClass(),
		StartLocation,
		SpawnRotation,
		SpawnParameters
	);

	if (!IsValid(ActiveAttackProjectile))
	{
		UE_LOG(LogTemp, Warning, TEXT("Attack projectile spawn failed. Resolving attack immediately. Attacker=%s Target=%s"),
			*GetNameSafe(Attacker),
			*GetNameSafe(Target)
		);

		HandleAttackProjectileImpact(nullptr, Attacker, Target);
		return;
	}

	ActiveAttackProjectile->InitializeProjectile(
		this,
		Attacker,
		Target,
		Attacker->AttackProjectileMesh,
		Attacker->AttackProjectileVFX,
		Attacker->AttackImpactVFX,
		Attacker->AttackProjectileTargetSocketName,
		TargetLocation,
		Attacker->AttackProjectileTargetOffset,
		Attacker->AttackProjectileSpeed,
		Attacker->AttackProjectileArrivalRadius,
		Attacker->AttackProjectileMaxLifetime,
		Attacker->AttackProjectileScale,
		Attacker->AttackImpactVFXScale,
		Attacker->AttackProjectileRotationOffset
	);
}

void AHexGridActor::HandleAttackProjectileImpact(AHexAttackProjectile* Projectile, AHexUnitActor* Attacker, AHexUnitActor* Target)
{
	if (!bAttackProjectileInProgress)
	{
		return;
	}

	if (IsValid(Projectile) && IsValid(ActiveAttackProjectile) && Projectile != ActiveAttackProjectile)
	{
		return;
	}

	AHexUnitActor* ResolvedAttacker = IsValid(PendingProjectileAttacker) ? PendingProjectileAttacker : Attacker;
	AHexUnitActor* ResolvedTarget = IsValid(PendingProjectileTarget) ? PendingProjectileTarget : Target;
	const EHexUnitTeam AttackerTeam = IsValid(ResolvedAttacker) ? ResolvedAttacker->Team : EHexUnitTeam::Player;

	// Clear state before damage, because TakeUnitDamage can finish the match and re-enter cleanup.
	ClearPendingProjectileAttack(false);

	if (IsValid(ResolvedAttacker) && !ResolvedAttacker->GetIsDead() && IsValid(ResolvedTarget) && !ResolvedTarget->GetIsDead())
	{
		ApplyAttackDamageAndRewards(ResolvedAttacker, ResolvedTarget);
	}

	if (bMatchFinished)
	{
		return;
	}

	if (AttackerTeam == EHexUnitTeam::Player)
	{
		ScheduleAutoEndPlayerTurnAfterAction(ResolvedAttacker, false);
	}
	else if (IsEnemyTurn() && bEnemyTurnInProgress)
	{
		EnemyBotBusyRetriesDone = 0;
		ScheduleEnemyBotContinueAfterAction();
	}
}

void AHexGridActor::ApplyAttackDamageAndRewards(AHexUnitActor* Attacker, AHexUnitActor* Target)
{
	if (!IsValid(Attacker) || !IsValid(Target) || Attacker->GetIsDead() || Target->GetIsDead())
	{
		return;
	}

	const bool bTargetWasAliveBeforeDamage = !Target->GetIsDead();
	const int32 TargetHealthBeforeDamage = FMath::Max(0, Target->CurrentHealth);
	const int32 RawAttackDamage = Target->GetModifiedIncomingDamage(Attacker->AttackDamage);
	const bool bPotentiallyLethal = RawAttackDamage >= TargetHealthBeforeDamage;
	const bool bLastStandWillSaveTarget = bPotentiallyLethal && Target->bLastStandActive;
	const int32 PredictedSurviveHealth = bLastStandWillSaveTarget
		? FMath::Clamp(Target->LastStandSurviveHealth, 1, FMath::Max(1, Target->MaxHealth))
		: 0;
	const int32 PredictedActualDamage = bLastStandWillSaveTarget
		? FMath::Clamp(TargetHealthBeforeDamage - PredictedSurviveHealth, 0, TargetHealthBeforeDamage)
		: FMath::Clamp(RawAttackDamage, 0, TargetHealthBeforeDamage);
	const bool bWillKillTarget = bTargetWasAliveBeforeDamage && bPotentiallyLethal && !bLastStandWillSaveTarget;

	if (PredictedActualDamage > 0)
	{
		const int32 NormalizedDamageExperience = FMath::Max(
			1,
			FMath::RoundToInt(static_cast<float>(PredictedActualDamage) / static_cast<float>(FMath::Max(1, AHexUnitActor::GetCombatStatScale())))
		);
		AwardBattleExperience(Attacker, Attacker->CanHeal() ? 1 : NormalizedDamageExperience, Attacker->CanHeal() ? TEXT("HealerAttack") : TEXT("DamageDealt"));
		AwardBattleExperience(Target, NormalizedDamageExperience, TEXT("DamageTaken"));
	}

	if (bWillKillTarget)
	{
		AwardBattleExperience(Attacker, 10, TEXT("Kill"));
	}

	Target->TakeUnitDamage(Attacker->AttackDamage);
	const int32 ActualDamage = FMath::Clamp(TargetHealthBeforeDamage - FMath::Max(0, Target->CurrentHealth), 0, TargetHealthBeforeDamage);
	const bool bTargetKilledByThisAttack = bTargetWasAliveBeforeDamage && Target->GetIsDead();

	if (bTargetKilledByThisAttack)
	{
		AwardKillActionPointBonus(Attacker, Target);
	}

	UE_LOG(LogTemp, Log, TEXT("Attack impact: %s -> %s Damage=%d TargetHealth=%d/%d"),
		*GetNameSafe(Attacker),
		*GetNameSafe(Target),
		ActualDamage,
		Target->CurrentHealth,
		Target->MaxHealth
	);
}

void AHexGridActor::ClearPendingProjectileAttack(bool bDestroyProjectile)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(AttackProjectileLaunchTimerHandle);
	}

	AHexUnitActor* LockedAttacker = PendingProjectileAttacker;
	PendingProjectileAttacker = nullptr;
	PendingProjectileTarget = nullptr;
	bAttackProjectileInProgress = false;

	if (IsValid(LockedAttacker))
	{
		LockedAttacker->SetAttackProjectileActionLocked(false);
	}

	AHexAttackProjectile* ProjectileToClear = ActiveAttackProjectile;
	ActiveAttackProjectile = nullptr;

	if (bDestroyProjectile && IsValid(ProjectileToClear))
	{
		ProjectileToClear->Destroy();
	}
}

bool AHexGridActor::HealUnit(AHexUnitActor* Healer, AHexUnitActor* Target)
{
	if (!IsValid(Healer) || !IsValid(Target))
	{
		return false;
	}

	if (Healer->GetIsDead() || Target->GetIsDead())
	{
		return false;
	}

	if ((Healer->Team == EHexUnitTeam::Player && !IsPlayerInputAllowed()) ||
		(Healer->Team == EHexUnitTeam::Enemy && !IsEnemyTurn()))
	{
		UE_LOG(LogTemp, Log, TEXT("Heal blocked: wrong turn."));
		return false;
	}

	if (!Healer->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Heal blocked: healer is busy."));
		return false;
	}

	if (!Healer->CanHeal())
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal blocked: unit is not a healer."));
		return false;
	}

	if (!Healer->CanHealTarget(Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal blocked: target is not a valid wounded ally."));
		return false;
	}

	const FIntPoint HealerCoord = Healer->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 Distance = GetHexDistance(HealerCoord.X, HealerCoord.Y, TargetCoord.X, TargetCoord.Y);
	const int32 HealRange = FMath::Max(1, Healer->AttackRange);

	if (Distance <= 0 || Distance > HealRange)
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal blocked: target distance=%d heal range=%d."),
			Distance,
			HealRange
		);

		return false;
	}

	const int32 HealPointCost = CalculateHealActionPointCost(Healer);
	if (!HasEnoughActionPoints(HealPointCost))
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal blocked: not enough action points. Cost=%d Current=%d/%d"),
			HealPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return false;
	}

	if (Healer->Team == EHexUnitTeam::Player)
	{
		HideActionWarning();
	}

	const FVector Direction = Target->GetActorLocation() - Healer->GetActorLocation();
	if (!Direction.IsNearlyZero())
	{
		FRotator LookRotation = Direction.Rotation();
		LookRotation.Pitch = 0.0f;
		LookRotation.Roll = 0.0f;
		Healer->SetActorRotation(LookRotation);
	}

	if (!Healer->PlayHealAnimation())
	{
		UE_LOG(LogTemp, Warning, TEXT("Heal blocked: heal animation did not start."));
		return false;
	}

	if (!SpendActionPoints(HealPointCost))
	{
		return false;
	}

	const int32 TargetHealthBeforeHeal = FMath::Max(0, Target->CurrentHealth);
	Target->HealUnit(Healer->HealAmount);
	const int32 ActualHealing = FMath::Max(0, FMath::Max(0, Target->CurrentHealth) - TargetHealthBeforeHeal);
	Healer->PlayHealVFX(Target);

	if (ActualHealing > 0)
	{
		AwardBattleExperience(Healer, 2, TEXT("HealCaster"));
		AwardBattleExperience(Target, 3, TEXT("HealTarget"));
	}

	UE_LOG(LogTemp, Log, TEXT("Heal: %s -> %s HealAmount=%d TargetHealth=%d/%d"),
		*GetNameSafe(Healer),
		*GetNameSafe(Target),
		Healer->HealAmount,
		Target->CurrentHealth,
		Target->MaxHealth
	);

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();

	if (Healer->Team == EHexUnitTeam::Player)
	{
		ScheduleAutoEndPlayerTurnAfterAction(Healer, false);
	}

	return true;
}

bool AHexGridActor::PrepareChampionAbility(AHexUnitActor* Champion)
{
	if (!IsValid(Champion) || Champion->GetIsDead())
	{
		return false;
	}

	if (!Champion->CanUseChampionAbility())
	{
		UE_LOG(LogTemp, Warning, TEXT("Champion ability blocked: unit is not a champion ability user. Unit=%s"), *GetNameSafe(Champion));
		return false;
	}

	if (!Champion->CanActivateChampionAbility())
	{
		if (Champion->Team == EHexUnitTeam::Player)
		{
			ShowChampionAbilityUnavailableWarning(Champion);
		}

		UE_LOG(LogTemp, Log, TEXT("Champion ability blocked: ability is unavailable. Unit=%s RemainingLastStandTurns=%d RemainingCooldown=%d"),
			*GetNameSafe(Champion),
			Champion->RemainingLastStandTurns,
			Champion->GetRemainingChampionAbilityCooldownTurns()
		);
		return false;
	}

	if (!Champion->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Champion ability blocked: champion is busy. Unit=%s"), *GetNameSafe(Champion));
		return false;
	}

	const int32 AbilityPointCost = CalculateChampionAbilityActionPointCost(Champion);
	if (!HasEnoughActionPoints(AbilityPointCost))
	{
		if (Champion->Team == EHexUnitTeam::Player)
		{
			ShowNotEnoughActionPointsWarning();
		}

		UE_LOG(LogTemp, Warning, TEXT("Champion ability blocked: not enough action points. Cost=%d Current=%d/%d"),
			AbilityPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);

		return false;
	}

	if (Champion->IsChampionAbilitySummon())
	{
		SelectedUnit = Champion;
		CurrentSelectedActionMode = EHexSelectedActionMode::Ability;
		ClearAttackRangeHighlight();
		ShowSummonRangeForUnit(Champion);
		UpdateSelectedUnitWidget();
		return true;
	}

	if (Champion->IsChampionAbilityMarkedForDeath())
	{
		SelectedUnit = Champion;
		CurrentSelectedActionMode = EHexSelectedActionMode::Ability;
		ClearMoveRangeHighlight();
		ClearAttackRangeHighlight();
		ShowMarkedForDeathRangeForUnit(Champion);
		UpdateSelectedUnitWidget();
		return true;
	}

	if (Champion->IsChampionAbilityLastStand())
	{
		return ExecuteLastStandChampionAbility(Champion);
	}

	//                                              ,                                                    .
	ShowActionWarning(
		ChampionAbilityNotConfiguredTitle,
		ChampionAbilityNotConfiguredSubText
	);

	return true;
}

bool AHexGridActor::ExecuteLastStandChampionAbility(AHexUnitActor* Champion)
{
	if (!IsValid(Champion) || Champion->GetIsDead())
	{
		return false;
	}

	if (!Champion->IsChampionAbilityLastStand())
	{
		return false;
	}

	if (!Champion->CanActivateChampionAbility())
	{
		if (Champion->Team == EHexUnitTeam::Player)
		{
			ShowChampionAbilityUnavailableWarning(Champion);
		}

		return false;
	}

	if (!Champion->CanAct())
	{
		return false;
	}

	if (Champion->Team == EHexUnitTeam::Player && !IsPlayerInputAllowed())
	{
		return false;
	}

	if (Champion->Team == EHexUnitTeam::Enemy && !IsEnemyTurn())
	{
		return false;
	}

	const int32 AbilityPointCost = CalculateChampionAbilityActionPointCost(Champion);
	if (!HasEnoughActionPoints(AbilityPointCost))
	{
		if (Champion->Team == EHexUnitTeam::Player)
		{
			ShowNotEnoughActionPointsWarning();
		}

		return false;
	}

	if (!Champion->PlayChampionAbilityAnimation())
	{
		UE_LOG(LogTemp, Warning, TEXT("Last Stand blocked: ability animation did not start. Unit=%s"),
			*GetNameSafe(Champion)
		);
		return false;
	}

	if (!SpendActionPoints(AbilityPointCost))
	{
		return false;
	}

	if (!Champion->ActivateLastStand())
	{
		return false;
	}

	Champion->StartChampionAbilityCooldown();
	Champion->PlayChampionAbilityVFX(Champion, Champion->GetActorLocation());
	AwardBattleExperience(Champion, 10, TEXT("Ability"));

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();
	UpdateActionPointsWidget();

	UE_LOG(LogTemp, Log, TEXT("Last Stand ability executed. Unit=%s Cost=%d RemainingTurns=%d"),
		*GetNameSafe(Champion),
		AbilityPointCost,
		Champion->RemainingLastStandTurns
	);

	if (Champion->Team == EHexUnitTeam::Player)
	{
		ScheduleAutoEndPlayerTurnAfterAction(Champion, false);
	}

	return true;
}

bool AHexGridActor::ExecuteMarkedForDeathChampionAbility(AHexUnitActor* Champion, AHexUnitActor* Target)
{
	if (!IsValid(Champion) || !IsValid(Target) || Champion->GetIsDead() || Target->GetIsDead())
	{
		return false;
	}

	if (!Champion->IsChampionAbilityMarkedForDeath() || !Champion->CanActivateChampionAbility())
	{
		return false;
	}

	if (!Champion->CanAct() || !Champion->IsEnemyFor(Target))
	{
		return false;
	}

	if (Champion->Team == EHexUnitTeam::Player && !IsPlayerInputAllowed())
	{
		return false;
	}

	if (Champion->Team == EHexUnitTeam::Enemy && !IsEnemyTurn())
	{
		return false;
	}

	const FIntPoint ChampionCoord = Champion->GetGridCoord();
	const FIntPoint TargetCoord = Target->GetGridCoord();
	const int32 AbilityRange = FMath::Max(1, Champion->ChampionAbilityRange);
	const int32 Distance = GetHexDistance(ChampionCoord.X, ChampionCoord.Y, TargetCoord.X, TargetCoord.Y);
	if (Distance <= 0 || Distance > AbilityRange)
	{
		return false;
	}

	const int32 AbilityPointCost = CalculateChampionAbilityActionPointCost(Champion);
	if (!HasEnoughActionPoints(AbilityPointCost))
	{
		if (Champion->Team == EHexUnitTeam::Player)
		{
			ShowNotEnoughActionPointsWarning();
		}
		return false;
	}

	const FVector Direction = Target->GetActorLocation() - Champion->GetActorLocation();
	if (!Direction.IsNearlyZero())
	{
		FRotator LookRotation = Direction.Rotation();
		LookRotation.Pitch = 0.0f;
		LookRotation.Roll = 0.0f;
		Champion->SetActorRotation(LookRotation);
	}

	if (!Champion->PlayChampionAbilityAnimation())
	{
		if (Champion->Team == EHexUnitTeam::Player)
		{
			ShowActionWarning(
				ChampionAbilityNotConfiguredTitle,
				FText::FromString(TEXT("Set Champion Ability Montage in this champion Blueprint."))
			);
		}

		UE_LOG(LogTemp, Warning, TEXT("Marked for Death blocked: ability animation did not start. Unit=%s"), *GetNameSafe(Champion));
		return false;
	}

	if (!SpendActionPoints(AbilityPointCost))
	{
		return false;
	}

	Target->ApplyMarkedForDeath(
		Champion->MarkedForDeathDamageIncreasePercent,
		Champion->MarkedForDeathDurationTurns
	);

	Champion->StartChampionAbilityCooldown();
	Champion->PlayChampionAbilityVFX(Target, Target->GetActorLocation());
	AwardBattleExperience(Champion, 10, TEXT("Ability"));

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	ClearAbilityPreviewRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();
	UpdateActionPointsWidget();

	UE_LOG(LogTemp, Log, TEXT("Marked for Death executed. Champion=%s Target=%s Increase=%.1f%% Duration=%d Cost=%d"),
		*GetNameSafe(Champion),
		*GetNameSafe(Target),
		Champion->MarkedForDeathDamageIncreasePercent * 100.0f,
		Champion->MarkedForDeathDurationTurns,
		AbilityPointCost
	);

	if (Champion->Team == EHexUnitTeam::Player)
	{
		ScheduleAutoEndPlayerTurnAfterAction(Champion, false);
	}

	return true;
}

bool AHexGridActor::IsValidSummonTargetCell(AHexUnitActor* Summoner, const FIntPoint& TargetCoord) const
{
	if (!IsValid(Summoner) || Summoner->GetIsDead())
	{
		return false;
	}

	if (!HasCell(TargetCoord.X, TargetCoord.Y))
	{
		return false;
	}

	if (IsCellOccupied(TargetCoord.X, TargetCoord.Y))
	{
		return false;
	}

	const FIntPoint SummonerCoord = Summoner->GetGridCoord();
	const int32 Distance = GetHexDistance(SummonerCoord.X, SummonerCoord.Y, TargetCoord.X, TargetCoord.Y);
	const int32 SafeSummonRange = Summoner->GetSummonRange();

	return Distance > 0 && Distance <= SafeSummonRange;
}

void AHexGridActor::GetSummonCandidateCells(AHexUnitActor* Summoner, TArray<FIntPoint>& OutCells) const
{
	OutCells.Empty();

	if (!IsValid(Summoner) || Summoner->GetIsDead())
	{
		return;
	}

	const FIntPoint SummonerCoord = Summoner->GetGridCoord();
	TSet<FIntPoint> AddedCells;

	if (Summoner->SummonPlacementMode == EHexSummonPlacementMode::FixedRelativeCells)
	{
		for (const FHexSummonRelativeCell& RelativeCell : Summoner->SummonFixedRelativeCells)
		{
			const FIntPoint TargetCoord(SummonerCoord.X + RelativeCell.Q, SummonerCoord.Y + RelativeCell.R);
			if (AddedCells.Contains(TargetCoord))
			{
				continue;
			}

			if (IsValidSummonTargetCell(Summoner, TargetCoord))
			{
				OutCells.Add(TargetCoord);
				AddedCells.Add(TargetCoord);
			}
		}

		return;
	}

	for (const FHexCell& Cell : Cells)
	{
		const FIntPoint TargetCoord(Cell.Q, Cell.R);
		if (IsValidSummonTargetCell(Summoner, TargetCoord))
		{
			OutCells.Add(TargetCoord);
		}
	}
}

void AHexGridActor::GetSummonTargetCellsForSelectedCell(AHexUnitActor* Summoner, const FIntPoint& SelectedCellCoord, TArray<FIntPoint>& OutCells) const
{
	OutCells.Empty();

	if (!IsValid(Summoner) || Summoner->GetIsDead())
	{
		return;
	}

	const int32 TargetCount = Summoner->GetSummonUnitCount();
	TSet<FIntPoint> AddedCells;

	if (IsValidSummonTargetCell(Summoner, SelectedCellCoord))
	{
		OutCells.Add(SelectedCellCoord);
		AddedCells.Add(SelectedCellCoord);
	}
	else
	{
		return;
	}

	if (OutCells.Num() >= TargetCount)
	{
		return;
	}

	TArray<FIntPoint> CandidateCells;
	GetSummonCandidateCells(Summoner, CandidateCells);

	while (CandidateCells.Num() > 0 && OutCells.Num() < TargetCount)
	{
		const int32 RandomIndex = FMath::RandRange(0, CandidateCells.Num() - 1);
		const FIntPoint CandidateCoord = CandidateCells[RandomIndex];
		CandidateCells.RemoveAt(RandomIndex);

		if (AddedCells.Contains(CandidateCoord))
		{
			continue;
		}

		OutCells.Add(CandidateCoord);
		AddedCells.Add(CandidateCoord);
	}
}

bool AHexGridActor::SpawnSingleSummonedUnitAt(AHexUnitActor* Summoner, const FIntPoint& TargetCoord, AHexUnitActor*& OutSpawnedUnit)
{
	OutSpawnedUnit = nullptr;

	if (!GetWorld() || !IsValid(Summoner) || !Summoner->SummonedUnitClass)
	{
		return false;
	}

	const FVector SpawnLocation = GetCellWorldLocation(TargetCoord.X, TargetCoord.Y);
	const FRotator SpawnRotation = GetActorRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHexUnitActor* SpawnedUnit = GetWorld()->SpawnActor<AHexUnitActor>(
		Summoner->SummonedUnitClass,
		SpawnLocation,
		SpawnRotation,
		SpawnParams
	);

	if (!SpawnedUnit)
	{
		UE_LOG(LogTemp, Error, TEXT("Summon failed: SpawnActor returned nullptr. Summoner=%s Class=%s Q=%d R=%d"),
			*GetNameSafe(Summoner),
			*GetNameSafe(Summoner->SummonedUnitClass.Get()),
			TargetCoord.X,
			TargetCoord.Y
		);
		return false;
	}

	SpawnedUnit->Team = Summoner->Team;
	SpawnedUnit->SetGridCoord(TargetCoord.X, TargetCoord.Y);

	// SpawnActor has already run BeginPlay before Team is copied from the summoner,
	// so refresh the widget once more to apply the correct team color.
	SpawnedUnit->UpdateHealthBarWidget();

	FRotator FinalSpawnRotation = SpawnRotation;
	if (SpawnedUnit->Team == EHexUnitTeam::Enemy)
	{
		FinalSpawnRotation.Yaw += SpawnedUnit->EnemyStartYawOffsetDegrees;
	}
	SpawnedUnit->SetActorRotation(FinalSpawnRotation);

	SpawnedUnit->InitializeSummonedLifetime();
	SpawnedUnit->OnUnitDied.AddDynamic(this, &AHexGridActor::HandleUnitDied);
	UnitsByCoord.Add(TargetCoord, SpawnedUnit);

	OutSpawnedUnit = SpawnedUnit;
	return true;
}

bool AHexGridActor::SpawnSummonedUnitsAtCells(AHexUnitActor* Summoner, const TArray<FIntPoint>& TargetCells)
{
	if (!IsValid(Summoner))
	{
		return false;
	}

	if (Summoner->GetIsDead())
	{
		return false;
	}

	if ((Summoner->Team == EHexUnitTeam::Player && !IsPlayerInputAllowed()) ||
		(Summoner->Team == EHexUnitTeam::Enemy && !IsEnemyTurn()))
	{
		UE_LOG(LogTemp, Log, TEXT("Summon blocked: wrong turn."));
		return false;
	}

	if (!Summoner->CanAct())
	{
		UE_LOG(LogTemp, Log, TEXT("Summon blocked: summoner is busy."));
		return false;
	}

	if (!Summoner->CanSummon())
	{
		if (Summoner->Team == EHexUnitTeam::Player)
		{
			ShowActionWarning(
				SummonNotConfiguredTitle,
				SummonNotConfiguredSubText
			);
		}

		if (!Summoner->SummonedUnitClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("Summon blocked: SummonedUnitClass is not set. Unit=%s"), *GetNameSafe(Summoner));
		}
		else if (!Summoner->IsSummonedUnitClassValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Summon blocked: selected class must have bIsSummonedUnit=true. Summoner=%s Class=%s"),
				*GetNameSafe(Summoner),
				*GetNameSafe(Summoner->SummonedUnitClass.Get())
			);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Summon blocked: unit is not allowed to summon. Unit=%s"), *GetNameSafe(Summoner));
		}

		return false;
	}

	if (Summoner->IsChampionAbilitySummon() && !Summoner->CanActivateChampionAbility())
	{
		if (Summoner->Team == EHexUnitTeam::Player)
		{
			ShowChampionAbilityUnavailableWarning(Summoner);
		}

		UE_LOG(LogTemp, Log, TEXT("Summon blocked: champion ability is unavailable. Unit=%s RemainingCooldown=%d"),
			*GetNameSafe(Summoner),
			Summoner->GetRemainingChampionAbilityCooldownTurns()
		);
		return false;
	}

	const int32 SummonPointCost = CalculateSummonActionPointCost(Summoner);
	if (!HasEnoughActionPoints(SummonPointCost))
	{
		if (Summoner->Team == EHexUnitTeam::Player)
		{
			ShowNotEnoughActionPointsWarning();
		}
		UE_LOG(LogTemp, Warning, TEXT("Summon blocked: not enough action points. Cost=%d Current=%d/%d"),
			SummonPointCost,
			CurrentActionPoints,
			MaxActionPoints
		);
		return false;
	}

	const int32 TargetCount = Summoner->GetSummonUnitCount();
	TArray<FIntPoint> ValidTargetCells;
	TSet<FIntPoint> UsedTargetCells;

	for (const FIntPoint& TargetCoord : TargetCells)
	{
		if (ValidTargetCells.Num() >= TargetCount)
		{
			break;
		}

		if (UsedTargetCells.Contains(TargetCoord))
		{
			continue;
		}

		if (!IsValidSummonTargetCell(Summoner, TargetCoord))
		{
			continue;
		}

		ValidTargetCells.Add(TargetCoord);
		UsedTargetCells.Add(TargetCoord);
	}

	if (ValidTargetCells.Num() <= 0)
	{
		if (Summoner->Team == EHexUnitTeam::Player)
		{
			ShowActionWarning(InvalidSummonTargetTitle, InvalidSummonTargetSubText);
		}
		UE_LOG(LogTemp, Warning, TEXT("Summon blocked: no valid free cells. Unit=%s Mode=%d Count=%d"),
			*GetNameSafe(Summoner),
			static_cast<int32>(Summoner->SummonPlacementMode),
			TargetCount
		);
		return false;
	}

	TArray<AHexUnitActor*> SpawnedUnits;
	SpawnedUnits.Reserve(ValidTargetCells.Num());

	for (const FIntPoint& TargetCoord : ValidTargetCells)
	{
		AHexUnitActor* SpawnedUnit = nullptr;
		if (SpawnSingleSummonedUnitAt(Summoner, TargetCoord, SpawnedUnit) && IsValid(SpawnedUnit))
		{
			SpawnedUnits.Add(SpawnedUnit);
		}
	}

	if (SpawnedUnits.Num() <= 0)
	{
		return false;
	}

	if (!SpendActionPoints(SummonPointCost))
	{
		for (AHexUnitActor* SpawnedUnit : SpawnedUnits)
		{
			HandleSummonedUnitDespawn(SpawnedUnit);
		}
		return false;
	}

	if (Summoner->IsChampionAbilitySummon())
	{
		Summoner->PlayChampionAbilityAnimation();
		Summoner->StartChampionAbilityCooldown();
	}

	AwardBattleExperience(Summoner, 10, TEXT("Ability"));

	for (AHexUnitActor* SpawnedUnit : SpawnedUnits)
	{
		if (!IsValid(SpawnedUnit))
		{
			continue;
		}

		Summoner->PlaySummonVFX(SpawnedUnit->GetActorLocation());
	}

	UE_LOG(LogTemp, Log, TEXT("Summon: %s spawned %d/%d unit(s). Cost=%d AP=%d/%d Mode=%d"),
		*GetNameSafe(Summoner),
		SpawnedUnits.Num(),
		TargetCount,
		SummonPointCost,
		CurrentActionPoints,
		MaxActionPoints,
		static_cast<int32>(Summoner->SummonPlacementMode)
	);

	SelectedUnit = nullptr;
	CurrentSelectedActionMode = EHexSelectedActionMode::None;
	ClearMoveRangeHighlight();
	ClearAttackRangeHighlight();
	SetSelectedInstance(INDEX_NONE);
	UpdateSelectedUnitWidget();

	if (Summoner->Team == EHexUnitTeam::Player)
	{
		ScheduleAutoEndPlayerTurnAfterAction(Summoner, false);
	}

	return true;
}

bool AHexGridActor::SpawnSummonedUnitsFromSummoner(AHexUnitActor* Summoner)
{
	TArray<FIntPoint> TargetCells;
	GetSummonCandidateCells(Summoner, TargetCells);

	if (IsValid(Summoner) && Summoner->SummonPlacementMode == EHexSummonPlacementMode::RandomFreeCells)
	{
		TArray<FIntPoint> RandomCells;
		const int32 TargetCount = Summoner->GetSummonUnitCount();

		while (TargetCells.Num() > 0 && RandomCells.Num() < TargetCount)
		{
			const int32 RandomIndex = FMath::RandRange(0, TargetCells.Num() - 1);
			RandomCells.Add(TargetCells[RandomIndex]);
			TargetCells.RemoveAt(RandomIndex);
		}

		TargetCells = RandomCells;
	}

	return SpawnSummonedUnitsAtCells(Summoner, TargetCells);
}

bool AHexGridActor::SpawnSummonedUnitFromSummonerAt(AHexUnitActor* Summoner, int32 Q, int32 R)
{
	TArray<FIntPoint> TargetCells;
	GetSummonTargetCellsForSelectedCell(Summoner, FIntPoint(Q, R), TargetCells);
	return SpawnSummonedUnitsAtCells(Summoner, TargetCells);
}

void AHexGridActor::DamageSelectedOrHoveredUnit()
{
	AHexUnitActor* TargetUnit = nullptr;

	if (IsValid(SelectedUnit) && !SelectedUnit->GetIsDead())
	{
		TargetUnit = SelectedUnit;
	}

	if (!TargetUnit && HoveredInstanceIndex != INDEX_NONE)
	{
		FHexCoord HoveredCoord;
		if (GetCoordByInstanceIndex(HoveredInstanceIndex, HoveredCoord))
		{
			AHexUnitActor* HoveredUnit = GetUnitAtCell(HoveredCoord.Q, HoveredCoord.R);
			if (IsValid(HoveredUnit) && !HoveredUnit->GetIsDead())
			{
				TargetUnit = HoveredUnit;
			}
		}
	}

	if (!TargetUnit)
	{
		UE_LOG(LogTemp, Warning, TEXT("DamageSelectedOrHoveredUnit: no alive selected or hovered unit."));
		return;
	}

	TargetUnit->TakeUnitDamage(DebugDamageAmount);
	UpdateSelectedUnitWidget();

	UE_LOG(LogTemp, Log, TEXT("Debug damage: Unit=%s Damage=%d Health=%d/%d"),
		*GetNameSafe(TargetUnit),
		DebugDamageAmount,
		TargetUnit->CurrentHealth,
		TargetUnit->MaxHealth
	);
}

void AHexGridActor::GetReachableMoveCells(
	int32 StartQ,
	int32 StartR,
	int32 MaxSteps,
	TArray<FHexCoord>& OutCells
) const
{
	OutCells.Empty();

	if (MaxSteps <= 0)
	{
		return;
	}

	const FIntPoint Start(StartQ, StartR);

	if (!HasCell(Start.X, Start.Y))
	{
		return;
	}

	TQueue<FIntPoint> Queue;
	TSet<FIntPoint> Visited;
	TMap<FIntPoint, int32> DistanceByCoord;

	Queue.Enqueue(Start);
	Visited.Add(Start);
	DistanceByCoord.Add(Start, 0);

	while (!Queue.IsEmpty())
	{
		FIntPoint Current;
		Queue.Dequeue(Current);

		const int32* CurrentDistancePtr = DistanceByCoord.Find(Current);
		if (!CurrentDistancePtr)
		{
			continue;
		}

		const int32 CurrentDistance = *CurrentDistancePtr;

		if (CurrentDistance >= MaxSteps)
		{
			continue;
		}

		const TArray<FIntPoint> Neighbors = GetHexNeighbors(Current);

		for (const FIntPoint& Neighbor : Neighbors)
		{
			if (Visited.Contains(Neighbor))
			{
				continue;
			}

			if (!HasCell(Neighbor.X, Neighbor.Y))
			{
				continue;
			}

			//                                                          .
			if (IsCellOccupied(Neighbor.X, Neighbor.Y))
			{
				continue;
			}

			const int32 NextDistance = CurrentDistance + 1;

			Visited.Add(Neighbor);
			DistanceByCoord.Add(Neighbor, NextDistance);
			Queue.Enqueue(Neighbor);

			OutCells.Add(FHexCoord(Neighbor.X, Neighbor.Y));
		}
	}
}

void AHexGridActor::GetAttackRangeCells(
	int32 StartQ,
	int32 StartR,
	int32 MaxRange,
	TArray<FHexCoord>& OutCells
) const
{
	OutCells.Empty();

	if (MaxRange <= 0)
	{
		return;
	}

	if (!HasCell(StartQ, StartR))
	{
		return;
	}

	for (const FHexCell& Cell : Cells)
	{
		const int32 Distance = GetHexDistance(StartQ, StartR, Cell.Q, Cell.R);

		if (Distance <= 0)
		{
			continue;
		}

		if (Distance <= MaxRange)
		{
			OutCells.Add(FHexCoord(Cell.Q, Cell.R));
		}
	}
}

int32 AHexGridActor::GetHexDistance(int32 AQ, int32 AR, int32 BQ, int32 BR) const
{
	const int32 DQ = BQ - AQ;
	const int32 DR = BR - AR;
	const int32 DS = -DQ - DR;

	return (FMath::Abs(DQ) + FMath::Abs(DR) + FMath::Abs(DS)) / 2;
}

bool AHexGridActor::FindPath(int32 StartQ, int32 StartR, int32 TargetQ, int32 TargetR, TArray<FHexCoord>& OutPath) const
{
	OutPath.Empty();

	const FIntPoint Start(StartQ, StartR);
	const FIntPoint Target(TargetQ, TargetR);

	if (!HasCell(Start.X, Start.Y) || !HasCell(Target.X, Target.Y))
	{
		return false;
	}

	if (Start == Target)
	{
		return true;
	}

	if (IsCellOccupied(Target.X, Target.Y))
	{
		return false;
	}

	TQueue<FIntPoint> Queue;
	TSet<FIntPoint> Visited;
	TMap<FIntPoint, FIntPoint> CameFrom;

	Queue.Enqueue(Start);
	Visited.Add(Start);

	while (!Queue.IsEmpty())
	{
		FIntPoint Current;
		Queue.Dequeue(Current);

		if (Current == Target)
		{
			break;
		}

		const TArray<FIntPoint> Neighbors = GetHexNeighbors(Current);

		for (const FIntPoint& Neighbor : Neighbors)
		{
			if (Visited.Contains(Neighbor))
			{
				continue;
			}

			if (!HasCell(Neighbor.X, Neighbor.Y))
			{
				continue;
			}

			if (Neighbor != Target && IsCellOccupied(Neighbor.X, Neighbor.Y))
			{
				continue;
			}

			Visited.Add(Neighbor);
			CameFrom.Add(Neighbor, Current);
			Queue.Enqueue(Neighbor);
		}
	}

	if (!Visited.Contains(Target))
	{
		return false;
	}

	TArray<FIntPoint> ReversedPath;

	FIntPoint Current = Target;
	while (Current != Start)
	{
		ReversedPath.Add(Current);

		const FIntPoint* Previous = CameFrom.Find(Current);
		if (!Previous)
		{
			return false;
		}

		Current = *Previous;
	}

	for (int32 Index = ReversedPath.Num() - 1; Index >= 0; --Index)
	{
		OutPath.Add(FHexCoord(ReversedPath[Index].X, ReversedPath[Index].Y));
	}

	return OutPath.Num() > 0;
}

TArray<FIntPoint> AHexGridActor::GetHexNeighbors(const FIntPoint& Coord) const
{
	static const FIntPoint Directions[6] =
	{
		FIntPoint(1, 0),
		FIntPoint(1, -1),
		FIntPoint(0, -1),
		FIntPoint(-1, 0),
		FIntPoint(-1, 1),
		FIntPoint(0, 1)
	};

	TArray<FIntPoint> Result;
	Result.Reserve(6);

	for (const FIntPoint& Direction : Directions)
	{
		Result.Add(FIntPoint(Coord.X + Direction.X, Coord.Y + Direction.Y));
	}

	return Result;
}


void AHexGridActor::GetHexCornerWorldLocations(int32 Q, int32 R, TArray<FVector>& OutCorners) const
{
	OutCorners.Empty();
	OutCorners.Reserve(6);

	const FVector LocalCenter = AxialToLocal(Q, R);
	const FTransform GridTransform = HexMeshComponent
		? HexMeshComponent->GetComponentTransform()
		: GetActorTransform();

	const float Radius = HexRadius * FMath::Max(0.01f, AttackOutlineRadiusScale);

	// FlatTop:                /            , PointyTop:               .
	const float AngleOffsetDegrees = (Orientation == EHexOrientation::FlatTop) ? 0.0f : 30.0f;

	for (int32 CornerIndex = 0; CornerIndex < 6; ++CornerIndex)
	{
		const float AngleRadians = FMath::DegreesToRadians(AngleOffsetDegrees + 60.0f * static_cast<float>(CornerIndex));

		const FVector LocalCorner(
			LocalCenter.X + Radius * FMath::Cos(AngleRadians),
			LocalCenter.Y + Radius * FMath::Sin(AngleRadians),
			LocalCenter.Z
		);

		FVector WorldCorner = GridTransform.TransformPosition(LocalCorner);
		WorldCorner.Z += AttackOutlineZOffset;

		OutCorners.Add(WorldCorner);
	}
}

int32 AHexGridActor::FindEdgeIndexFacingNeighbor(int32 Q, int32 R, const FIntPoint& NeighborCoord) const
{
	TArray<FVector> Corners;
	GetHexCornerWorldLocations(Q, R, Corners);

	if (Corners.Num() != 6)
	{
		return 0;
	}

	const FTransform GridTransform = HexMeshComponent
		? HexMeshComponent->GetComponentTransform()
		: GetActorTransform();

	FVector CellCenter = GridTransform.TransformPosition(AxialToLocal(Q, R));
	FVector NeighborCenter = GridTransform.TransformPosition(AxialToLocal(NeighborCoord.X, NeighborCoord.Y));

	FVector ToNeighbor = NeighborCenter - CellCenter;
	ToNeighbor.Z = 0.0f;
	ToNeighbor.Normalize();

	int32 BestEdgeIndex = 0;
	float BestDot = -2.0f;

	for (int32 EdgeIndex = 0; EdgeIndex < 6; ++EdgeIndex)
	{
		const FVector EdgeMidpoint = (Corners[EdgeIndex] + Corners[(EdgeIndex + 1) % 6]) * 0.5f;

		FVector ToEdge = EdgeMidpoint - CellCenter;
		ToEdge.Z = 0.0f;
		ToEdge.Normalize();

		const float Dot = FVector::DotProduct(ToNeighbor, ToEdge);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestEdgeIndex = EdgeIndex;
		}
	}

	return BestEdgeIndex;
}

void AHexGridActor::DrawAttackRangeOutline() const
{
	if (!bDrawAttackRangeOutline)
	{
		return;
	}

	if (CurrentAttackBorderSet.Num() == 0 || CurrentAttackRangeSet.Num() == 0)
	{
		return;
	}

	if (!GetWorld())
	{
		return;
	}

	//                                              .
	//   CurrentAttackRangeSet                            ,                              ,
	//           AttackRange=1                                           .
	TSet<FIntPoint> FilledAttackRegion = CurrentAttackRangeSet;
	if (IsValid(SelectedUnit))
	{
		FilledAttackRegion.Add(SelectedUnit->GetGridCoord());
	}

	const FColor LineColor = AttackOutlineColor.ToFColor(true);

	for (const FIntPoint& CellCoord : CurrentAttackBorderSet)
	{
		TArray<FVector> Corners;
		GetHexCornerWorldLocations(CellCoord.X, CellCoord.Y, Corners);

		if (Corners.Num() != 6)
		{
			continue;
		}

		const TArray<FIntPoint> Neighbors = GetHexNeighbors(CellCoord);
		for (const FIntPoint& NeighborCoord : Neighbors)
		{
			//                       ,                                            .
			//                                                        .
			if (FilledAttackRegion.Contains(NeighborCoord))
			{
				continue;
			}

			const int32 EdgeIndex = FindEdgeIndexFacingNeighbor(CellCoord.X, CellCoord.Y, NeighborCoord);
			const FVector Start = Corners[EdgeIndex];
			const FVector End = Corners[(EdgeIndex + 1) % 6];

			DrawDebugLine(
				GetWorld(),
				Start,
				End,
				LineColor,
				false,
				0.0f,
				0,
				AttackOutlineThickness
			);
		}
	}
}

void AHexGridActor::UpdateHoverUnderCursor()
{
	FHitResult HitResult;

	if (!TraceHexUnderCursor(HitResult))
	{
		SetHoveredInstance(INDEX_NONE);
		return;
	}

	const int32 HitInstanceIndex = HitResult.Item;

	if (HitInstanceIndex == INDEX_NONE)
	{
		SetHoveredInstance(INDEX_NONE);
		return;
	}

	//                                          ,             hover-         .
	if (IsDeadUnitInstance(HitInstanceIndex))
	{
		SetHoveredInstance(INDEX_NONE);
		UpdateInstanceVisualState(HitInstanceIndex);
		return;
	}

	SetHoveredInstance(HitInstanceIndex);
}

bool AHexGridActor::TraceHexUnderCursor(FHitResult& OutHit) const
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !HexMeshComponent)
	{
		return false;
	}

	const bool bHit = PlayerController->GetHitResultUnderCursorByChannel(
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		OutHit
	);

	if (!bHit)
	{
		return false;
	}

	if (OutHit.GetComponent() != HexMeshComponent)
	{
		return false;
	}

	return true;
}

void AHexGridActor::SetHoveredInstance(int32 NewHoveredInstanceIndex)
{
	if (IsDeadUnitInstance(NewHoveredInstanceIndex))
	{
		NewHoveredInstanceIndex = INDEX_NONE;
	}

	if (HoveredInstanceIndex == NewHoveredInstanceIndex)
	{
		return;
	}

	const int32 OldHoveredInstanceIndex = HoveredInstanceIndex;
	HoveredInstanceIndex = NewHoveredInstanceIndex;

	if (OldHoveredInstanceIndex != INDEX_NONE)
	{
		UpdateInstanceVisualState(OldHoveredInstanceIndex);
	}

	if (HoveredInstanceIndex != INDEX_NONE)
	{
		UpdateInstanceVisualState(HoveredInstanceIndex);

		if (const FIntPoint* Coord = InstanceToCoord.Find(HoveredInstanceIndex))
		{
			OnHexCellHovered(Coord->X, Coord->Y, HoveredInstanceIndex);
		}
	}
}

void AHexGridActor::SetSelectedInstance(int32 NewSelectedInstanceIndex)
{
	if (IsDeadUnitInstance(NewSelectedInstanceIndex))
	{
		NewSelectedInstanceIndex = INDEX_NONE;
	}

	if (SelectedInstanceIndex == NewSelectedInstanceIndex)
	{
		return;
	}

	const int32 OldSelectedInstanceIndex = SelectedInstanceIndex;
	SelectedInstanceIndex = NewSelectedInstanceIndex;

	if (OldSelectedInstanceIndex != INDEX_NONE)
	{
		UpdateInstanceVisualState(OldSelectedInstanceIndex);
	}

	if (SelectedInstanceIndex != INDEX_NONE)
	{
		UpdateInstanceVisualState(SelectedInstanceIndex);
	}
}

void AHexGridActor::UpdateInstanceVisualState(int32 InstanceIndex)
{
	if (!HexMeshComponent || InstanceIndex == INDEX_NONE)
	{
		return;
	}

	const bool bIsDeadUnitCell = IsDeadUnitInstance(InstanceIndex);
	const bool bIsEnemyUnitCell = !bIsDeadUnitCell && IsEnemyUnitInstance(InstanceIndex);

	bool bIsPlayerUnitCell = false;
	bool bIsReachableMoveCell = false;
	bool bIsAbilityPreviewCell = false;

	if (!bIsDeadUnitCell)
	{
		if (const FIntPoint* Coord = InstanceToCoord.Find(InstanceIndex))
		{
			bIsReachableMoveCell = CurrentMoveRangeSet.Contains(*Coord);
			bIsAbilityPreviewCell = CurrentAbilityPreviewRangeSet.Contains(*Coord);

			if (AHexUnitActor* const* UnitPtr = UnitsByCoord.Find(*Coord))
			{
				const AHexUnitActor* Unit = *UnitPtr;
				bIsPlayerUnitCell =
					IsValid(Unit)
					&& !Unit->GetIsDead()
					&& Unit->Team == EHexUnitTeam::Player;
			}
		}
	}

	const bool bIsHovered = !bIsDeadUnitCell && InstanceIndex == HoveredInstanceIndex;
	const bool bIsSelected = !bIsDeadUnitCell && InstanceIndex == SelectedInstanceIndex;
	const bool bWantsHoverOrSelection = bIsHovered || bIsSelected;

	const bool bIsEnemyHighlighted = bIsEnemyUnitCell && bWantsHoverOrSelection;
	const bool bIsAllyHighlighted = bIsPlayerUnitCell && bWantsHoverOrSelection;
	const bool bIsEmptyCellHighlighted =
		bWantsHoverOrSelection
		&& !bIsEnemyUnitCell
		&& !bIsPlayerUnitCell;

	// Material contract:
	// PerInstanceCustomData[0] = empty-cell hover/selection, yellow.
	// PerInstanceCustomData[1] = movement range, blue.
	// PerInstanceCustomData[2] = enemy hover/selection, red.
	// PerInstanceCustomData[3] = attack border, orange.
	// PerInstanceCustomData[4] = ability preview, purple.
	// PerInstanceCustomData[5] = player/allied unit hover/selection, green.

	HexMeshComponent->SetCustomDataValue(
		InstanceIndex,
		0,
		bIsEmptyCellHighlighted ? 1.0f : 0.0f,
		false
	);

	HexMeshComponent->SetCustomDataValue(
		InstanceIndex,
		1,
		bIsReachableMoveCell ? 1.0f : 0.0f,
		false
	);

	HexMeshComponent->SetCustomDataValue(
		InstanceIndex,
		2,
		bIsEnemyHighlighted ? 1.0f : 0.0f,
		false
	);

	HexMeshComponent->SetCustomDataValue(
		InstanceIndex,
		3,
		0.0f,
		false
	);

	HexMeshComponent->SetCustomDataValue(
		InstanceIndex,
		4,
		bIsAbilityPreviewCell ? 1.0f : 0.0f,
		false
	);

	HexMeshComponent->SetCustomDataValue(
		InstanceIndex,
		5,
		bIsAllyHighlighted ? 1.0f : 0.0f,
		true
	);
}

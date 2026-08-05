#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "UObject/SoftObjectPtr.h"
#include "AccountSaveGame.generated.h"

class AHexUnitActor;

/**
 * Persistent progression for one unit type.
 * A soft class reference stores an asset path instead of a process-memory pointer,
 * so it survives closing the game and rebuilding the C++ project.
 */
USTRUCT(BlueprintType)
struct FAccountUnitProgressRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Account Progress")
	TSoftClassPtr<AHexUnitActor> UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Account Progress", meta = (ClampMin = "1", ClampMax = "15"))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Account Progress", meta = (ClampMin = "0"))
	int32 CurrentExperience = 0;
};


/**
 * Persisted snapshot of one faction/role synergy icon.
 * Values are stored as bytes to keep AccountSaveGame independent from ArmyBuilderWidget enums.
 * ArmyBuilder validates/rebuilds these records from SavedArmyUnitClasses after load.
 */
USTRUCT(BlueprintType)
struct FAccountFactionEffectRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Faction Effects")
	uint8 FactionValue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Faction Effects")
	uint8 RoleValue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Faction Effects", meta = (ClampMin = "0", ClampMax = "5"))
	int32 UnitCount = 0;
};



/**
 * Persisted deployment slot for the saved player army.
 */
USTRUCT(BlueprintType)
struct FAccountDeploymentSlotRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Deployment")
	int32 UnitIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Deployment")
	int32 Q = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Deployment")
	int32 R = 0;
};


/**
 * One of five independent army templates.
 * Unit progression and coins remain account-wide; composition, deployment and
 * faction-effect snapshot belong to the individual template.
 */
USTRUCT(BlueprintType)
struct FAccountArmyPresetRecord
{
	GENERATED_BODY()

	// Ordered composition. Duplicate unit classes are intentionally allowed.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Army Presets")
	TArray<TSoftClassPtr<AHexUnitActor>> UnitClasses;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Army Presets")
	TArray<FAccountDeploymentSlotRecord> DeploymentSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Army Presets")
	TArray<FAccountFactionEffectRecord> FactionEffects;
};

/**
 * Disk-backed account data.
 * Character progression is stored by class, while army composition preserves
 * slot order and duplicate classes. Deployment coordinates are also persisted.
 */
UCLASS()
class OTHERBIOS_API UAccountSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account")
	int32 SaveVersion = 5;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Progression")
	TArray<FAccountUnitProgressRecord> UnitProgress;

	// Exactly five reusable army templates. Template 0 is used when migrating old saves.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Army Presets")
	TArray<FAccountArmyPresetRecord> ArmyPresets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Army Presets", meta = (ClampMin = "0", ClampMax = "4"))
	int32 ActiveArmyPresetIndex = 0;

	// Ordered selected army. Duplicate unit classes are intentionally allowed.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Army")
	TArray<TSoftClassPtr<AHexUnitActor>> SavedArmyUnitClasses;

	// Snapshot of active faction-role icons. It is persisted as a safety net and
	// validated/rebuilt from the saved army composition after loading.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Faction Effects")
	TArray<FAccountFactionEffectRecord> FactionEffects;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Deployment")
	TArray<FAccountDeploymentSlotRecord> SavedDeploymentSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Account|Currency")
	int32 Coins = 0;
};

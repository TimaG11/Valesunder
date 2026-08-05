// SUPPORT HEALER NEW v3 - generated 2026-06-20, contains bCanHeal/HealAmount/HealMontage and InHealAmount fix
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "HexGridActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UAudioComponent;
class USoundBase;
class UUserWidget;
class UTexture2D;
class UMaterialInstanceDynamic;
class AHexUnitActor;
class AHexAttackProjectile;
enum class EArmyBattleExperienceOutcome : uint8;

UENUM(BlueprintType)
enum class EHexGridShape : uint8
{
	Hexagon      UMETA(DisplayName = "Hexagon"),
	Rectangle    UMETA(DisplayName = "Rectangle"),
	WideHexagon  UMETA(DisplayName = "Wide Hexagon")
};

UENUM(BlueprintType)
enum class EHexOrientation : uint8
{
	FlatTop   UMETA(DisplayName = "Flat Top"),
	PointyTop UMETA(DisplayName = "Pointy Top")
};

UENUM(BlueprintType)
enum class EHexTurnOwner : uint8
{
	Player UMETA(DisplayName = "Player Turn"),
	Enemy  UMETA(DisplayName = "Enemy Turn")
};

UENUM(BlueprintType)
enum class EHexMatchResult : uint8
{
	None    UMETA(DisplayName = "None"),
	Victory UMETA(DisplayName = "Victory"),
	Defeat  UMETA(DisplayName = "Defeat")
};

UENUM(BlueprintType)
enum class EHexSelectedActionMode : uint8
{
	None    UMETA(DisplayName = "None"),
	Move    UMETA(DisplayName = "Move"),
	Attack  UMETA(DisplayName = "Attack"),
	Ability UMETA(DisplayName = "Ability")
};

UENUM(BlueprintType)
enum class EHexBotDifficulty : uint8
{
	WarmUp    UMETA(DisplayName = "Warm Up"),
	Challenge UMETA(DisplayName = "Challenge"),
	Ordeal    UMETA(DisplayName = "Ordeal"),
	Nightmare UMETA(DisplayName = "Nightmare")
};

// Optional exact X..Y override for a specific enemy unit count.
// Keep a value at -1 to calculate that endpoint automatically from percentages.
USTRUCT(BlueprintType)
struct FEnemyArmyAbsolutePowerRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power", meta = (ClampMin = "-1"))
	int32 MinPower = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power", meta = (ClampMin = "-1"))
	int32 MaxPower = -1;
};

// Relative power interval for an enemy army at the current unit count.
// 0.0 = the weakest legal composition in the current roster, 1.0 = the strongest.
// Exact per-count overrides below take priority over the corresponding percentage endpoint.
USTRUCT(BlueprintType)
struct FEnemyArmyPowerBand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MinPowerPercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MaxPowerPercent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power|Exact Overrides")
	FEnemyArmyAbsolutePowerRange ThreeUnits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power|Exact Overrides")
	FEnemyArmyAbsolutePowerRange FourUnits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Army Power|Exact Overrides")
	FEnemyArmyAbsolutePowerRange FiveUnits;

	FEnemyArmyPowerBand()
	{
	}

	FEnemyArmyPowerBand(float InMinPowerPercent, float InMaxPowerPercent)
		: MinPowerPercent(InMinPowerPercent), MaxPowerPercent(InMaxPowerPercent)
	{
	}
};

USTRUCT(BlueprintType)
struct FHexCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Q = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 R = 0;

	FHexCoord()
	{
	}

	FHexCoord(int32 InQ, int32 InR)
		: Q(InQ), R(InR)
	{
	}
};

USTRUCT(BlueprintType)
struct FHexCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Q = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 R = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 InstanceIndex = INDEX_NONE;

	FHexCell()
	{
	}

	FHexCell(int32 InQ, int32 InR, int32 InInstanceIndex)
		: Q(InQ), R(InR), InstanceIndex(InInstanceIndex)
	{
	}
};

USTRUCT(BlueprintType)
struct FHexUnitSpawnInfo
{
	GENERATED_BODY()

	//                                   .
	//         : BP_Kingdom_Crossbowman, BP_Knight    . .
	//                     ,                         UnitClass         .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units")
	TSubclassOf<AHexUnitActor> UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units")
	FHexCoord Coord = FHexCoord(0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units")
	bool bEnabled = true;

	FHexUnitSpawnInfo()
	{
	}

	FHexUnitSpawnInfo(TSubclassOf<AHexUnitActor> InUnitClass, const FHexCoord& InCoord)
		: UnitClass(InUnitClass), Coord(InCoord), bEnabled(true)
	{
	}
};

struct FHexBattleExperienceResultRow
{
	FString UnitName;
	int32 Level = 1;
	int32 CurrentExperience = 0;
	int32 RequiredExperience = 0;
	int32 EarnedExperience = 0;
	bool bCanUpgrade = false;
	bool bMaxLevel = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHexGridActionPointsChangedSignature, int32, CurrentActionPoints, int32, MaxActionPoints);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHexGridTurnChangedSignature, EHexTurnOwner, CurrentTurnOwner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHexGridPlayerTurnTimerChangedSignature, float, TimeRemaining, float, TimeLimit);

UCLASS()
class OTHERBIOS_API AHexGridActor : public AActor
{
	GENERATED_BODY()

public:
	AHexGridActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Audio")
	void StartBattleMusic();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Audio")
	void StopBattleMusic(float FadeOutTime = 0.5f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid")
	UHierarchicalInstancedStaticMeshComponent* HexMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid")
	UStaticMesh* HexTileMesh = nullptr;

	// Per-map colors for the existing highlight logic.
	// The hex material must expose vector parameters with the names below.
	// Set these values independently in every BP_HexGrid placed on a map.
	// Multiplies the BaseCellTexture inside the master material.
	// Keep it white to preserve the original texture; tint it per map when needed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor BaseCellColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor NormalHighlightColor = FLinearColor(1.0f, 0.75f, 0.05f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor MoveHighlightColor = FLinearColor(0.05f, 0.35f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor EnemyHighlightColor = FLinearColor(1.0f, 0.05f, 0.03f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor AllyHighlightColor = FLinearColor(0.05f, 1.0f, 0.20f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor AttackHighlightColor = FLinearColor(1.0f, 0.22f, 0.02f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors")
	FLinearColor AbilityHighlightColor = FLinearColor(0.55f, 0.05f, 1.0f, 1.0f);

	// Global glow for the separate border material. The border material must expose
	// GridGlowColor (Vector), GridGlowIntensity (Scalar) and GridGlowEnabled (Scalar).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow")
	FLinearColor GridGlowColor = FLinearColor(1.0f, 0.65f, 0.08f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow", meta = (ClampMin = "0.0"))
	float GridGlowIntensity = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow|Advanced")
	FName GridGlowColorParameterName = TEXT("GridGlowColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow|Advanced")
	FName GridGlowIntensityParameterName = TEXT("GridGlowIntensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow|Advanced")
	FName GridGlowEnabledParameterName = TEXT("GridGlowEnabled");

	// Material slots used by the six separate border sections of the hex mesh.
	// Element 0 remains the centre material; Elements 1..6 are the six borders.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow|Advanced", meta = (ClampMin = "0"))
	int32 FirstBorderMaterialSlot = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Border Glow|Advanced", meta = (ClampMin = "0"))
	int32 LastBorderMaterialSlot = 6;

	// Parameter names used by the master material. Usually you should leave these unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName BaseCellColorParameterName = TEXT("BaseCellColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName NormalHighlightColorParameterName = TEXT("NormalHighlightColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName MoveHighlightColorParameterName = TEXT("MoveHighlightColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName EnemyHighlightColorParameterName = TEXT("EnemyHighlightColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName AllyHighlightColorParameterName = TEXT("AllyHighlightColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName AttackHighlightColorParameterName = TEXT("AttackHighlightColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Material Colors|Advanced")
	FName AbilityHighlightColorParameterName = TEXT("AbilityHighlightColor");

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hex Grid|Material Colors")
	void ApplyHexMaterialColors();

	// Creates one dynamic border material and assigns it to all six border slots.
	void InitializeHexBorderMaterial();

	// Enables/disables the global emissive glow on the separate hex-border material.
	void ApplyHexBorderGlow(bool bEnabled);

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* HexBorderMaterialInstance = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid")
	EHexGridShape GridShape = EHexGridShape::Hexagon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid")
	EHexOrientation Orientation = EHexOrientation::FlatTop;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid", meta = (ClampMin = "1.0"))
	float HexRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid", meta = (ClampMin = "0"))
	int32 GridRadius = 5;

	//                         Grid Shape = Wide Hexagon.
	//                                              .
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Wide Hexagon", meta = (ClampMin = "0"))
	int32 WideHexExtraWidth = 3;

	//                         Grid Shape = Wide Hexagon.
	//                            ,                      .
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Wide Hexagon", meta = (ClampMin = "0"))
	int32 WideHexTopBottomCrop = 1;

	//                         Grid Shape = Wide Hexagon.
	//                     :                                      .
	// 0 =           , 1 =           1                                     ,
	// 2 =           2                                                 .
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Wide Hexagon", meta = (ClampMin = "0"))
	int32 WideHexLeftRightCrop = 0;

	//                         Grid Shape = Wide Hexagon.
	//                    :                                                  .
	//     Flat Top             Q-        .
	// 0 =           , 1 =                                   ,
	// 2 =                                                   .
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Wide Hexagon", meta = (ClampMin = "0"))
	int32 WideHexVerticalSideCrop = 0;

	//                         Grid Shape = Wide Hexagon.
	//                          :            1               
	//                                          .
	// 0 =           , 1 =        1                        ,
	// 2 =        3                        , 3 =        5                   .
	//                               ,                                                 .
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Wide Hexagon", meta = (ClampMin = "0"))
	int32 WideHexEdgeStripCrop = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid", meta = (ClampMin = "1"))
	int32 GridWidth = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid", meta = (ClampMin = "1"))
	int32 GridHeight = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid")
	TArray<FHexCoord> RemovedCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Mesh", meta = (ClampMin = "1.0"))
	float MeshSourceRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Mesh")
	bool bScaleMeshToHexRadius = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Mesh")
	float ExtraMeshScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Mesh")
	float MeshYawDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid")
	TArray<FHexCell> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Interaction")
	int32 HoveredInstanceIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Interaction")
	int32 SelectedInstanceIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units")
	TSubclassOf<AHexUnitActor> UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units")
	bool bSpawnInitialUnit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units")
	FHexCoord InitialUnitCoord = FHexCoord(0, 0);

	//                                                      .
	//                      ,                            .
	//             UnitClass + InitialUnitCoord                           InitialUnits       .
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units")
	TArray<FHexUnitSpawnInfo> InitialUnits;

	// If true, the battle uses the saved Army Builder army.
	// When this is enabled and no ready army is saved, legacy InitialUnits are not spawned by default.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	bool bUseArmyBuilderDeployment = true;

	// Keep this false for the real menu flow. Turn it on only for quick map/debug tests
	// where you intentionally want InitialUnits to spawn without saving an army.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	bool bAllowInitialUnitsFallbackWithoutSavedArmy = false;

	// Top card in the selected army uses index 0. Later indexes are placed closer to the camera.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	TArray<FHexCoord> PlayerArmyDeploymentCoords;

	// Enemy coordinates. Enemy classes are shuffled before spawning if bRandomizeEnemyArmyOrder is true.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	TArray<FHexCoord> EnemyArmyDeploymentCoords;

	// Keep false for the normal game. If true, player army is used only as an emergency fallback
	// when no enemy pool / no Army Builder available roster exists.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	bool bMirrorSavedPlayerArmyForEnemies = false;

	// Optional extra enemy pool. The real enemy selection pool is built from this array
	// plus the full AvailableUnitClasses roster cached by WBP_ArmyBuilderWidget.
	// Classes do not need Team=Enemy in BP: spawn overrides team at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	TArray<TSubclassOf<AHexUnitActor>> EnemyArmyPoolUnitClasses;

	// If true, enemy gets the same number of units as the saved player army.
	// The supported battle size is always 3..5 units.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	bool bUsePlayerArmyUnitCountForRandomEnemyArmy = true;

	// Used only when bUsePlayerArmyUnitCountForRandomEnemyArmy is false.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder", meta = (ClampMin = "3", ClampMax = "5"))
	int32 EnemyArmyUnitCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units|Army Builder")
	bool bRandomizeEnemyArmyOrder = true;

	// Enables guaranteed lower/upper army-power thresholds based on bot difficulty.
	// The exact numeric interval is recalculated for the selected army size (3, 4 or 5 units).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units|Army Builder|Enemy Power")
	bool bUseDifficultyBasedEnemyArmyPower = true;

	// Enemy units are evaluated and spawned at this level. Keep 1 for the current progression stage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units|Army Builder|Enemy Power", meta = (ClampMin = "1", ClampMax = "15"))
	int32 EnemyArmyLevel = 1;

	// Defaults: weakest 35% of legal compositions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units|Army Builder|Enemy Power")
	FEnemyArmyPowerBand WarmUpEnemyArmyPowerBand;

	// Defaults: 35%..60% of the legal power interval.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units|Army Builder|Enemy Power")
	FEnemyArmyPowerBand ChallengeEnemyArmyPowerBand;

	// Defaults: 60%..85% of the legal power interval.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units|Army Builder|Enemy Power")
	FEnemyArmyPowerBand OrdealEnemyArmyPowerBand;

	// Defaults: exactly the strongest legal level-1 composition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Units|Army Builder|Enemy Power")
	FEnemyArmyPowerBand NightmareEnemyArmyPowerBand;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hex Grid|Units")
	float UnitHeightOffset = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hex Grid|Units")
	AHexUnitActor* SelectedUnit = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hex Grid|Movement")
	TArray<FHexCoord> CurrentMoveRangeCells;

	//                                                 .                                         .
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hex Grid|Combat")
	TArray<FHexCoord> CurrentAttackRangeCells;

	//           preview-                         ,                                     AbilityButton.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hex Grid|Champion Ability|Preview")
	TArray<FHexCoord> CurrentAbilityPreviewRangeCells;

	//                     .
	// Player:                                  ,                      .
	// Enemy:                        ,                                              .
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Turns")
	EHexTurnOwner CurrentTurnOwner = EHexTurnOwner::Player;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Turns")
	bool bEnemyTurnInProgress = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns")
	bool bEnableEnemyBot = true;

	// Difficulty used by the enemy bot. The game mode select screen passes it through level options.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Difficulty")
	EHexBotDifficulty EnemyBotDifficulty = EHexBotDifficulty::WarmUp;

	// If true, battle map reads ?BotDifficulty=WarmUp/Challenge/Ordeal/Nightmare from OpenLevel options.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Difficulty")
	bool bReadEnemyBotDifficultyFromLevelOptions = true;

	//                            .      ,                                  ,                          .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns", meta = (ClampMin = "0.0"))
	float EnemyBotStepDelay = 0.15f;

	//                        hit-reaction/         /                ,                                            .
	//                                                     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns", meta = (ClampMin = "0.0"))
	float EnemyBotBusyRetryDelay = 0.10f;

	//                                ,                                                  -                  .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns", meta = (ClampMin = "0"))
	int32 EnemyBotMaxBusyRetries = 30;

	//                            ,                         MoveActionPointCost = 0.
	//                                    ,                      :                      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns", meta = (ClampMin = "1"))
	int32 EnemyBotMaxMovesPerTurn = 16;

	//                                       AI     Behavior Tree.
	//                          BP      ,                                         .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyBotRetreatHealthPercent = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotAttackOpportunityBonus = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotKillScoreBonus = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotThreatenedCellPenalty = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "1.0"))
	float EnemyBotLowHealthThreatMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotRetreatDistanceScore = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotRandomScoreJitter = 8.0f;

	//                ,                                                                .
	//                     /         :                           ,                               .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotFutureThreatPenalty = 55.0f;

	//                                                           ,                           ,
	//                                        .
	//                                               AttackRange - 1,
	//                   AttackRange=2                            2                .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "1"))
	int32 EnemyBotRangedTooCloseDistance = 2;

	//              ,                                                      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI", meta = (ClampMin = "0.0"))
	float EnemyBotKiteAttackPositionBonus = 280.0f;

	//           Ram/                                                            .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Roles", meta = (ClampMin = "0.0"))
	float EnemyBotRamAdvanceScore = 95.0f;

	//                                        Ram.        1.0 =                                    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Roles", meta = (ClampMin = "0.0"))
	float EnemyBotRamThreatPenaltyMultiplier = 0.45f;

	//       Skirmisher/                                        Ram.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Roles", meta = (ClampMin = "0.0"))
	float EnemyBotSkirmisherTankGuardScore = 150.0f;

	//                                                                                 .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Roles", meta = (ClampMin = "0.0"))
	float EnemyBotRangedPreferredDistanceScore = 70.0f;

	//                         ,                                             .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Roles", meta = (ClampMin = "0.0"))
	float EnemyBotSupportHealPositionBonus = 520.0f;

	//                                                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Roles", meta = (ClampMin = "0.0"))
	float EnemyBotSupportDangerPenalty = 180.0f;

	// Keeps the whole enemy army closer to one battle line instead of letting one unit rush alone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotFormationCohesionScore = 130.0f;

	// Bonus for moving toward an ally that is currently taking pressure.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotAssistAllyScore = 260.0f;

	// PRE-EMPTIVE REINFORCEMENT:
	// Number of future combat turns used when deciding whether a frontline ally can handle
	// the local fight. The forecast uses MaxHealth, so it can request help BEFORE the ally
	// has already lost half of its HP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "1", ClampMax = "4"))
	int32 EnemyBotReinforcementPredictionTurns = 2;

	// A support request is considered when projected incoming damage over the forecast horizon
	// reaches this share of the ally's FULL MaxHealth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "0.20", ClampMax = "2.00"))
	float EnemyBotReinforcementFullHealthDangerRatio = 0.70f;

	// If projected enemy damage per turn is this many times larger than the damage already
	// contributed by the local defenders, the bot asks for reinforcements early.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "0.50", ClampMax = "3.00"))
	float EnemyBotReinforcementPowerDisadvantageRatio = 1.10f;

	// A unit inside this radius of the pressured ally is treated as already being part of
	// that local engagement when it can currently attack one of the threatening player units.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "1", ClampMax = "6"))
	int32 EnemyBotReinforcementLocalSupportRange = 3;

	// Once a free helper reaches this distance from the pressured ally, it is considered to
	// have arrived at the engagement and the need calculation is re-run before pulling more units.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "1", ClampMax = "6"))
	int32 EnemyBotReinforcementArrivalRange = 3;

	// Weight for a player unit that cannot attack the ally now but can move + attack it next turn.
	// This is deliberately substantial so the AI reacts before taking damage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyBotReinforcementFutureThreatWeight = 0.72f;

	// Dominant movement reward for an idle unit that closes distance to a losing engagement.
	// This is intentionally much larger than ordinary formation/advance scores.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "0.0"))
	float EnemyBotPreemptiveReinforcementMoveScore = 1800.0f;

	// Additional reward after a helper reaches the local engagement radius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Reinforcement", meta = (ClampMin = "0.0"))
	float EnemyBotPreemptiveReinforcementArrivalScore = 2600.0f;

	// Bonus for moves/attacks that keep following the current multi-turn focus target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotPlanCommitmentScore = 220.0f;

	// Penalty when a unit runs too far ahead of the army line without an immediate attack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotFrontlineSpreadPenalty = 110.0f;

	// Move must pass this score unless it is an attack setup, heal setup, retreat, or ally assist.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics")
	float EnemyBotPointlessMoveScoreThreshold = 180.0f;

	// Global turn planner bonus for a move that leaves enough AP to attack immediately afterwards.
	// This is deliberately large: move -> attack must beat moving a second unrelated unit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotMoveIntoAttackBonus = 7200.0f;

	// Penalizes packing several allies into adjacent hexes. Cohesion should produce a line/screen, not a blob.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotAdjacentCrowdingPenalty = 520.0f;

	// Small reward for healthy local spacing inside the formation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotHealthySpacingScore = 160.0f;

	// Preferred distance to the nearest ally. 2 usually leaves one visible hex of breathing room.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "1", ClampMax = "4"))
	int32 EnemyBotPreferredFormationSpacing = 2;

	// A normal non-attacking move must stay connected to at least one ally inside this range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "1", ClampMax = "6"))
	int32 EnemyBotFormationLinkRange = 3;

	// Maximum difference between the foremost and rearmost unit measured by distance to the player.
	// This is the hard anti-"3 fight, 2 watch from spawn" rule.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "1", ClampMax = "8"))
	int32 EnemyBotMaxFormationDepth = 2;

	// Maximum normal pairwise army diameter. A broken formation may exceed it temporarily,
	// but non-attacking moves are then allowed only when they reduce the diameter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "3", ClampMax = "12"))
	int32 EnemyBotMaxFormationDiameter = 5;

	// Dominant score for reconnecting a split formation / catching up a detached subgroup.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotFormationRepairScore = 950.0f;

	// Support/ranged units retreat only from direct pressure, not just because a tank is fighting nearby.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0"))
	int32 EnemyBotSupportDirectThreatRetreatDamage = 1;

	// Weight of the cheap multi-turn lookahead. Warm Up uses depth 1, then 2/3/4 by difficulty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotLookaheadScoreWeight = 0.35f;

	// Reward for every hex that moves a unit closer to the player army while it is not retreating.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotArmyAdvanceScore = 190.0f;

	// Fast march before contact. Active only while NO enemy unit can reach any player
	// with move + attack this turn. Once contact is possible, normal tactical AI takes over.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Approach", meta = (ClampMin = "0.0"))
	float EnemyBotApproachAdvanceScore = 1250.0f;

	// During the march, units at the rear are deliberately moved first. This prevents the
	// front line from waiting for the back line while the back line waits for the front.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Approach", meta = (ClampMin = "0.0"))
	float EnemyBotApproachRearPriorityScore = 1150.0f;

	// Temporary extra depth/diameter allowed while the formation is translating across the map.
	// Connectivity is still a hard rule, so this cannot recreate the old 3+2 split.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Approach", meta = (ClampMin = "0", ClampMax = "4"))
	int32 EnemyBotApproachFormationSlack = 2;

	// Strong penalty for sideways/non-advancing movement during the pre-contact march.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Approach", meta = (ClampMin = "0.0"))
	float EnemyBotApproachNoProgressPenalty = 1800.0f;

	// Extra reward for a unit that is behind the common battle line and catches up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotLaggingUnitCatchUpScore = 260.0f;

	// Penalty for walking backwards without a real retreat reason.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotBackwardMovePenalty = 170.0f;

	// Normal maximum distance behind the foremost allied unit. Ranged units may use their attack range if it is larger.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "1"))
	int32 EnemyBotMaxBacklineLag = 3;

	// Makes every useful unit receive one action before the same unit repeatedly spends the shared AP pool.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotFirstActionPriorityBonus = 6000.0f;

	// Extra priority for melee/frontline units that still have not acted this turn.
	// This makes tanks and melee champions establish the line before ranged units walk forward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotFrontlineFirstActionBonus = 5200.0f;

	// Backline units wait for an unacted frontline unless they already have a useful attack or heal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotBacklineWaitForFrontlinePenalty = 4200.0f;

	// Large penalty when a ranged/support unit tries to stand closer to the player than the living melee line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotBacklineAheadOfFrontPenalty = 2600.0f;

	// Ranged/support candidate cells are rejected when predicted next-turn damage reaches this share of current HP and no melee screen is nearby.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float EnemyBotBacklineUnsafeDamagePercent = 0.35f;

	// Maximum hex distance from a backline unit to a melee ally that counts as protection.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "1"))
	int32 EnemyBotBacklineScreenRange = 2;

	// Applied for every action already performed by the same unit this turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Group Tactics", meta = (ClampMin = "0.0"))
	float EnemyBotRepeatActionPenalty = 2600.0f;

	// Last Stand uses future-move threat only when predicted damage exceeds current HP by this safety margin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Ability Use", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyBotLastStandFutureDamageSafetyPercent = 0.10f;

	// A summon cell must reach this utility before the bot spends the champion ability.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Ability Use")
	float EnemyBotSummonMinimumUtilityScore = 900.0f;

	// Weak summons are rejected unless one guaranteed attack covers at least this part of the target's current HP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Ability Use", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyBotSummonMinimumDamageFraction = 0.35f;

	// When true, the bot summons only into a cell from which the summoned unit can attack immediately this enemy turn.
	// This is intentionally strict for short-lived summons.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Ability Use")
	bool bEnemyBotSummonRequiresImmediateAttack = true;

	// Newly summoned units with an immediate attack act before unrelated units spend the remaining AP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Ability Use", meta = (ClampMin = "0.0"))
	float EnemyBotImmediateSummonActionPriorityBonus = 12000.0f;


	// Hard bonus for kills. Higher values make the bot finish wounded units instead of spreading damage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotExecuteKillBonus = 1800.0f;

	// Bonus for attacking a target that other bot units can also damage soon.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotFocusFireBonus = 420.0f;

	// Penalty when too many units chase the same weak target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotOvercommitPenalty = 280.0f;

	// Penalty for moving into a cell where the player can probably kill this unit next turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotFreeKillPenalty = 950.0f;

	// Bonus/penalty for trades. Positive if the bot kills a more valuable unit than it risks losing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotTradeScore = 380.0f;

	// Bonus for tanks/frontliners standing between the player and allied ranged/support units.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotBacklineScreenScore = 320.0f;

	// Penalty for occupying a cell that likely blocks a stronger ally from reaching the focus target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotBlockingAllyPenalty = 260.0f;

	// Bonus for punishing player units that moved away from their army.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotIsolatedTargetBonus = 520.0f;

	// Bonus for side pressure on Ordeal/Nightmare instead of always walking straight into the front.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotFlankPressureScore = 240.0f;

	// Penalty for spending actions to save an ally that is still likely to die afterwards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotDoomedAllySavePenalty = 420.0f;

	// Bonus for summon cells that block access to bot support/champion or trap a high-value player unit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Enemy Bot AI|Advanced Rules", meta = (ClampMin = "0.0"))
	float EnemyBotSummonTacticalCellScore = 360.0f;

	//                                                                                                  ,                                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns")
	bool bAutoEndPlayerTurnWhenActionPointsEmpty = true;

	//                                                                          .
	//                                    ,                                           .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns", meta = (ClampMin = "0.0"))
	float AutoEndPlayerTurnAfterAttackDelay = 0.05f;

	//                          :                      ,                       
	//              ,                                                        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns")
	bool bWaitForPlayerActionsBeforeEnemyTurn = true;

	//                    ,                                                          .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns", meta = (ClampMin = "0.01"))
	float PlayerTurnEndBusyRetryDelay = 0.05f;

	//                   .                   ,                                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer")
	bool bEnablePlayerTurnTimer = true;

	//                                    .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer", meta = (ClampMin = "1.0"))
	float PlayerTurnTimeLimit = 30.0f;

	//                                               .
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Turns|Timer")
	float PlayerTurnTimeRemaining = 0.0f;

	// Starts warning animation when player turn timer reaches this value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning", meta = (ClampMin = "0.0"))
	float PlayerTurnTimerWarningThreshold = 10.0f;

	// Timer color outside warning mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning")
	FLinearColor PlayerTurnTimerNormalColor = FLinearColor(1.0f, 0.78f, 0.24f, 1.0f);

	// First blinking warning color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning")
	FLinearColor PlayerTurnTimerWarningRedColor = FLinearColor(1.0f, 0.05f, 0.02f, 1.0f);

	// Second blinking warning color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning")
	FLinearColor PlayerTurnTimerWarningWhiteColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// How often the color switches. 0.5 = red/white every half second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning", meta = (ClampMin = "0.05"))
	float PlayerTurnTimerWarningBlinkInterval = 0.5f;

	// Pulse cycles per second. 1.0 = one pulse per second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning", meta = (ClampMin = "0.01"))
	float PlayerTurnTimerWarningPulseSpeed = 1.0f;

	// Maximum timer text scale during pulse.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Timer|Warning", meta = (ClampMin = "1.0"))
	float PlayerTurnTimerWarningMaxScale = 1.18f;



	// Per-map battle music. Set this on the BP_HexGrid actor instance placed in each level.
	// Use a looping Sound Cue for seamless playback.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Audio")
	bool bAutoStartBattleMusic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Audio")
	USoundBase* BattleMusic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BattleMusicVolume = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Audio", meta = (ClampMin = "0.0"))
	float BattleMusicFadeInTime = 1.0f;

	//                          :         BATTLE START,       PLAYER TURN     ENEMY TURN.
	//                    ,                                        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence")
	bool bEnableMatchStartSequence = true;

	//      true,                               .      false,              FixedFirstTurnOwner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence")
	bool bRandomizeFirstTurn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence", meta = (EditCondition = "!bRandomizeFirstTurn"))
	EHexTurnOwner FixedFirstTurnOwner = EHexTurnOwner::Player;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence", meta = (ClampMin = "0.0"))
	float MatchStartBannerDuration = 2.0f;

	//                                              .
	//               ,                                  BATTLE START                        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence")
	bool bEnableTurnStartBanners = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence", meta = (ClampMin = "0.0"))
	float TurnStartBannerDuration = 1.4f;

	//                              Turn 1.                               ,                                          .
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Turns|Start Sequence")
	int32 CurrentRoundNumber = 1;

	//           Widget Blueprint                      BATTLE START / PLAYER TURN / ENEMY TURN.
	//                  TextBlock           TurnBannerTitle   TurnBannerSubText.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	TSubclassOf<UUserWidget> TurnBannerWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerRootName = TEXT("TurnBannerRoot");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerTitleTextBlockName = TEXT("TurnBannerTitle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerSubTextBlockName = TEXT("TurnBannerSubText");

	//              :        WBP      Image                 , C++                                                            .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerLeftLineImageName = TEXT("TurnBannerLeftLine");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerRightLineImageName = TEXT("TurnBannerRightLine");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerIconImageName = TEXT("TurnBannerIcon");

	//          Image        WBP                                                .
	//        WBP      Image              , C++                              :
	// Battle Start, Player Turn     Enemy Turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FName TurnBannerImageName = TEXT("TurnBannerImage");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	UTexture2D* MatchStartBannerTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	UTexture2D* PlayerTurnBannerTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	UTexture2D* EnemyTurnBannerTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	int32 TurnBannerZOrder = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FLinearColor MatchStartBannerColor = FLinearColor(1.0f, 0.86f, 0.45f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FLinearColor PlayerTurnBannerColor = FLinearColor(0.45f, 0.78f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	FLinearColor EnemyTurnBannerColor = FLinearColor(1.0f, 0.38f, 0.25f, 1.0f);

	// Fallback,      TurnBannerWidgetClass                .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Turns|Start Sequence|UI")
	bool bShowTurnBannerDebugText = true;


	//                                                :         ,                   Attack,
	//                 ,                                               +     .
	//   WBP                            -      TextBlock           .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	TSubclassOf<UUserWidget> ActionWarningWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	FName ActionWarningRootName = TEXT("WarningRoot");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	FName ActionWarningImageName = TEXT("WarningImage");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	FName ActionWarningTitleTextBlockName = TEXT("WarningTitle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	FName ActionWarningSubTextBlockName = TEXT("WarningSubText");

	//                               BP_HexGrid.                     ,
	//                            ,                                WBP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	UTexture2D* ActionWarningTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	FLinearColor ActionWarningTextColor = FLinearColor(1.0f, 0.86f, 0.55f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	FLinearColor ActionWarningImageColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI")
	int32 ActionWarningZOrder = 200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI", meta = (ClampMin = "0.1"))
	float ActionWarningDuration = 1.5f;

	//                      .                         BP_HexGrid            C++.
	//   C++                                       ,                                                  .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText NotEnoughActionPointsTitle = FText::FromString(TEXT("Not enough action points"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText NotEnoughActionPointsSubText = FText::FromString(TEXT("Need more action points."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText TooFarForAttackTitle = FText::FromString(TEXT("Too far for attack"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText TooFarForAttackSubText = FText::FromString(TEXT("Cannot move and attack this turn."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText AttackAlreadyUsedTitle = FText::FromString(TEXT("Attack already used"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText AttackAlreadyUsedSubText = FText::FromString(TEXT("This unit can attack only once per turn."));

	//                ,                                                        .
	//        Ability                         ,                                                                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText ChampionAbilityNotConfiguredTitle = FText::FromString(TEXT("Ability is not configured"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText ChampionAbilityNotConfiguredSubText = FText::FromString(TEXT("Champion ability logic will be added next."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText SummonNotConfiguredTitle = FText::FromString(TEXT("Summon is not configured"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText SummonNotConfiguredSubText = FText::FromString(TEXT("Set Summoned Unit Class with bIsSummonedUnit enabled."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText InvalidSummonTargetTitle = FText::FromString(TEXT("Cannot summon there"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText InvalidSummonTargetSubText = FText::FromString(TEXT("Choose an empty cell inside ability range."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText InvalidMarkedForDeathTargetTitle = FText::FromString(TEXT("Invalid ability target"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText InvalidMarkedForDeathTargetSubText = FText::FromString(TEXT("Choose an enemy unit inside ability range."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText ChampionAbilityAlreadyActiveTitle = FText::FromString(TEXT("Ability is already active"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText ChampionAbilityAlreadyActiveSubText = FText::FromString(TEXT("Wait until the current ability effect ends."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText ChampionAbilityOnCooldownTitle = FText::FromString(TEXT("Ability is on cooldown"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Warning UI|Text")
	FText ChampionAbilityOnCooldownSubText = FText::FromString(TEXT("Wait until the cooldown ends."));

	//                      : VICTORY / DEFEAT.
	// C++                  WBP,                                               .
	//        WBP                                   ,          , progress bar,                      ,
	//        MAIN MENU          ARMY.                       unlock-                      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	TSubclassOf<UUserWidget> ResultScreenWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultRootName = TEXT("ResultRoot");

	// Image        ResultWindowOverlay. C++                    VictoryResultWindowTexture     DefeatResultWindowTexture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultWindowImageName = TEXT("ResultWindowImage");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultTitleTextBlockName = TEXT("ResultTitleText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultSubTextBlockName = TEXT("ResultSubText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultProgressBarName = TEXT("ResultProgressBar");

	// Optional VerticalBox. When present, C++ creates one XP row and one progress bar for every army unit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultExperienceListName = TEXT("ResultExperienceList");

	//            /           progress bar.                         , C++                        ResultInfoText.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultInfoTextBlockName = TEXT("ResultInfoText");

	// Optional TextBlock dedicated to the reward. If it does not exist, reward text is written into ResultInfoText.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultCoinsTextBlockName = TEXT("ResultCoinsText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultMainMenuButtonName = TEXT("ResultMainMenuButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultArmyButtonName = TEXT("ResultArmyButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultMainMenuButtonTextName = TEXT("ResultMainMenuButtonText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	FName ResultArmyButtonTextName = TEXT("ResultArmyButtonText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText VictoryResultTitle = FText::FromString(TEXT("VICTORY"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText VictoryResultSubText = FText::FromString(TEXT("You won the battle."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText DefeatResultTitle = FText::FromString(TEXT("DEFEAT"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText DefeatResultSubText = FText::FromString(TEXT("Your army has fallen."));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText ResultInfoText = FText::FromString(TEXT(""));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText ResultExperienceUpgradeText = FText::FromString(TEXT("UPGRADE"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText ResultMainMenuButtonText = FText::FromString(TEXT("MAIN MENU"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Text")
	FText ResultArmyButtonText = FText::FromString(TEXT("ARMY"));

	//      /                   .                              victory texture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Textures")
	UTexture2D* VictoryResultWindowTexture = nullptr;

	//      /                      .                              defeat texture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Textures")
	UTexture2D* DefeatResultWindowTexture = nullptr;

	//                 White,                                                      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Textures")
	FLinearColor ResultWindowImageColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Colors")
	FLinearColor VictoryResultTitleColor = FLinearColor(1.0f, 0.86f, 0.38f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Colors")
	FLinearColor DefeatResultTitleColor = FLinearColor(0.88f, 0.20f, 0.16f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Colors")
	FLinearColor ResultSubTextColor = FLinearColor(0.90f, 0.82f, 0.66f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Colors")
	FLinearColor VictoryResultProgressColor = FLinearColor(1.0f, 0.72f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Colors")
	FLinearColor DefeatResultProgressColor = FLinearColor(0.78f, 0.18f, 0.16f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience")
	FLinearColor ResultExperienceNameColor = FLinearColor(0.92f, 0.92f, 0.92f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience")
	FLinearColor ResultExperienceLeaderColor = FLinearColor(0.40f, 0.40f, 0.40f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience")
	FLinearColor ResultExperienceNumberColor = FLinearColor(0.92f, 0.92f, 0.92f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience")
	FLinearColor ResultExperienceEarnedColor = FLinearColor(1.0f, 0.72f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience")
	FLinearColor ResultExperienceProgressColor = FLinearColor(1.0f, 0.72f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience")
	FLinearColor ResultExperienceUpgradeColor = FLinearColor(0.20f, 0.95f, 0.28f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience", meta = (ClampMin = "8", ClampMax = "48"))
	int32 ResultExperienceFontSize = 11;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience", meta = (ClampMin = "2.0", ClampMax = "30.0"))
	float ResultExperienceProgressBarHeight = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Experience", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float ResultExperienceRowSpacing = 4.0f;

	//                             .                                  Main Menu map.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Navigation")
	FName MainMenuLevelName = TEXT("L_MainMenu");

	//           :                                    ,                           ArmyLevelName.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Navigation")
	bool bEnableResultArmyButton = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI|Navigation", meta = (EditCondition = "bEnableResultArmyButton"))
	FName ArmyLevelName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI", meta = (ClampMin = "0"))
	int32 ResultScreenZOrder = 5000;

	//                         ,                                                                          .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI", meta = (ClampMin = "0.0"))
	float MatchResultScreenDelay = 0.85f;

	//      progression-          ,                                  progress bar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VictoryResultProgressPercent = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefeatResultProgressPercent = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	bool bCheckMatchResultAfterUnitDeath = true;

	// true =                                          /         .
	// false =                                           .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	bool bCountSummonedUnitsForMatchResult = true;

	// Fallback,      ResultScreenWidgetClass                .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Match Result UI")
	bool bShowMatchResultDebugText = true;

	// Battle coin reward. The balance is kept for the current game session together with Army Builder data.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins")
	bool bEnableBattleCoinRewards = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins", meta = (ClampMin = "0"))
	int32 BaseBattleCoinReward = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins", meta = (ClampMin = "0.0"))
	float VictoryCoinMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins", meta = (ClampMin = "0.0"))
	float DefeatCoinMultiplier = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins", meta = (ClampMin = "0.0"))
	float SurrenderCoinMultiplier = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Difficulty", meta = (ClampMin = "0.0"))
	float WarmUpCoinMultiplier = 0.80f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Difficulty", meta = (ClampMin = "0.0"))
	float ChallengeCoinMultiplier = 1.00f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Difficulty", meta = (ClampMin = "0.0"))
	float OrdealCoinMultiplier = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Difficulty", meta = (ClampMin = "0.0"))
	float NightmareCoinMultiplier = 1.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Performance", meta = (ClampMin = "0"))
	int32 CoinsPerDefeatedEnemy = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Performance", meta = (ClampMin = "0"))
	int32 CoinsPerSurvivingPlayerUnit = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Performance", meta = (ClampMin = "0"))
	int32 NoPlayerDeathsCoinBonus = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Performance", meta = (ClampMin = "0"))
	int32 FastVictoryCoinBonus = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Performance", meta = (ClampMin = "0.0"))
	float FastVictoryTimeLimit = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Performance", meta = (ClampMin = "0"))
	int32 MaxPerformanceCoinBonus = 30;

	// A very fast surrender gives no coins, which blocks instant surrender farming.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Rewards|Coins|Anti Farm", meta = (ClampMin = "0.0"))
	float MinimumBattleTimeForSurrenderReward = 45.0f;

	//                                        .
	//                              ,                           .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points", meta = (ClampMin = "0"))
	int32 MaxActionPoints = 6;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Action Points")
	int32 CurrentActionPoints = 6;

	//                                     .
	//      bUsePathLengthForMoveCost = false,                                   .
	//      bUsePathLengthForMoveCost = true,                                  .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points", meta = (ClampMin = "0"))
	int32 MoveActionPointCost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points")
	bool bUsePathLengthForMoveCost = false;

	//                               .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points", meta = (ClampMin = "0"))
	int32 AttackActionPointCost = 1;

	//      true,                                                                      .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|Kill Bonus")
	bool bEnableKillActionPointBonus = true;

	//                                          .
	//                                 MaxActionPoints       AddActionPoints().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|Kill Bonus", meta = (ClampMin = "0"))
	int32 KillActionPointBonus = 2;

	//      true,                                                                          .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|Kill Bonus")
	bool bLimitKillActionPointBonusOncePerTurn = true;

	//         UI                      .
	//        Widget Blueprint   TextBlock          ActionPointsText                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|UI")
	TSubclassOf<UUserWidget> ActionPointsWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|UI")
	FName ActionPointsTextBlockName = TEXT("ActionPointsText");

	//              :           TextBlock                              .
	//             ,                                       ActionPointsText.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|UI")
	FName TurnTextBlockName = TEXT("TurnText");

	//              :           TextBlock                        .
	//             ,                                  ActionPointsText.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|UI")
	FName TurnTimerTextBlockName = TEXT("TurnTimerText");

	// Button          Widget Blueprint.                                 .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|UI")
	FName EndTurnButtonName = TEXT("EndTurnButton");

	// TextBlock in WBP_TurnHUD for total battle time. It starts when the first real turn starts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Timer|UI")
	FName BattleTimerTextBlockName = TEXT("BattleTimerText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Timer")
	bool bEnableBattleTimer = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hex Grid|Battle Timer")
	float BattleElapsedTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Timer|UI")
	FText BattleTimerPrefixText = FText::FromString(TEXT("BATTLE"));

	// Hamburger/menu widgets in WBP_TurnHUD.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName BattleMenuButtonName = TEXT("BattleMenuButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName BattleMenuPanelName = TEXT("BattleMenuPanel");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName BattleSettingsButtonName = TEXT("BattleSettingsButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName BattleSurrenderButtonName = TEXT("BattleSurrenderButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName SurrenderConfirmPanelName = TEXT("SurrenderConfirmPanel");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName SurrenderCancelButtonName = TEXT("SurrenderCancelButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI")
	FName SurrenderConfirmButtonName = TEXT("SurrenderConfirmButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI|Text")
	FName BattleSettingsButtonTextName = TEXT("BattleSettingsButtonText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI|Text")
	FName BattleSurrenderButtonTextName = TEXT("BattleSurrenderButtonText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI|Text")
	FName SurrenderTitleTextBlockName = TEXT("SurrenderTitleText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI|Text")
	FName SurrenderSubTextBlockName = TEXT("SurrenderSubText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI|Text")
	FName SurrenderCancelButtonTextName = TEXT("SurrenderCancelButtonText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|UI|Text")
	FName SurrenderConfirmButtonTextName = TEXT("SurrenderConfirmButtonText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|Text")
	FText BattleSettingsButtonText = FText::FromString(TEXT("SETTINGS"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|Text")
	FText BattleSurrenderButtonText = FText::FromString(TEXT("SURRENDER"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|Text")
	FText SurrenderTitleText = FText::FromString(TEXT("SURRENDER?"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|Text")
	FText SurrenderSubText = FText::FromString(TEXT("Are you sure you want to surrender?"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|Text")
	FText SurrenderCancelButtonText = FText::FromString(TEXT("CANCEL"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Battle Menu|Text")
	FText SurrenderConfirmButtonText = FText::FromString(TEXT("SURRENDER"));

	//                                   .
	//   UMG                                                                   WBP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitPanelRootName = TEXT("SelectedUnitPanel");

	//                     ,                                   champion ability.
	//         ,      SelectedUnitPanel                    Canvas Panel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Layout")
	FVector2D SelectedUnitPanelNormalSize = FVector2D(760.0f, 96.0f);

	//                     ,                   Champion                          .
	//                                              +        .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Layout")
	FVector2D SelectedUnitPanelChampionSize = FVector2D(1015.0f, 96.0f);

	//                                       .
	//             Move/Attack,                                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Layout")
	FVector2D SelectedEnemyUnitPanelNormalSize = FVector2D(760.0f, 130.0f);

	//                                    Champion   Ability-       .
	//                                                  ,                                 .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Layout")
	FVector2D SelectedEnemyUnitPanelChampionSize = FVector2D(1015.0f, 130.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitPortraitImageName = TEXT("UnitPortraitImage");

	// Border-                     .      ,          C++                                          .
	//         WBP                        Border_PortraitFrame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitPortraitFrameName = TEXT("Border_PortraitFrame");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitNameTextBlockName = TEXT("UnitNameText");

	// Optional row under the unit name that shows the unit faction.
	// In WBP create a SizeBox with this exact name and put UnitFactionText inside it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitFactionContainerName = TEXT("SizeBox_FactionRow");

	// TextBlock inside UnitFactionContainerName.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitFactionTextBlockName = TEXT("UnitFactionText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitHealthTextBlockName = TEXT("UnitHealthText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitHealthProgressBarName = TEXT("UnitHealthProgressBar");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitDamageTextBlockName = TEXT("UnitDamageText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitAttackRangeTextBlockName = TEXT("UnitAttackRangeText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitMoveRangeTextBlockName = TEXT("UnitMoveRangeText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitOccupiedSlotsTextBlockName = TEXT("UnitOccupiedSlotsText");

	// Optional SizeBox/Border/VerticalBox that contains champion ability cooldown text.
	// In WBP create it with this name and put it as the third stats column.
	// The column is visible only for champions with an ability.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityCooldownColumnName = TEXT("SizeBox_AbilityCooldownColumn");

	// TextBlock inside UnitAbilityCooldownColumnName.
	// Shows current ability cooldown for selected player or enemy champion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityCooldownTextBlockName = TEXT("UnitAbilityCooldownText");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitMoveButtonName = TEXT("MoveButton");

	// Optional TextBlock inside MoveButton. If present, C++ writes the assigned hotkey there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitMoveButtonTextName = TEXT("MoveButtonText");

	//                          SizeBox,                              ,                                 .
	//   WBP            SizeBox     MoveButton   SizeBox_MoveButton.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitMoveButtonContainerName = TEXT("SizeBox_MoveButton");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitAttackButtonName = TEXT("AttackButton");

	// Optional TextBlock inside AttackButton. If present, C++ writes the assigned hotkey there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitAttackButtonTextName = TEXT("AttackButtonText");

	//   WBP            SizeBox     AttackButton   SizeBox_AttackButton.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI")
	FName UnitAttackButtonContainerName = TEXT("SizeBox_AttackButton");

	//                                                         .
	//   WBP Button                   AbilityButton.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityButtonName = TEXT("AbilityButton");

	//   WBP            SizeBox     AbilityButton   SizeBox_AbilityButton.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityButtonContainerName = TEXT("SizeBox_AbilityButton");

	//                TextBlock        AbilityButton.
	//             , C++                                   BP          .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityButtonTextName = TEXT("AbilityButtonText");

	// Optional widget above AbilityButton. Usually this is a SizeBox/Border that contains AbilityDescriptionText.
	// It is shown only while the ability button is held/hovered long enough.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityDescriptionContainerName = TEXT("SizeBox_AbilityDescription");

	// TextBlock inside UnitAbilityDescriptionContainerName.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability")
	FName UnitAbilityDescriptionTextName = TEXT("AbilityDescriptionText");

	// Wrap width for ability description text. 0 = do not force wrapping from C++.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability", meta = (ClampMin = "0.0"))
	float AbilityDescriptionWrapTextAt = 340.0f;

	//                                         AbilityButton                     preview-                .
	//                    ChampionAbilityRange = 0,                     .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability", meta = (ClampMin = "0.0"))
	float AbilityPreviewHoverDelay = 1.0f;

	// Extra invisible margin around AbilityButton used only by C++ mouse checks.
	// This prevents tooltip/layout recalculation from causing false Unhovered events and blinking.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability", meta = (ClampMin = "0.0"))
	float AbilityPreviewMouseHoverPadding = 18.0f;

	// How often C++ checks whether the mouse really left the ability button/tooltip area.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability", meta = (ClampMin = "0.01"))
	float AbilityPreviewMousePollInterval = 0.05f;

	// Number of failed mouse checks before hiding the ability preview.
	// 4 checks at 0.05 seconds = about 0.2 seconds of grace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Selected Unit UI|Champion Ability", meta = (ClampMin = "1"))
	int32 AbilityPreviewMouseOutsidePollsBeforeHide = 4;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hex Grid|Selected Unit UI")
	EHexSelectedActionMode CurrentSelectedActionMode = EHexSelectedActionMode::None;

	//           fallback:      UMG-                      ,                      debug-               .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Action Points|UI")
	bool bShowActionPointsDebugText = true;

	UPROPERTY(BlueprintAssignable, Category = "Hex Grid|Action Points")
	FHexGridActionPointsChangedSignature OnActionPointsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Hex Grid|Turns")
	FHexGridTurnChangedSignature OnTurnChanged;

	UPROPERTY(BlueprintAssignable, Category = "Hex Grid|Turns|Timer")
	FHexGridPlayerTurnTimerChangedSignature OnPlayerTurnTimerChanged;

	//                                                                          .
	//                                                   .
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Combat|Visual")
	bool bDrawAttackRangeOutline = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Combat|Visual")
	FLinearColor AttackOutlineColor = FLinearColor(1.0f, 0.42f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Combat|Visual", meta = (ClampMin = "0.0"))
	float AttackOutlineZOffset = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Combat|Visual", meta = (ClampMin = "0.1"))
	float AttackOutlineThickness = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Combat|Visual", meta = (ClampMin = "0.01"))
	float AttackOutlineRadiusScale = 1.0f;

	//                            :                                                       H.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Debug")
	bool bEnableDebugDamageHotkey = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hex Grid|Debug", meta = (ClampMin = "1"))
	int32 DebugDamageAmount = 25;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid")
	void GenerateGrid();

	UFUNCTION(BlueprintPure, Category = "Hex Grid")
	FVector AxialToLocal(int32 Q, int32 R) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid")
	FVector GetCellWorldLocation(int32 Q, int32 R) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid")
	bool HasCell(int32 Q, int32 R) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid")
	bool IsCellRemoved(int32 Q, int32 R) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid")
	int32 GetInstanceIndex(int32 Q, int32 R) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	bool HasEnoughActionPoints(int32 Cost) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Action Points")
	bool SpendActionPoints(int32 Cost);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Action Points")
	void ResetActionPoints();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Action Points")
	void AddActionPoints(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	int32 CalculateMoveActionPointCost(int32 PathLength) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	int32 CalculateAttackActionPointCost() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	int32 CalculateHealActionPointCost(AHexUnitActor* Healer) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	int32 CalculateSummonActionPointCost(AHexUnitActor* Summoner) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	int32 CalculateChampionAbilityActionPointCost(AHexUnitActor* Champion) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Action Points")
	int32 GetEffectiveMovementRangeForUnit(AHexUnitActor* Unit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Movement")
	int32 GetMovementSpentThisTurnForUnit(AHexUnitActor* Unit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Movement")
	int32 GetRemainingMovementRangeForUnit(AHexUnitActor* Unit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Movement")
	bool HasEnoughMovementRangeForUnit(AHexUnitActor* Unit, int32 PathLength) const;

	void SpendMovementRangeForUnit(AHexUnitActor* Unit, int32 PathLength);
	void ResetMovementRangeForTurnOwner(EHexTurnOwner TurnOwner);

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Combat")
	bool HasUnitAttackedThisTurn(AHexUnitActor* Unit) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Combat")
	bool CanUnitAttackThisTurn(AHexUnitActor* Unit) const;

	void MarkUnitAttackedThisTurn(AHexUnitActor* Unit);
	void ResetAttackUsageForTurnOwner(EHexTurnOwner TurnOwner);

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Turns")
	bool IsPlayerTurn() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Turns")
	bool IsEnemyTurn() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Turns")
	bool IsPlayerInputAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Turns|Timer")
	float GetPlayerTurnTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Turns|Timer")
	float GetPlayerTurnTimePercent() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Battle Timer")
	float GetBattleElapsedTime() const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Battle Timer")
	FText GetBattleElapsedTimeText() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Battle Menu")
	void ToggleBattleMenu();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Battle Menu")
	void ShowBattleMenu(bool bShow);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Battle Menu")
	void ShowSurrenderConfirm(bool bShow);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Battle Menu")
	void RequestPlayerSurrender();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Battle Menu")
	void ConfirmPlayerSurrender();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Turns|Start Sequence")
	void StartMatchIntroSequence();

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Turns|Start Sequence")
	bool IsTurnIntroInProgress() const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Turns")
	void StartPlayerTurn();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Turns")
	void EndPlayerTurn();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Turns")
	void StartEnemyTurn();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Turns")
	void FinishEnemyTurn();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Action Points|UI")
	void UpdateActionPointsWidget();


	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Action Warning UI")
	void ShowActionWarning(const FText& TitleText, const FText& SubtitleText);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Action Warning UI")
	void HideActionWarning();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Selected Unit UI")
	void UpdateSelectedUnitWidget();

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Interaction")
	bool GetCoordByInstanceIndex(int32 InstanceIndex, FHexCoord& OutCoord) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Units")
	AHexUnitActor* SpawnUnitAt(int32 Q, int32 R);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Units")
	AHexUnitActor* SpawnUnitOfClassAt(TSubclassOf<AHexUnitActor> InUnitClass, int32 Q, int32 R);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Units")
	AHexUnitActor* SpawnUnitOfClassForTeamAt(TSubclassOf<AHexUnitActor> InUnitClass, EHexUnitTeam Team, int32 Q, int32 R);

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Units")
	AHexUnitActor* GetUnitAtCell(int32 Q, int32 R) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Units")
	bool IsCellOccupied(int32 Q, int32 R) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Units")
	bool MoveSelectedUnitToCell(int32 Q, int32 R);

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Movement")
	bool IsCellInCurrentMoveRange(int32 Q, int32 R) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Movement")
	void ShowMoveRangeForUnit(AHexUnitActor* Unit);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Summon")
	void ShowSummonRangeForUnit(AHexUnitActor* Summoner);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Movement")
	void ClearMoveRangeHighlight();

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Combat")
	bool IsCellInCurrentAttackRange(int32 Q, int32 R) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Combat")
	void ShowAttackRangeForUnit(AHexUnitActor* Unit);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Combat")
	void ClearAttackRangeHighlight();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability|Preview")
	void ShowAbilityPreviewRangeForSelectedUnit();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability|Preview")
	void ShowAbilityDescriptionTooltipForSelectedUnit();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability|Preview")
	void ClearAbilityDescriptionTooltip();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability|Preview")
	void ClearAbilityPreviewRangeHighlight();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Combat")
	bool AttackUnit(AHexUnitActor* Attacker, AHexUnitActor* Target);

	// Called by the native travelling projectile when it reaches the target.
	// Public only so AHexAttackProjectile can report completion; do not call this from Blueprint.
	void HandleAttackProjectileImpact(AHexAttackProjectile* Projectile, AHexUnitActor* Attacker, AHexUnitActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Support|Healing")
	bool HealUnit(AHexUnitActor* Healer, AHexUnitActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Summon")
	bool SpawnSummonedUnitFromSummonerAt(AHexUnitActor* Summoner, int32 Q, int32 R);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Summon")
	bool SpawnSummonedUnitsFromSummoner(AHexUnitActor* Summoner);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Summon")
	bool SpawnSummonedUnitsAtCells(AHexUnitActor* Summoner, const TArray<FIntPoint>& TargetCells);

	//                                            .
	//                                                                            Custom-       .
	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability")
	bool PrepareChampionAbility(AHexUnitActor* Champion);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability|Last Stand")
	bool ExecuteLastStandChampionAbility(AHexUnitActor* Champion);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Champion Ability|Marked for Death")
	bool ExecuteMarkedForDeathChampionAbility(AHexUnitActor* Champion, AHexUnitActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Debug")
	void DamageSelectedOrHoveredUnit();

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Pathfinding")
	void GetReachableMoveCells(int32 StartQ, int32 StartR, int32 MaxSteps, TArray<FHexCoord>& OutCells) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Pathfinding")
	bool FindPath(int32 StartQ, int32 StartR, int32 TargetQ, int32 TargetR, TArray<FHexCoord>& OutPath) const;

	UFUNCTION(BlueprintCallable, Category = "Hex Grid|Combat")
	void GetAttackRangeCells(int32 StartQ, int32 StartR, int32 MaxRange, TArray<FHexCoord>& OutCells) const;

	UFUNCTION(BlueprintPure, Category = "Hex Grid|Pathfinding")
	int32 GetHexDistance(int32 AQ, int32 AR, int32 BQ, int32 BR) const;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Hex Grid|Interaction")
	void OnHexCellHovered(int32 Q, int32 R, int32 InstanceIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "Hex Grid|Interaction")
	void OnHexCellClicked(int32 Q, int32 R, int32 InstanceIndex);

	//                      BP_Grid,                                                  /                           Blueprint.
	UFUNCTION(BlueprintImplementableEvent, Category = "Hex Grid|Turns|Start Sequence")
	void OnTurnBannerRequested(const FText& TitleText, const FText& SubtitleText, EHexTurnOwner TurnOwner, bool bIsMatchStart);

	UFUNCTION(BlueprintImplementableEvent, Category = "Hex Grid|Turns|Start Sequence")
	void OnTurnBannerHidden();


	UFUNCTION(BlueprintImplementableEvent, Category = "Hex Grid|Match Result")
	void OnMatchResultDecided(EHexMatchResult MatchResult);

	// Implement this in BP_HexGrid if you later create a real settings window.
	UFUNCTION(BlueprintImplementableEvent, Category = "Hex Grid|Battle Menu")
	void OnBattleSettingsRequested();

private:
	TMap<FIntPoint, int32> CoordToInstance;
	TMap<int32, FIntPoint> InstanceToCoord;

	TMap<FIntPoint, AHexUnitActor*> UnitsByCoord;

	UPROPERTY()
	UUserWidget* ActionPointsWidget = nullptr;

	UPROPERTY()
	UUserWidget* TurnBannerWidget = nullptr;

	UPROPERTY()
	UUserWidget* ResultScreenWidget = nullptr;

	UPROPERTY()
	UAudioComponent* BattleMusicComponent = nullptr;

	bool bMatchRuntimeInitializationInProgress = false;
	bool bMatchFinished = false;
	EHexMatchResult PendingMatchResult = EHexMatchResult::None;

	UPROPERTY()
	UUserWidget* ActionWarningWidget = nullptr;

	FTimerHandle MatchStartBannerTimerHandle;
	FTimerHandle TurnStartBannerTimerHandle;

	FTimerHandle ActionWarningTimerHandle;
	FTimerHandle MatchResultTimerHandle;

	bool bMatchIntroInProgress = false;
	bool bTurnBannerInProgress = false;
	EHexTurnOwner PendingTurnOwner = EHexTurnOwner::Player;

	UPROPERTY()
	AHexUnitActor* BotMovingUnit = nullptr;

	UPROPERTY()
	AHexUnitActor* PlayerAutoEndMovingUnit = nullptr;

	UPROPERTY()
	AHexUnitActor* PendingMoveAttackUnit = nullptr;

	UPROPERTY()
	AHexUnitActor* PendingMoveAttackTarget = nullptr;

	bool bPlayerMoveAttackInProgress = false;

	UPROPERTY()
	AHexAttackProjectile* ActiveAttackProjectile = nullptr;

	UPROPERTY()
	AHexUnitActor* PendingProjectileAttacker = nullptr;

	UPROPERTY()
	AHexUnitActor* PendingProjectileTarget = nullptr;

	bool bAttackProjectileInProgress = false;
	FTimerHandle AttackProjectileLaunchTimerHandle;

	FTimerHandle AutoEndPlayerTurnTimerHandle;
	FTimerHandle PlayerTurnEndBusyRetryTimerHandle;

	bool bPlayerTurnEnding = false;
	int32 LastDisplayedPlayerTurnTimerSecond = INDEX_NONE;

	bool bBattleTimerStarted = false;
	bool bBattleTimerRunning = false;
	int32 LastDisplayedBattleTimerSecond = INDEX_NONE;
	bool bBattleMenuOpen = false;
	bool bSurrenderConfirmOpen = false;

	TArray<AHexUnitActor*> EnemyBotUnits;
	int32 EnemyBotUnitIndex = 0;
	int32 EnemyBotMovesDoneThisTurn = 0;
	int32 EnemyBotBusyRetriesDone = 0;
	TMap<AHexUnitActor*, int32> EnemyBotActionCountThisTurn;

	TWeakObjectPtr<AHexUnitActor> EnemyBotPlannedFocusTarget;
	int32 EnemyBotPlanTurnsRemaining = 0;
	int32 EnemyBotPlannedHorizon = 1;
	FIntPoint EnemyBotPlannedFrontCell = FIntPoint(0, 0);

	bool bPlayerKillActionPointBonusGrantedThisTurn = false;
	bool bEnemyKillActionPointBonusGrantedThisTurn = false;

	TSet<FIntPoint> CurrentMoveRangeSet;
	TMap<AHexUnitActor*, int32> MovementSpentByUnitThisTurn;
	TSet<AHexUnitActor*> UnitsThatAttackedThisTurn;

	TMap<AHexUnitActor*, int32> PlayerArmyIndexByUnit;
	TMap<int32, int32> RawBattleExperienceByPlayerArmyIndex;
	bool bPlayerSurrenderedForExperience = false;
	bool bBattleExperienceCommitted = false;
	bool bBattleCoinsCommitted = false;
	int32 InitialPlayerUnitCountForRewards = 0;
	int32 InitialEnemyUnitCountForRewards = 0;
	int32 LastBattleCoinReward = 0;
	TArray<FHexBattleExperienceResultRow> LastBattleExperienceRows;

	TSet<FIntPoint> CurrentAttackRangeSet;
	TSet<FIntPoint> CurrentAttackBorderSet;
	TSet<FIntPoint> CurrentAbilityPreviewRangeSet;

	FTimerHandle AbilityPreviewTimerHandle;
	FTimerHandle AbilityPreviewUnhoverValidationTimerHandle;
	FTimerHandle AbilityPreviewMousePollTimerHandle;
	bool bAbilityPreviewVisibleFromButton = false;
	int32 AbilityPreviewMouseOutsidePollCount = 0;
	bool bAbilityButtonHovered = false;
	bool bAbilityButtonPressed = false;
	bool bAbilityButtonHoldPreviewTriggered = false;
	bool bSuppressNextAbilityButtonClick = false;

	void AddCell(int32 Q, int32 R);

	void StartPlayerTurnTimer();
	void StopPlayerTurnTimer(bool bResetRemainingTime);
	void UpdatePlayerTurnTimer(float DeltaTime);
	void HandlePlayerTurnTimerExpired();
	void RefreshTurnTimerDisplayIfNeeded(bool bForceUpdate);
	void UpdateTurnTimerWarningVisuals();

	void StartBattleTimerIfNeeded();
	void StopBattleTimer();
	void UpdateBattleTimer(float DeltaTime);
	void RefreshBattleTimerDisplay(bool bForceUpdate);
	FText FormatBattleElapsedTimeText() const;
	void UpdateBattleMenuWidgetState();

	UFUNCTION()
	void HandleBattleMenuButtonClicked();

	UFUNCTION()
	void HandleBattleSettingsButtonClicked();

	UFUNCTION()
	void HandleBattleSurrenderButtonClicked();

	UFUNCTION()
	void HandleSurrenderCancelButtonClicked();

	UFUNCTION()
	void HandleSurrenderConfirmButtonClicked();

	void CreateActionPointsWidget();
	void CreateTurnBannerWidget();
	void ApplyBattleInputMode();
	void InitializeMatch();
	void StartMatchAfterInitialization();
	bool TrySpawnArmyBuilderArmies();
	void CollectEnemyArmyPoolClasses(TArray<TSubclassOf<AHexUnitActor>>& OutEnemyClasses) const;
	FEnemyArmyPowerBand GetEnemyArmyPowerBandForCurrentDifficulty() const;
	bool BuildEnemyArmyForCurrentDifficulty(
		const TArray<TSubclassOf<AHexUnitActor>>& EnemyPoolClasses,
		int32 UnitCount,
		const TArray<TSubclassOf<AHexUnitActor>>& PlayerArmyClasses,
		TArray<TSubclassOf<AHexUnitActor>>& OutEnemyArmyClasses,
		int32& OutEnemyArmyPower
	) const;
	AHexUnitActor* SpawnUnitOfClassAtInternal(TSubclassOf<AHexUnitActor> InUnitClass, int32 Q, int32 R, bool bOverrideTeam, EHexUnitTeam OverrideTeam);

	void CreateResultScreenWidget();
	void EvaluateMatchResultAfterUnitDeath();
	bool HasAlivePlayerUnitsForMatchResult() const;
	bool HasAliveEnemyUnitsForMatchResult() const;
	void FinishMatch(EHexMatchResult MatchResult);
	void ShowMatchResultScreen(EHexMatchResult MatchResult);
	void RegisterPlayerArmyUnitForProgression(AHexUnitActor* Unit, int32 ArmyIndex);
	void ApplySavedProgressToSpawnedUnit(AHexUnitActor* Unit, int32 Level, int32 CurrentExperience);
	void AwardBattleExperience(AHexUnitActor* Unit, int32 RawExperience, const TCHAR* Reason);
	void CommitBattleExperienceToSavedArmy(EHexMatchResult MatchResult);
	EArmyBattleExperienceOutcome GetArmyExperienceOutcome(EHexMatchResult MatchResult) const;
	int32 CountAliveNonSummonedUnitsForTeam(EHexUnitTeam Team) const;
	float GetBattleCoinDifficultyMultiplier() const;
	float GetBattleCoinOutcomeMultiplier(EHexMatchResult MatchResult) const;
	int32 CalculateBattleCoinReward(EHexMatchResult MatchResult) const;
	void CommitBattleCoinReward(EHexMatchResult MatchResult);
	FText BuildBattleCoinRewardText() const;
	FText BuildFallbackBattleExperienceText() const;
	void PopulateResultExperienceList();

	UFUNCTION()
	void HandleResultMainMenuButtonClicked();

	UFUNCTION()
	void HandleResultArmyButtonClicked();

	void CreateActionWarningWidget();
	void ShowNotEnoughActionPointsWarning();
	bool GetCheapestMoveAttackActionPointCost(AHexUnitActor* Attacker, AHexUnitActor* Target, int32& OutRequiredActionPoints, int32& OutPathLength) const;
	void SetTurnBannerWidgetVisibility(bool bVisible);
	void ShowTurnBanner(const FText& TitleText, const FText& SubtitleText, const FLinearColor& TextColor, EHexTurnOwner TurnOwner, bool bIsMatchStart);
	void HideTurnBanner();
	void ShowMatchStartBanner();
	void ShowTurnOwnerBanner(EHexTurnOwner TurnOwner);
	EHexTurnOwner ChooseFirstTurnOwner() const;
	void StartFirstTurn();
	void StartTurnWithBanner(EHexTurnOwner NewTurnOwner);
	void BeginPlayerTurnAfterBanner();
	void BeginEnemyTurnAfterBanner();
	void HandleMatchStartBannerFinished();
	void HandleTurnStartBannerFinished();
	void ClearSelectionAndHighlights();
	void ApplyEnemyBotDifficultyFromLevelOptions();
	bool IsEnemyBotDifficultyAtLeast(EHexBotDifficulty MinimumDifficulty) const;
	float GetEnemyBotDifficultyTargetPriorityScale() const;
	float GetEnemyBotDifficultyKillScoreScale() const;
	float GetEnemyBotDifficultyMistakeChance() const;
	float GetEnemyBotDifficultyFutureThreatScale() const;
	float GetEnemyBotTargetBaseValue(AHexUnitActor* Target) const;
	float GetEnemyBotFactionTargetBonus(AHexUnitActor* EnemyUnit, AHexUnitActor* Target) const;
	float GetEnemyBotFactionMoveBonus(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, int32 NearestPlayerDistance, bool bCanAttackFromCandidate) const;
	bool ShouldEnemyBotMakeDifficultyMistake(float ChanceMultiplier = 1.0f) const;
	int32 GetEnemyBotPlanningDepth() const;
	void RefreshEnemyBotPlan(bool bForce);
	AHexUnitActor* FindEnemyBotPlanFocusTarget() const;
	float ScoreEnemyBotPlanTarget(AHexUnitActor* Target) const;
	float GetEnemyBotUnitBoardValue(AHexUnitActor* Unit) const;
	int32 CountEnemyBotAttackersThreateningPlayerTarget(AHexUnitActor* Target, int32 ExtraReach = 0, AHexUnitActor* IgnoreEnemyUnit = nullptr) const;
	int32 GetPotentialEnemyBotDamageToPlayerTarget(AHexUnitActor* Target, int32 ExtraReach = 0, AHexUnitActor* IgnoreEnemyUnit = nullptr) const;
	int32 GetDesiredEnemyBotAttackersForTarget(AHexUnitActor* Target) const;
	bool IsPlayerUnitIsolatedForEnemyBot(AHexUnitActor* Target) const;
	bool IsEnemyBotAllyDoomed(AHexUnitActor* Ally, int32 ExtraHealing = 0) const;
	float ScoreEnemyBotTargetPressure(AHexUnitActor* EnemyUnit, AHexUnitActor* Target) const;
	float ScoreEnemyBotTrade(AHexUnitActor* EnemyUnit, AHexUnitActor* Target, bool bCanKillTarget) const;
	float ScoreEnemyBotCounterplayAtCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, bool bCanAttackFromCandidate, bool bCanKillFromCandidate) const;
	float ScoreEnemyBotBacklineScreen(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const;
	float ScoreEnemyBotRetreatQuality(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const;
	float ScoreEnemyBotFlankPressure(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const;
	bool IsEnemyBotCellBlockingImportantAlly(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const;
	float ScoreEnemyBotUnitTurnPriority(AHexUnitActor* EnemyUnit) const;
	void SortEnemyBotUnitsByTacticalPriority(TArray<AHexUnitActor*>& Units) const;
	FIntPoint GetArmyCenterCell(EHexUnitTeam Team) const;
	int32 GetAliveUnitCountForTeam(EHexUnitTeam Team) const;
	int32 GetEnemyAllyCountNearCell(const FIntPoint& CellCoord, int32 Range, AHexUnitActor* IgnoreUnit = nullptr) const;
	int32 GetPlayerUnitCountNearCell(const FIntPoint& CellCoord, int32 Range) const;
	AHexUnitActor* FindMostThreatenedEnemyAlly(AHexUnitActor* ActingUnit, int32* OutThreatDamage = nullptr) const;
	bool HasEnemyBotCurrentAttackTarget(AHexUnitActor* EnemyUnit) const;
	float ScoreEnemyBotPreemptiveSupportNeed(AHexUnitActor* Ally, int32* OutThreatCount = nullptr, int32* OutLocalDefenderCount = nullptr) const;
	AHexUnitActor* FindEnemyBotPreemptiveSupportTarget(AHexUnitActor* ActingUnit, float* OutNeedScore = nullptr) const;
	bool IsEnemyBotUnitDirectlyThreatened(AHexUnitActor* EnemyUnit, int32* OutThreatDamage = nullptr) const;
	bool ShouldEnemyBotUseLastStand(AHexUnitActor* Champion) const;
	AHexUnitActor* FindBestEnemyBotMarkedForDeathTarget(AHexUnitActor* Champion) const;
	bool WillEnemyBotTargetTakeDamageThisOrNextTurn(AHexUnitActor* Target, AHexUnitActor* MarkingChampion) const;
	float ScoreEnemyBotSummonCell(AHexUnitActor* Summoner, const FIntPoint& CellCoord) const;
	bool GetUsefulEnemyBotSummonCells(AHexUnitActor* Summoner, TArray<FIntPoint>& OutCells, float* OutBestUtility = nullptr) const;
	int32 GetEnemyBotActionCountThisTurn(AHexUnitActor* EnemyUnit) const;
	int32 GetEnemyBotUnactedUnitCount(AHexUnitActor* IgnoreUnit = nullptr) const;
	int32 GetEnemyBotUnactedFrontlineUnitCount(AHexUnitActor* IgnoreUnit = nullptr) const;
	bool IsEnemyBotArmyInApproachPhase() const;
	bool IsEnemyBotFrontlineUnit(AHexUnitActor* Unit) const;
	int32 GetNearestPlayerDistanceForEnemyFrontline(AHexUnitActor* IgnoreUnit = nullptr) const;
	bool IsEnemyBotBacklineScreenedAtCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord) const;
	bool CanEnemyBotAttackAnyPlayerFromCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, bool* OutCanKill = nullptr) const;
	void RecordEnemyBotUnitAction(AHexUnitActor* EnemyUnit);
	float ScoreEnemyBotGroupTactics(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, bool bCanAttackFromCandidate, bool bCanHealFromCandidate) const;
	float ScoreEnemyBotLookahead(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, int32 Depth) const;
	float ScoreEnemyBotStrategicStateForUnit(AHexUnitActor* EnemyUnit, const FIntPoint& VirtualCoord) const;
	bool IsEnemyBotMovePurposeful(AHexUnitActor* EnemyUnit, const FIntPoint& TargetCoord, const TArray<FHexCoord>& Path, float MoveScore) const;
	bool TryEnemyBotChampionAbility(AHexUnitActor* Champion);
	bool TryEnemyBotSummon(AHexUnitActor* Summoner);
	void ResetEnemyBotActionOrder();
	void AwardKillActionPointBonus(AHexUnitActor* Killer, AHexUnitActor* KilledUnit);
	void UpdateSummonedUnitLifetimeForTurnOwner(EHexTurnOwner TurnOwner);
	void UpdateLastStandDurationForTurnOwner(EHexTurnOwner TurnOwner);
	void UpdateMarkedForDeathDurationForTurnOwner(EHexTurnOwner TurnOwner);
	void UpdateChampionAbilityCooldownForTurnOwner(EHexTurnOwner TurnOwner);
	void ShowChampionAbilityUnavailableWarning(AHexUnitActor* Champion);
	void CollectAliveEnemyUnits(TArray<AHexUnitActor*>& OutUnits) const;
	void CollectAlivePlayerUnits(TArray<AHexUnitActor*>& OutUnits) const;
	bool HasBusyPlayerUnit() const;
	bool HasBusyEnemyBotUnit() const;
	void ScheduleEnemyBotRetryAfterBusyUnit();
	void ScheduleEnemyBotContinueAfterAction();
	void RunEnemyBotTurn();
	bool TrySpendEnemyBotMove(AHexUnitActor* EnemyUnit);
	bool TryEnemyBotAttack(AHexUnitActor* EnemyUnit);
	bool TryEnemyBotHeal(AHexUnitActor* Healer);
	bool TryEnemyBotMove(AHexUnitActor* EnemyUnit);
	bool ExecuteEnemyBotMove(AHexUnitActor* EnemyUnit, const FIntPoint& TargetCoord, const TArray<FHexCoord>& CoordPath);
	AHexUnitActor* FindBestEnemyBotAttackTarget(AHexUnitActor* EnemyUnit) const;
	float ScoreEnemyBotAttackTarget(AHexUnitActor* EnemyUnit, AHexUnitActor* Target) const;
	AHexUnitActor* FindBestEnemyBotHealTarget(AHexUnitActor* Healer) const;
	AHexUnitActor* FindBestEnemyBotHealTargetFromCell(AHexUnitActor* Healer, const FIntPoint& HealerCoord, float* OutScore = nullptr) const;
	float ScoreEnemyBotHealTargetFromCell(AHexUnitActor* Healer, AHexUnitActor* Target, const FIntPoint& HealerCoord) const;
	bool FindBestEnemyBotMove(AHexUnitActor* EnemyUnit, FIntPoint& OutTargetCoord, TArray<FHexCoord>& OutPath, float* OutScore = nullptr) const;
	float ScoreEnemyBotMoveCell(AHexUnitActor* EnemyUnit, const FIntPoint& CandidateCoord, int32 PathLength) const;
	int32 GetNearestPlayerDistanceFromCell(const FIntPoint& CellCoord) const;
	int32 GetNearestAliveEnemyRamDistanceFromCell(const FIntPoint& CellCoord, AHexUnitActor* IgnoreUnit) const;
	int32 GetNearestPlayerDistanceForEnemyRams() const;
	int32 GetRangedTooCloseDistanceForUnit(AHexUnitActor* EnemyUnit) const;
	bool HasWoundedEnemyAllyForSupport(AHexUnitActor* Healer) const;
	bool IsCellThreatenedByPlayerUnits(int32 Q, int32 R, int32& OutThreatDamage) const;
	bool IsCellThreatenedByPlayerUnitsAfterMove(int32 Q, int32 R, int32& OutThreatDamage) const;
	bool IsRangedEnemyTooClose(AHexUnitActor* EnemyUnit) const;
	bool IsMeaningfulEnemyBotRetreat(AHexUnitActor* EnemyUnit, const FIntPoint& TargetCoord) const;
	bool HasAnyAffordablePlayerHealAction() const;
	bool HasAnyAffordablePlayerSummonAction() const;
	bool HasAnyAffordablePlayerChampionAbilityAction() const;
	bool ShouldAutoEndPlayerTurn() const;
	void ScheduleAutoEndPlayerTurnAfterAction(AHexUnitActor* ActingUnit, bool bWaitForMovement);
	bool TryAttackSelectedUnitOrMoveIntoRange(AHexUnitActor* Target);
	bool FindBestMoveAttackPosition(AHexUnitActor* Attacker, AHexUnitActor* Target, FIntPoint& OutMoveCoord, TArray<FHexCoord>& OutPath) const;
	bool ExecuteMoveThenAttack(AHexUnitActor* Attacker, AHexUnitActor* Target, const FIntPoint& MoveCoord, const TArray<FHexCoord>& CoordPath);

	UFUNCTION()
	void HandleEndTurnButtonClicked();

	UFUNCTION()
	void HandleUnitMoveButtonClicked();

	UFUNCTION()
	void HandleUnitAttackButtonClicked();

	UFUNCTION()
	void HandleUnitAbilityButtonClicked();

	UFUNCTION()
	void HandleUnitAbilityButtonPressed();

	UFUNCTION()
	void HandleUnitAbilityButtonReleased();

	UFUNCTION()
	void HandleUnitAbilityButtonHovered();

	UFUNCTION()
	void HandleUnitAbilityButtonUnhovered();

	void StartAbilityPreviewTimer();
	void ValidateAbilityButtonUnhover();
	void HandleActionHotkeys();
	bool CanUseSelectedPlayerUnitActionHotkeys() const;
	FText BuildHotkeyButtonText(const FString& HotkeyLabel, const FText& BaseLabel) const;
	void StartAbilityPreviewMousePollTimer();
	void StopAbilityPreviewMousePollTimer();
	void PollAbilityPreviewMousePosition();
	bool IsAbilityButtonCurrentlyHovered();
	bool IsMouseInsideAbilityPreviewHoverArea() const;
	bool IsMouseInsideWidgetByName(FName WidgetName, float ExtraPadding) const;
	void ShowDelayedAbilityPreviewAndTooltip();
	void StopAbilityPreviewAndTooltip(bool bClearPressState);

	UFUNCTION()
	void HandlePlayerAutoEndMovementFinished();

	UFUNCTION()
	void HandleMoveThenAttackMovementFinished();

	UFUNCTION()
	void HandleDelayedAutoEndPlayerTurn();

	UFUNCTION()
	void ContinueEnemyBotTurn();

	void DrawAttackRangeOutline() const;
	void GetHexCornerWorldLocations(int32 Q, int32 R, TArray<FVector>& OutCorners) const;
	int32 FindEdgeIndexFacingNeighbor(int32 Q, int32 R, const FIntPoint& NeighborCoord) const;

	void UpdateHoverUnderCursor();
	bool TraceHexUnderCursor(FHitResult& OutHit) const;

	void SetHoveredInstance(int32 NewHoveredInstanceIndex);
	void SetSelectedInstance(int32 NewSelectedInstanceIndex);
	void UpdateInstanceVisualState(int32 InstanceIndex);

	void HandleCellClicked(int32 Q, int32 R, int32 InstanceIndex);
	void SelectUnit(AHexUnitActor* Unit);
	void ShowMarkedForDeathRangeForUnit(AHexUnitActor* Champion);

	bool IsDeadUnitOnCell(int32 Q, int32 R) const;
	bool IsDeadUnitInstance(int32 InstanceIndex) const;
	bool IsEnemyUnitInstance(int32 InstanceIndex) const;

	UFUNCTION()
	void HandleUnitDied(AHexUnitActor* DeadUnit);

	UFUNCTION()
	void HandleSummonedUnitDespawn(AHexUnitActor* SummonedUnit);

	bool IsValidSummonTargetCell(AHexUnitActor* Summoner, const FIntPoint& TargetCoord) const;
	void GetSummonCandidateCells(AHexUnitActor* Summoner, TArray<FIntPoint>& OutCells) const;
	void GetSummonTargetCellsForSelectedCell(AHexUnitActor* Summoner, const FIntPoint& SelectedCellCoord, TArray<FIntPoint>& OutCells) const;
	bool SpawnSingleSummonedUnitAt(AHexUnitActor* Summoner, const FIntPoint& TargetCoord, AHexUnitActor*& OutSpawnedUnit);

	bool StartAttackProjectile(AHexUnitActor* Attacker, AHexUnitActor* Target);
	void LaunchPendingAttackProjectile();
	void ApplyAttackDamageAndRewards(AHexUnitActor* Attacker, AHexUnitActor* Target);
	void ClearPendingProjectileAttack(bool bDestroyProjectile);

	TArray<FIntPoint> GetHexNeighbors(const FIntPoint& Coord) const;
};
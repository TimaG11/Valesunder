#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "Widgets/SCompoundWidget.h"

#include "RuleMapGen.generated.h"

class IDetailsView;
class SDockTab;
class STableViewBase;
template<typename ItemType> class SListView;
class ITableRow;

UENUM(BlueprintType)
enum class EMapGenerationMode : uint8
{
    Solo,
    Duo,
    Squad
};

USTRUCT(BlueprintType)
struct FMapGenerationGlobalRules
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "16"))
    int32 WidthCells = 512;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "16"))
    int32 HeightCells = 512;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
    float CellSizeUU = 400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 RandomSeed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "1"))
    int32 InitialRegionSeeds = 32;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "1"))
    int32 MinSeedDistanceCells = 24;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0"))
    int32 SmoothingPasses = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
    bool bStrictBoundaryRules = true;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class RULEMAPGEN_API UBiomeResourceRule : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
    FName ResourceId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Visual")
    TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Visual")
    TArray<TSoftObjectPtr<UMaterialInterface>> Materials;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Balance", meta = (ClampMin = "0"))
    int32 ClusterCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Balance", meta = (ClampMin = "0.0"))
    float RespawnSeconds = 1800.0f;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class RULEMAPGEN_API UBiomeBoundaryRule : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boundary")
    FName OtherBiomeId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boundary", meta = (ClampMin = "1"))
    int32 BoundaryWidthCells = 4;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boundary|Visual")
    TArray<TSoftObjectPtr<UTexture2D>> GroundTextures;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boundary|Visual")
    TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boundary|Audio")
    TArray<TSoftObjectPtr<USoundBase>> Music;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class RULEMAPGEN_API UBiomeRule : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
    FName BiomeId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float TargetPercent = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Visual")
    TArray<TSoftObjectPtr<UTexture2D>> GroundTextures;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Visual")
    TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Audio")
    TArray<TSoftObjectPtr<USoundBase>> Music;

    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Biome|Resources")
    TArray<TObjectPtr<UBiomeResourceRule>> Resources;

    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Biome|Boundaries")
    TArray<TObjectPtr<UBiomeBoundaryRule>> BoundaryRules;
};

UCLASS(BlueprintType)
class RULEMAPGEN_API UMapGenerationRulesAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
    EMapGenerationMode Mode = EMapGenerationMode::Solo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
    FMapGenerationGlobalRules GlobalRules;

    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Biomes")
    TArray<TObjectPtr<UBiomeRule>> Biomes;

public:
    UFUNCTION(BlueprintCallable, Category = "Map Generation")
    bool ValidateRules(FString& OutError) const;

    UFUNCTION(BlueprintCallable, Category = "Map Generation")
    int32 FindBiomeIndexById(FName BiomeId) const;

    UFUNCTION(BlueprintCallable, Category = "Map Generation")
    bool HasBoundaryRule(FName A, FName B) const;

    UFUNCTION(BlueprintCallable, Category = "Map Generation")
    bool AreBiomesAllowedToTouch(FName A, FName B) const;
};

USTRUCT(BlueprintType)
struct FGeneratedBiomeCell
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 BiomeIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bIsBoundary = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 BoundaryOtherBiomeIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FGeneratedBiomeMap
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 WidthCells = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 HeightCells = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float CellSizeUU = 400.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FGeneratedBiomeCell> Cells;

    bool IsValidCoord(int32 X, int32 Y) const
    {
        return X >= 0 && Y >= 0 && X < WidthCells && Y < HeightCells;
    }

    int32 ToIndex(int32 X, int32 Y) const
    {
        return Y * WidthCells + X;
    }
};

UCLASS()
class RULEMAPGEN_API UBiomeMapGenerator : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Map Generation")
    static bool GenerateBiomeMap(
        const UMapGenerationRulesAsset* Rules,
        FGeneratedBiomeMap& OutMap,
        FString& OutError
    );

private:
    static void GetFourNeighbors(
        int32 CellIndex,
        int32 Width,
        int32 Height,
        TArray<int32>& OutNeighbors
    );

    static bool CanPlaceBiomeAtCell(
        const UMapGenerationRulesAsset* Rules,
        const TArray<int32>& LocalToAssetBiomeIndex,
        const TArray<int32>& CellLocalBiomes,
        int32 CellIndex,
        int32 CandidateLocalBiome,
        int32 Width,
        int32 Height
    );
};

using FBiomeListItem = TWeakObjectPtr<UBiomeRule>;

class SMapRulesEditorWindow final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMapRulesEditorWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TStrongObjectPtr<UMapGenerationRulesAsset> RulesAsset;

    TArray<FBiomeListItem> BiomeItems;

    TSharedPtr<SListView<FBiomeListItem>> BiomeListView;
    TSharedPtr<IDetailsView> DetailsView;

private:
    void CreateTransientRulesAsset();
    void RefreshBiomeItems();

    FReply OnAddBiomeClicked();
    FReply OnRemoveBiomeClicked();
    FReply OnShowGlobalRulesClicked();
    FReply OnValidateClicked();

    TSharedRef<ITableRow> OnGenerateBiomeRow(
        FBiomeListItem Item,
        const TSharedRef<STableViewBase>& OwnerTable
    );

    void OnBiomeSelectionChanged(
        FBiomeListItem Item,
        ESelectInfo::Type SelectInfo
    );

    UBiomeRule* GetSelectedBiome() const;
    FName MakeUniqueBiomeId() const;
};

class FRuleMapGenModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void OpenMapRulesWindow();
    TSharedRef<SDockTab> SpawnMapRulesTab(const class FSpawnTabArgs& SpawnTabArgs);
};
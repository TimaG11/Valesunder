#include "RuleMapGen.h"

#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

#include "PropertyEditorModule.h"
#include "IDetailsView.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "RuleMapGen"

static const FName RuleMapGenTabName(TEXT("RuleMapGen_MapRulesEditor"));

int32 UMapGenerationRulesAsset::FindBiomeIndexById(const FName BiomeId) const
{
    if (BiomeId.IsNone())
    {
        return INDEX_NONE;
    }

    for (int32 Index = 0; Index < Biomes.Num(); ++Index)
    {
        const UBiomeRule* Biome = Biomes[Index];

        if (IsValid(Biome) && Biome->BiomeId == BiomeId)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

bool UMapGenerationRulesAsset::HasBoundaryRule(const FName A, const FName B) const
{
    if (A.IsNone() || B.IsNone() || A == B)
    {
        return false;
    }

    const int32 AIndex = FindBiomeIndexById(A);
    const int32 BIndex = FindBiomeIndexById(B);

    if (!Biomes.IsValidIndex(AIndex) || !Biomes.IsValidIndex(BIndex))
    {
        return false;
    }

    const UBiomeRule* ABiome = Biomes[AIndex];
    const UBiomeRule* BBiome = Biomes[BIndex];

    if (IsValid(ABiome))
    {
        for (const UBiomeBoundaryRule* Rule : ABiome->BoundaryRules)
        {
            if (IsValid(Rule) && Rule->OtherBiomeId == B)
            {
                return true;
            }
        }
    }

    if (IsValid(BBiome))
    {
        for (const UBiomeBoundaryRule* Rule : BBiome->BoundaryRules)
        {
            if (IsValid(Rule) && Rule->OtherBiomeId == A)
            {
                return true;
            }
        }
    }

    return false;
}

bool UMapGenerationRulesAsset::AreBiomesAllowedToTouch(const FName A, const FName B) const
{
    if (A == B)
    {
        return true;
    }

    return HasBoundaryRule(A, B);
}

bool UMapGenerationRulesAsset::ValidateRules(FString& OutError) const
{
    OutError.Reset();

    if (GlobalRules.WidthCells <= 0 || GlobalRules.HeightCells <= 0)
    {
        OutError = TEXT("Map size must be positive.");
        return false;
    }

    if (GlobalRules.CellSizeUU <= 0.0f)
    {
        OutError = TEXT("CellSizeUU must be positive.");
        return false;
    }

    TSet<FName> UsedIds;
    TArray<FName> EnabledBiomeIds;

    float TotalPercent = 0.0f;

    for (const UBiomeRule* Biome : Biomes)
    {
        if (!IsValid(Biome))
        {
            OutError = TEXT("Biome list contains invalid biome object.");
            return false;
        }

        if (Biome->BiomeId.IsNone())
        {
            OutError = TEXT("Biome has empty BiomeId.");
            return false;
        }

        if (UsedIds.Contains(Biome->BiomeId))
        {
            OutError = FString::Printf(TEXT("Duplicate BiomeId: %s"), *Biome->BiomeId.ToString());
            return false;
        }

        UsedIds.Add(Biome->BiomeId);

        if (Biome->TargetPercent > 0.0f)
        {
            EnabledBiomeIds.Add(Biome->BiomeId);
            TotalPercent += Biome->TargetPercent;
        }
    }

    if (EnabledBiomeIds.Num() == 0)
    {
        OutError = TEXT("No enabled biomes.");
        return false;
    }

    if (TotalPercent <= KINDA_SMALL_NUMBER)
    {
        OutError = TEXT("Total biome percent is zero.");
        return false;
    }

    for (const UBiomeRule* Biome : Biomes)
    {
        if (!IsValid(Biome))
        {
            continue;
        }

        TSet<FName> LocalBoundaryTargets;

        for (const UBiomeBoundaryRule* BoundaryRule : Biome->BoundaryRules)
        {
            if (!IsValid(BoundaryRule))
            {
                OutError = FString::Printf(
                    TEXT("Biome %s has invalid boundary rule."),
                    *Biome->BiomeId.ToString()
                );
                return false;
            }

            if (BoundaryRule->OtherBiomeId.IsNone())
            {
                OutError = FString::Printf(
                    TEXT("Biome %s has empty boundary target."),
                    *Biome->BiomeId.ToString()
                );
                return false;
            }

            if (BoundaryRule->OtherBiomeId == Biome->BiomeId)
            {
                OutError = FString::Printf(
                    TEXT("Biome %s has boundary rule with itself."),
                    *Biome->BiomeId.ToString()
                );
                return false;
            }

            if (!UsedIds.Contains(BoundaryRule->OtherBiomeId))
            {
                OutError = FString::Printf(
                    TEXT("Biome %s has boundary rule to unknown biome %s."),
                    *Biome->BiomeId.ToString(),
                    *BoundaryRule->OtherBiomeId.ToString()
                );
                return false;
            }

            if (LocalBoundaryTargets.Contains(BoundaryRule->OtherBiomeId))
            {
                OutError = FString::Printf(
                    TEXT("Biome %s has duplicated boundary rule to %s."),
                    *Biome->BiomeId.ToString(),
                    *BoundaryRule->OtherBiomeId.ToString()
                );
                return false;
            }

            LocalBoundaryTargets.Add(BoundaryRule->OtherBiomeId);
        }
    }

    if (EnabledBiomeIds.Num() > 1)
    {
        TSet<FName> Visited;
        TArray<FName> Queue;

        Queue.Add(EnabledBiomeIds[0]);
        Visited.Add(EnabledBiomeIds[0]);

        while (Queue.Num() > 0)
        {
            const FName Current = Queue[0];
            Queue.RemoveAt(0);

            for (const FName Candidate : EnabledBiomeIds)
            {
                if (!Visited.Contains(Candidate) && AreBiomesAllowedToTouch(Current, Candidate))
                {
                    Visited.Add(Candidate);
                    Queue.Add(Candidate);
                }
            }
        }

        if (Visited.Num() != EnabledBiomeIds.Num())
        {
            OutError = TEXT("Enabled biome boundary graph is not connected.");
            return false;
        }
    }

    return true;
}

void UBiomeMapGenerator::GetFourNeighbors(
    const int32 CellIndex,
    const int32 Width,
    const int32 Height,
    TArray<int32>& OutNeighbors
)
{
    OutNeighbors.Reset();

    const int32 X = CellIndex % Width;
    const int32 Y = CellIndex / Width;

    if (X > 0)
    {
        OutNeighbors.Add(CellIndex - 1);
    }

    if (X < Width - 1)
    {
        OutNeighbors.Add(CellIndex + 1);
    }

    if (Y > 0)
    {
        OutNeighbors.Add(CellIndex - Width);
    }

    if (Y < Height - 1)
    {
        OutNeighbors.Add(CellIndex + Width);
    }
}

bool UBiomeMapGenerator::CanPlaceBiomeAtCell(
    const UMapGenerationRulesAsset* Rules,
    const TArray<int32>& LocalToAssetBiomeIndex,
    const TArray<int32>& CellLocalBiomes,
    const int32 CellIndex,
    const int32 CandidateLocalBiome,
    const int32 Width,
    const int32 Height
)
{
    if (!IsValid(Rules) || !LocalToAssetBiomeIndex.IsValidIndex(CandidateLocalBiome))
    {
        return false;
    }

    const int32 CandidateAssetIndex = LocalToAssetBiomeIndex[CandidateLocalBiome];

    if (!Rules->Biomes.IsValidIndex(CandidateAssetIndex))
    {
        return false;
    }

    const UBiomeRule* CandidateBiome = Rules->Biomes[CandidateAssetIndex];

    if (!IsValid(CandidateBiome))
    {
        return false;
    }

    TArray<int32> Neighbors;
    GetFourNeighbors(CellIndex, Width, Height, Neighbors);

    for (const int32 NeighborIndex : Neighbors)
    {
        const int32 NeighborLocalBiome = CellLocalBiomes[NeighborIndex];

        if (NeighborLocalBiome == INDEX_NONE || NeighborLocalBiome == CandidateLocalBiome)
        {
            continue;
        }

        if (!LocalToAssetBiomeIndex.IsValidIndex(NeighborLocalBiome))
        {
            return false;
        }

        const int32 NeighborAssetIndex = LocalToAssetBiomeIndex[NeighborLocalBiome];

        if (!Rules->Biomes.IsValidIndex(NeighborAssetIndex))
        {
            return false;
        }

        const UBiomeRule* NeighborBiome = Rules->Biomes[NeighborAssetIndex];

        if (!IsValid(NeighborBiome))
        {
            return false;
        }

        if (!Rules->AreBiomesAllowedToTouch(CandidateBiome->BiomeId, NeighborBiome->BiomeId))
        {
            return false;
        }
    }

    return true;
}

bool UBiomeMapGenerator::GenerateBiomeMap(
    const UMapGenerationRulesAsset* Rules,
    FGeneratedBiomeMap& OutMap,
    FString& OutError
)
{
    OutMap = FGeneratedBiomeMap();
    OutError.Reset();

    if (!IsValid(Rules))
    {
        OutError = TEXT("Rules asset is invalid.");
        return false;
    }

    if (!Rules->ValidateRules(OutError))
    {
        return false;
    }

    const int32 Width = Rules->GlobalRules.WidthCells;
    const int32 Height = Rules->GlobalRules.HeightCells;
    const int32 TotalCells = Width * Height;

    if (TotalCells <= 0)
    {
        OutError = TEXT("Invalid total cell count.");
        return false;
    }

    TArray<int32> LocalToAssetBiomeIndex;
    TArray<float> LocalPercents;

    float PercentSum = 0.0f;

    for (int32 AssetIndex = 0; AssetIndex < Rules->Biomes.Num(); ++AssetIndex)
    {
        const UBiomeRule* Biome = Rules->Biomes[AssetIndex];

        if (!IsValid(Biome) || Biome->TargetPercent <= 0.0f)
        {
            continue;
        }

        LocalToAssetBiomeIndex.Add(AssetIndex);
        LocalPercents.Add(Biome->TargetPercent);
        PercentSum += Biome->TargetPercent;
    }

    if (LocalToAssetBiomeIndex.Num() == 0 || PercentSum <= KINDA_SMALL_NUMBER)
    {
        OutError = TEXT("No enabled biomes.");
        return false;
    }

    TArray<int32> TargetCounts;
    TArray<int32> CurrentCounts;

    TargetCounts.SetNumZeroed(LocalToAssetBiomeIndex.Num());
    CurrentCounts.SetNumZeroed(LocalToAssetBiomeIndex.Num());

    int32 AssignedTargetTotal = 0;

    for (int32 LocalBiome = 0; LocalBiome < LocalToAssetBiomeIndex.Num(); ++LocalBiome)
    {
        TargetCounts[LocalBiome] = FMath::Max(
            1,
            FMath::RoundToInt(static_cast<float>(TotalCells) * LocalPercents[LocalBiome] / PercentSum)
        );

        AssignedTargetTotal += TargetCounts[LocalBiome];
    }

    while (AssignedTargetTotal > TotalCells)
    {
        int32 BestIndex = 0;

        for (int32 Index = 1; Index < TargetCounts.Num(); ++Index)
        {
            if (TargetCounts[Index] > TargetCounts[BestIndex])
            {
                BestIndex = Index;
            }
        }

        --TargetCounts[BestIndex];
        --AssignedTargetTotal;
    }

    while (AssignedTargetTotal < TotalCells)
    {
        int32 BestIndex = 0;

        for (int32 Index = 1; Index < TargetCounts.Num(); ++Index)
        {
            if (TargetCounts[Index] < TargetCounts[BestIndex])
            {
                BestIndex = Index;
            }
        }

        ++TargetCounts[BestIndex];
        ++AssignedTargetTotal;
    }

    FRandomStream Random(Rules->GlobalRules.RandomSeed);

    TArray<int32> CellLocalBiomes;
    CellLocalBiomes.Init(INDEX_NONE, TotalCells);

    TArray<int32> Frontier;
    TArray<FIntPoint> SeedPositions;

    const int32 DesiredSeeds = FMath::Max(
        Rules->GlobalRules.InitialRegionSeeds,
        LocalToAssetBiomeIndex.Num()
    );

    for (int32 LocalBiome = 0; LocalBiome < LocalToAssetBiomeIndex.Num(); ++LocalBiome)
    {
        const float BiomePart = static_cast<float>(TargetCounts[LocalBiome]) / static_cast<float>(TotalCells);
        const int32 SeedsForBiome = FMath::Max(1, FMath::RoundToInt(BiomePart * DesiredSeeds));

        for (int32 SeedIndex = 0; SeedIndex < SeedsForBiome; ++SeedIndex)
        {
            bool bPlaced = false;

            for (int32 Attempt = 0; Attempt < 5000; ++Attempt)
            {
                const int32 X = Random.RandRange(0, Width - 1);
                const int32 Y = Random.RandRange(0, Height - 1);
                const int32 CellIndex = Y * Width + X;

                if (CellLocalBiomes[CellIndex] != INDEX_NONE)
                {
                    continue;
                }

                bool bFarEnough = true;

                const int32 MinDistance = Rules->GlobalRules.MinSeedDistanceCells;
                const int32 MinDistanceSq = MinDistance * MinDistance;

                for (const FIntPoint& ExistingSeed : SeedPositions)
                {
                    const int32 Dx = ExistingSeed.X - X;
                    const int32 Dy = ExistingSeed.Y - Y;

                    if ((Dx * Dx + Dy * Dy) < MinDistanceSq)
                    {
                        bFarEnough = false;
                        break;
                    }
                }

                if (!bFarEnough)
                {
                    continue;
                }

                CellLocalBiomes[CellIndex] = LocalBiome;
                ++CurrentCounts[LocalBiome];

                Frontier.Add(CellIndex);
                SeedPositions.Add(FIntPoint(X, Y));

                bPlaced = true;
                break;
            }

            if (!bPlaced)
            {
                for (int32 Attempt = 0; Attempt < 5000; ++Attempt)
                {
                    const int32 CellIndex = Random.RandRange(0, TotalCells - 1);

                    if (CellLocalBiomes[CellIndex] == INDEX_NONE)
                    {
                        CellLocalBiomes[CellIndex] = LocalBiome;
                        ++CurrentCounts[LocalBiome];

                        Frontier.Add(CellIndex);
                        SeedPositions.Add(FIntPoint(CellIndex % Width, CellIndex / Width));

                        bPlaced = true;
                        break;
                    }
                }
            }

            if (!bPlaced)
            {
                OutError = TEXT("Failed to place biome seed.");
                return false;
            }
        }
    }

    int32 FilledCells = 0;

    for (const int32 LocalBiome : CellLocalBiomes)
    {
        if (LocalBiome != INDEX_NONE)
        {
            ++FilledCells;
        }
    }

    const int32 SafetyLimit = TotalCells * 64;
    int32 SafetyCounter = 0;

    while (FilledCells < TotalCells && Frontier.Num() > 0 && SafetyCounter < SafetyLimit)
    {
        ++SafetyCounter;

        const int32 FrontierSlot = Random.RandRange(0, Frontier.Num() - 1);
        const int32 SourceCellIndex = Frontier[FrontierSlot];

        Frontier.RemoveAtSwap(FrontierSlot, 1, EAllowShrinking::No);

        const int32 SourceLocalBiome = CellLocalBiomes[SourceCellIndex];

        if (SourceLocalBiome == INDEX_NONE)
        {
            continue;
        }

        TArray<int32> Neighbors;
        GetFourNeighbors(SourceCellIndex, Width, Height, Neighbors);

        for (int32 ShuffleIndex = Neighbors.Num() - 1; ShuffleIndex > 0; --ShuffleIndex)
        {
            const int32 SwapIndex = Random.RandRange(0, ShuffleIndex);
            Neighbors.Swap(ShuffleIndex, SwapIndex);
        }

        for (const int32 NeighborIndex : Neighbors)
        {
            if (CellLocalBiomes[NeighborIndex] != INDEX_NONE)
            {
                continue;
            }

            int32 ChosenLocalBiome = INDEX_NONE;

            if (
                CurrentCounts.IsValidIndex(SourceLocalBiome) &&
                TargetCounts.IsValidIndex(SourceLocalBiome) &&
                CurrentCounts[SourceLocalBiome] < TargetCounts[SourceLocalBiome] &&
                CanPlaceBiomeAtCell(
                    Rules,
                    LocalToAssetBiomeIndex,
                    CellLocalBiomes,
                    NeighborIndex,
                    SourceLocalBiome,
                    Width,
                    Height
                )
            )
            {
                ChosenLocalBiome = SourceLocalBiome;
            }
            else
            {
                TArray<int32> Candidates;

                for (int32 LocalBiome = 0; LocalBiome < LocalToAssetBiomeIndex.Num(); ++LocalBiome)
                {
                    if (
                        CurrentCounts[LocalBiome] < TargetCounts[LocalBiome] &&
                        CanPlaceBiomeAtCell(
                            Rules,
                            LocalToAssetBiomeIndex,
                            CellLocalBiomes,
                            NeighborIndex,
                            LocalBiome,
                            Width,
                            Height
                        )
                    )
                    {
                        Candidates.Add(LocalBiome);
                    }
                }

                if (Candidates.Num() > 0)
                {
                    ChosenLocalBiome = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
                }
            }

            if (ChosenLocalBiome == INDEX_NONE)
            {
                continue;
            }

            CellLocalBiomes[NeighborIndex] = ChosenLocalBiome;
            ++CurrentCounts[ChosenLocalBiome];
            ++FilledCells;

            Frontier.Add(NeighborIndex);
        }
    }

    for (int32 Pass = 0; Pass < 16 && FilledCells < TotalCells; ++Pass)
    {
        bool bMadeProgress = false;

        for (int32 CellIndex = 0; CellIndex < TotalCells; ++CellIndex)
        {
            if (CellLocalBiomes[CellIndex] != INDEX_NONE)
            {
                continue;
            }

            for (int32 LocalBiome = 0; LocalBiome < LocalToAssetBiomeIndex.Num(); ++LocalBiome)
            {
                if (
                    CanPlaceBiomeAtCell(
                        Rules,
                        LocalToAssetBiomeIndex,
                        CellLocalBiomes,
                        CellIndex,
                        LocalBiome,
                        Width,
                        Height
                    )
                )
                {
                    CellLocalBiomes[CellIndex] = LocalBiome;
                    ++CurrentCounts[LocalBiome];
                    ++FilledCells;

                    bMadeProgress = true;
                    break;
                }
            }
        }

        if (!bMadeProgress)
        {
            break;
        }
    }

    if (FilledCells != TotalCells)
    {
        OutError = TEXT("Failed to fill all cells. Boundary rules are too restrictive.");
        return false;
    }

    for (int32 Pass = 0; Pass < Rules->GlobalRules.SmoothingPasses; ++Pass)
    {
        TArray<int32> NewCellLocalBiomes = CellLocalBiomes;

        for (int32 CellIndex = 0; CellIndex < TotalCells; ++CellIndex)
        {
            TArray<int32> Neighbors;
            GetFourNeighbors(CellIndex, Width, Height, Neighbors);

            TMap<int32, int32> Counts;

            for (const int32 NeighborIndex : Neighbors)
            {
                const int32 NeighborBiome = CellLocalBiomes[NeighborIndex];

                if (NeighborBiome != INDEX_NONE)
                {
                    Counts.FindOrAdd(NeighborBiome)++;
                }
            }

            int32 BestBiome = CellLocalBiomes[CellIndex];
            int32 BestCount = 0;

            for (const TPair<int32, int32>& Pair : Counts)
            {
                if (Pair.Value > BestCount)
                {
                    BestBiome = Pair.Key;
                    BestCount = Pair.Value;
                }
            }

            if (
                BestBiome != CellLocalBiomes[CellIndex] &&
                BestCount >= 3 &&
                CanPlaceBiomeAtCell(
                    Rules,
                    LocalToAssetBiomeIndex,
                    CellLocalBiomes,
                    CellIndex,
                    BestBiome,
                    Width,
                    Height
                )
            )
            {
                NewCellLocalBiomes[CellIndex] = BestBiome;
            }
        }

        CellLocalBiomes = MoveTemp(NewCellLocalBiomes);
    }

    OutMap.WidthCells = Width;
    OutMap.HeightCells = Height;
    OutMap.CellSizeUU = Rules->GlobalRules.CellSizeUU;
    OutMap.Cells.SetNum(TotalCells);

    for (int32 CellIndex = 0; CellIndex < TotalCells; ++CellIndex)
    {
        const int32 LocalBiome = CellLocalBiomes[CellIndex];

        const int32 AssetBiomeIndex = LocalToAssetBiomeIndex.IsValidIndex(LocalBiome)
            ? LocalToAssetBiomeIndex[LocalBiome]
            : INDEX_NONE;

        OutMap.Cells[CellIndex].BiomeIndex = AssetBiomeIndex;
    }

    for (int32 CellIndex = 0; CellIndex < TotalCells; ++CellIndex)
    {
        FGeneratedBiomeCell& Cell = OutMap.Cells[CellIndex];

        TArray<int32> Neighbors;
        GetFourNeighbors(CellIndex, Width, Height, Neighbors);

        for (const int32 NeighborIndex : Neighbors)
        {
            const int32 NeighborBiomeIndex = OutMap.Cells[NeighborIndex].BiomeIndex;

            if (NeighborBiomeIndex != INDEX_NONE && NeighborBiomeIndex != Cell.BiomeIndex)
            {
                Cell.bIsBoundary = true;
                Cell.BoundaryOtherBiomeIndex = NeighborBiomeIndex;
                break;
            }
        }
    }

    return true;
}

void SMapRulesEditorWindow::Construct(const FArguments& InArgs)
{
    CreateTransientRulesAsset();

    FPropertyEditorModule& PropertyEditorModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bAllowSearch = true;
    DetailsArgs.bHideSelectionTip = true;
    DetailsArgs.bLockable = false;
    DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsArgs.bUpdatesFromSelection = false;

    DetailsView = PropertyEditorModule.CreateDetailView(DetailsArgs);
    DetailsView->SetObject(RulesAsset.Get());

    RefreshBiomeItems();

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(6.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(2.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("AddBiome", "+ Add Biome"))
                .OnClicked(this, &SMapRulesEditorWindow::OnAddBiomeClicked)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(2.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("RemoveBiome", "Remove"))
                .OnClicked(this, &SMapRulesEditorWindow::OnRemoveBiomeClicked)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(2.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("GlobalRules", "Global Rules"))
                .OnClicked(this, &SMapRulesEditorWindow::OnShowGlobalRulesClicked)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(2.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("ValidateRules", "Validate"))
                .OnClicked(this, &SMapRulesEditorWindow::OnValidateClicked)
            ]
        ]

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            SNew(SSplitter)

            + SSplitter::Slot()
            .Value(0.28f)
            [
                SNew(SBorder)
                .Padding(4.0f)
                [
                    SAssignNew(BiomeListView, SListView<FBiomeListItem>)
                    .ListItemsSource(&BiomeItems)
                    .OnGenerateRow_Raw(this, &SMapRulesEditorWindow::OnGenerateBiomeRow)
                    .OnSelectionChanged_Raw(this, &SMapRulesEditorWindow::OnBiomeSelectionChanged)
                    .SelectionMode(ESelectionMode::Single)
                ]
            ]

            + SSplitter::Slot()
            .Value(0.72f)
            [
                SNew(SBorder)
                .Padding(4.0f)
                [
                    DetailsView.ToSharedRef()
                ]
            ]
        ]
    ];
}

void SMapRulesEditorWindow::CreateTransientRulesAsset()
{
    UMapGenerationRulesAsset* NewAsset = NewObject<UMapGenerationRulesAsset>(
        GetTransientPackage(),
        UMapGenerationRulesAsset::StaticClass(),
        NAME_None,
        RF_Transactional
    );

    check(NewAsset);

    NewAsset->GlobalRules.WidthCells = 512;
    NewAsset->GlobalRules.HeightCells = 512;
    NewAsset->GlobalRules.CellSizeUU = 400.0f;

    RulesAsset.Reset(NewAsset);
}

void SMapRulesEditorWindow::RefreshBiomeItems()
{
    BiomeItems.Reset();

    UMapGenerationRulesAsset* Asset = RulesAsset.Get();

    if (!IsValid(Asset))
    {
        return;
    }

    for (UBiomeRule* Biome : Asset->Biomes)
    {
        if (IsValid(Biome))
        {
            BiomeItems.Add(Biome);
        }
    }

    if (BiomeListView.IsValid())
    {
        BiomeListView->RequestListRefresh();
    }
}

FName SMapRulesEditorWindow::MakeUniqueBiomeId() const
{
    const UMapGenerationRulesAsset* Asset = RulesAsset.Get();

    for (int32 Index = 1; Index < 10000; ++Index)
    {
        const FName Candidate(*FString::Printf(TEXT("Biome_%02d"), Index));

        bool bExists = false;

        if (IsValid(Asset))
        {
            for (const UBiomeRule* Biome : Asset->Biomes)
            {
                if (IsValid(Biome) && Biome->BiomeId == Candidate)
                {
                    bExists = true;
                    break;
                }
            }
        }

        if (!bExists)
        {
            return Candidate;
        }
    }

    return FName(TEXT("Biome"));
}

FReply SMapRulesEditorWindow::OnAddBiomeClicked()
{
    UMapGenerationRulesAsset* Asset = RulesAsset.Get();

    if (!IsValid(Asset))
    {
        return FReply::Handled();
    }

    Asset->Modify();

    UBiomeRule* NewBiome = NewObject<UBiomeRule>(
        Asset,
        UBiomeRule::StaticClass(),
        NAME_None,
        RF_Transactional
    );

    if (!IsValid(NewBiome))
    {
        return FReply::Handled();
    }

    NewBiome->Modify();

    const FName NewBiomeId = MakeUniqueBiomeId();

    NewBiome->BiomeId = NewBiomeId;
    NewBiome->DisplayName = FText::FromName(NewBiomeId);
    NewBiome->TargetPercent = 20.0f;

    Asset->Biomes.Add(NewBiome);

    RefreshBiomeItems();

    if (BiomeListView.IsValid())
    {
        BiomeListView->SetSelection(NewBiome);
    }

    if (DetailsView.IsValid())
    {
        DetailsView->SetObject(NewBiome);
    }

    return FReply::Handled();
}

FReply SMapRulesEditorWindow::OnRemoveBiomeClicked()
{
    UMapGenerationRulesAsset* Asset = RulesAsset.Get();
    UBiomeRule* SelectedBiome = GetSelectedBiome();

    if (!IsValid(Asset) || !IsValid(SelectedBiome))
    {
        return FReply::Handled();
    }

    Asset->Modify();

    const FName RemovedBiomeId = SelectedBiome->BiomeId;

    Asset->Biomes.Remove(SelectedBiome);

    for (UBiomeRule* Biome : Asset->Biomes)
    {
        if (!IsValid(Biome))
        {
            continue;
        }

        Biome->Modify();

        Biome->BoundaryRules.RemoveAll(
            [RemovedBiomeId](const UBiomeBoundaryRule* Rule)
            {
                return !IsValid(Rule) || Rule->OtherBiomeId == RemovedBiomeId;
            }
        );
    }

    RefreshBiomeItems();

    if (DetailsView.IsValid())
    {
        DetailsView->SetObject(Asset);
    }

    return FReply::Handled();
}

FReply SMapRulesEditorWindow::OnShowGlobalRulesClicked()
{
    if (DetailsView.IsValid())
    {
        DetailsView->SetObject(RulesAsset.Get());
    }

    if (BiomeListView.IsValid())
    {
        BiomeListView->ClearSelection();
    }

    return FReply::Handled();
}

FReply SMapRulesEditorWindow::OnValidateClicked()
{
    FString Error;

    const bool bValid = IsValid(RulesAsset.Get()) && RulesAsset->ValidateRules(Error);

    FNotificationInfo Info(
        bValid
            ? LOCTEXT("ValidationOk", "Map rules are valid.")
            : FText::FromString(FString::Printf(TEXT("Map rules are invalid: %s"), *Error))
    );

    Info.ExpireDuration = 4.0f;
    Info.bFireAndForget = true;

    FSlateNotificationManager::Get().AddNotification(Info);

    return FReply::Handled();
}

TSharedRef<ITableRow> SMapRulesEditorWindow::OnGenerateBiomeRow(
    FBiomeListItem Item,
    const TSharedRef<STableViewBase>& OwnerTable
)
{
    FString Label = TEXT("Invalid Biome");

    if (Item.IsValid())
    {
        const UBiomeRule* Biome = Item.Get();

        Label = !Biome->DisplayName.IsEmpty()
            ? Biome->DisplayName.ToString()
            : Biome->BiomeId.ToString();
    }

    return SNew(STableRow<FBiomeListItem>, OwnerTable)
    [
        SNew(STextBlock)
        .Text(FText::FromString(Label))
    ];
}

void SMapRulesEditorWindow::OnBiomeSelectionChanged(
    FBiomeListItem Item,
    ESelectInfo::Type SelectInfo
)
{
    if (DetailsView.IsValid())
    {
        DetailsView->SetObject(Item.Get());
    }
}

UBiomeRule* SMapRulesEditorWindow::GetSelectedBiome() const
{
    if (!BiomeListView.IsValid())
    {
        return nullptr;
    }

    const TArray<FBiomeListItem> SelectedItems = BiomeListView->GetSelectedItems();

    if (SelectedItems.Num() == 0)
    {
        return nullptr;
    }

    return SelectedItems[0].Get();
}

void FRuleMapGenModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        RuleMapGenTabName,
        FOnSpawnTab::CreateRaw(this, &FRuleMapGenModule::SpawnMapRulesTab)
    )
    .SetDisplayName(LOCTEXT("MapRulesTabTitle", "Map Rules Generator"))
    .SetMenuType(ETabSpawnerMenuType::Hidden);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FRuleMapGenModule::RegisterMenus)
    );
}

void FRuleMapGenModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(RuleMapGenTabName);
}

void FRuleMapGenModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");

    if (!ToolsMenu)
    {
        return;
    }

    FToolMenuSection& Section = ToolsMenu->FindOrAddSection("RuleMapGen");

    Section.AddMenuEntry(
        "OpenRuleMapGenEditor",
        LOCTEXT("OpenRuleMapGenEditor_Label", "Map Rules Generator"),
        LOCTEXT("OpenRuleMapGenEditor_Tooltip", "Open biome map generation rules editor."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FRuleMapGenModule::OpenMapRulesWindow))
    );
}

void FRuleMapGenModule::OpenMapRulesWindow()
{
    FGlobalTabmanager::Get()->TryInvokeTab(RuleMapGenTabName);
}

TSharedRef<SDockTab> FRuleMapGenModule::SpawnMapRulesTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SMapRulesEditorWindow)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRuleMapGenModule, RuleMapGen)
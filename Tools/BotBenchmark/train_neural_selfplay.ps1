param(
    [ValidateRange(10, 100000)]
    [int]$TrainPositions = 1200,

    [ValidateRange(10, 100000)]
    [int]$HoldoutPositions = 300,

    [ValidateRange(1, 32)]
    [int]$Rollouts = 2,

    [ValidateRange(2, 256)]
    [int]$CandidateCap = 10,

    [ValidateRange(1, 1000)]
    [int]$Epochs = 60,

    [ValidateRange(1.0, 3.0)]
    [double]$GreedyHealthMultiplier = 1.12,

    [ValidateRange(1.0, 3.0)]
    [double]$GreedyDamageMultiplier = 1.08,

    [ValidateRange(1, 64)]
    [int]$Workers = 6,

    [uint32]$Seed = 1592594996
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$DataDirectory = Join-Path $RepoRoot "Saved\BotTraining"
$Dataset = Join-Path $DataDirectory "neural_selfplay.csv"
New-Item -ItemType Directory -Force -Path $DataDirectory | Out-Null

& (Join-Path $PSScriptRoot "run_neural_benchmark.ps1") `
    -GenerateData $Dataset `
    -TrainPositions $TrainPositions `
    -HoldoutPositions $HoldoutPositions `
    -Rollouts $Rollouts `
    -CandidateCap $CandidateCap `
	-GreedyHealthMultiplier $GreedyHealthMultiplier `
	-GreedyDamageMultiplier $GreedyDamageMultiplier `
	-Workers $Workers `
    -Seed $Seed

& python (Join-Path $PSScriptRoot "train_neural_planner.py") `
    --data $Dataset `
    --epochs $Epochs `
    --seed $Seed
if ($LASTEXITCODE -ne 0) { throw "Neural self-play training failed with exit code $LASTEXITCODE." }

& (Join-Path $PSScriptRoot "run_neural_benchmark.ps1") -TestsOnly

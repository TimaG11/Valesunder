param(
    [ValidateRange(1, 100000)]
    [int]$Pairs = 1000,

    [uint32]$Seed = 1592594996,

    [ValidateRange(1, 1000000)]
    [int]$Nodes = 600,

    [ValidateRange(0.0, 1000.0)]
    [double]$Milliseconds = 5.0,

    [string]$JsonPath = "",

    [string]$GenerateData = "",

    [ValidateRange(1, 100000)]
    [int]$TrainPositions = 1200,

    [ValidateRange(1, 100000)]
    [int]$HoldoutPositions = 300,

    [ValidateRange(1, 32)]
    [int]$Rollouts = 2,

    [ValidateRange(2, 256)]
    [int]$CandidateCap = 10,

    [ValidateRange(1.0, 3.0)]
    [double]$GreedyHealthMultiplier = 1.0,

    [ValidateRange(1.0, 3.0)]
    [double]$GreedyDamageMultiplier = 1.0,

    [ValidateRange(1, 64)]
    [int]$Workers = 6,

    [ValidateRange(0.0, 1.0)]
    [double]$PolicyBlend = 0.55,

    [ValidateRange(0.0, 1.0)]
    [double]$ValueBlend = 0.55,

    [switch]$NoGate,

    [switch]$TestsOnly
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$VisualStudioRoot = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VisualStudioRoot) { throw "Visual Studio with the MSVC x64 toolchain was not found." }

$DevCmd = Join-Path $VisualStudioRoot "Common7\Tools\VsDevCmd.bat"
$Source = Join-Path $PSScriptRoot "HexBotNeuralBenchmark.cpp"
$Include = Join-Path $RepoRoot "Source\OtherBios"
$BuildRoot = Join-Path ([System.IO.Path]::GetTempPath()) "ValesunderNeuralBotBenchmark"
$Executable = Join-Path $BuildRoot "HexBotNeuralBenchmark.exe"
$ObjectFile = Join-Path $BuildRoot "HexBotNeuralBenchmark.obj"
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

$CompileCommand = '"{0}" -no_logo -arch=x64 -host_arch=x64 && cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /I"{1}" "{2}" /Fo:"{3}" /Fe:"{4}"' -f $DevCmd, $Include, $Source, $ObjectFile, $Executable
& cmd.exe /d /s /c $CompileCommand
if ($LASTEXITCODE -ne 0) { throw "Neural benchmark compilation failed with exit code $LASTEXITCODE." }

$Arguments = @(
    "--pairs", $Pairs.ToString(),
    "--seed", $Seed.ToString(),
    "--nodes", $Nodes.ToString(),
    "--milliseconds", $Milliseconds.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    "--greedy-health", $GreedyHealthMultiplier.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    "--greedy-damage", $GreedyDamageMultiplier.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    "--workers", $Workers.ToString(),
    "--policy-blend", $PolicyBlend.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    "--value-blend", $ValueBlend.ToString([System.Globalization.CultureInfo]::InvariantCulture)
)
if ($TestsOnly) { $Arguments = @("--test") }
if ($NoGate) { $Arguments += "--no-gate" }
if ($JsonPath) {
    $ResolvedJsonPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $JsonPath))
    $JsonParent = Split-Path -Parent $ResolvedJsonPath
    if ($JsonParent) { New-Item -ItemType Directory -Force -Path $JsonParent | Out-Null }
    $Arguments += @("--json", $ResolvedJsonPath)
}
if ($GenerateData) {
    $ResolvedDataPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $GenerateData))
    $DataParent = Split-Path -Parent $ResolvedDataPath
    if ($DataParent) { New-Item -ItemType Directory -Force -Path $DataParent | Out-Null }
    $Arguments += @(
        "--generate-data", $ResolvedDataPath,
        "--train-positions", $TrainPositions.ToString(),
        "--holdout-positions", $HoldoutPositions.ToString(),
        "--rollouts", $Rollouts.ToString(),
        "--candidate-cap", $CandidateCap.ToString()
    )
}

& $Executable @Arguments
if ($LASTEXITCODE -ne 0) { throw "Neural planner tests or metric gates failed with exit code $LASTEXITCODE." }

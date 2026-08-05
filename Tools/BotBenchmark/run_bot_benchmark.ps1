param(
    [ValidateRange(1, 1000000)]
    [int]$Pairs = 5000,

    [uint32]$Seed = 1592594996,

    [string]$JsonPath = "",

    [switch]$TestsOnly
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $VsWhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found. Install the MSVC C++ workload."
}

$VisualStudioRoot = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VisualStudioRoot) {
    throw "Visual Studio with the MSVC x64 toolchain was not found."
}

$DevCmd = Join-Path $VisualStudioRoot "Common7\Tools\VsDevCmd.bat"
$Source = Join-Path $PSScriptRoot "HexBotBenchmark.cpp"
$Include = Join-Path $RepoRoot "Source\OtherBios"
$BuildRoot = Join-Path ([System.IO.Path]::GetTempPath()) "ValesunderBotBenchmark"
$Executable = Join-Path $BuildRoot "HexBotBenchmark.exe"
$ObjectFile = Join-Path $BuildRoot "HexBotBenchmark.obj"
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

$CompileCommand = '"{0}" -no_logo -arch=x64 -host_arch=x64 && cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /I"{1}" "{2}" /Fo:"{3}" /Fe:"{4}"' -f $DevCmd, $Include, $Source, $ObjectFile, $Executable
& cmd.exe /d /s /c $CompileCommand
if ($LASTEXITCODE -ne 0) {
    throw "Bot benchmark compilation failed with exit code $LASTEXITCODE."
}

$Arguments = @("--pairs", $Pairs.ToString(), "--seed", $Seed.ToString())
if ($TestsOnly) {
    $Arguments = @("--test")
}
if ($JsonPath) {
    $ResolvedJsonPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $JsonPath))
    $JsonParent = Split-Path -Parent $ResolvedJsonPath
    if ($JsonParent) {
        New-Item -ItemType Directory -Force -Path $JsonParent | Out-Null
    }
    $Arguments += @("--json", $ResolvedJsonPath)
}

& $Executable @Arguments
if ($LASTEXITCODE -ne 0) {
    if ($TestsOnly) {
        throw "Bot decision-model tests failed with exit code $LASTEXITCODE."
    }
    throw "Bot benchmark or its metric gates failed with exit code $LASTEXITCODE."
}

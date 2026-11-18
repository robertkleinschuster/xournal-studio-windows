# Builds the Release (fre) runtime of the WinUI fork locally - the same steps as the fork's Actions workflow
# (.github/workflows/winui-source-product.yml, x64fre). For iterating on WinUI fixes: after the first build only the
# changed files are compiled and linked again. Releases for the app's pin still come from the fork's Actions.
# Usage: tools\Build-WinUILocal.ps1 -Source D:\wb  (a checkout/worktree of the fork; short path: WinUI's paths are long)
# The payload lands in <Source>\BuildOutput\packaging\Release\runtimes\win10-x64\native.
param(
    [Parameter(Mandatory)][string]$Source,
    [string]$Log = (Join-Path $Source 'local-build.log')
)
$ErrorActionPreference = 'Stop'
Set-Location $Source
$env:NUGET_PACKAGES = Join-Path $Source 'packages'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:VisualStudioVersion = '18.0'
$env:NuGetAudit = 'false'
$env:PGOBuildMode = 'Off'
# Never let a restore ask for credentials (CLAUDE.md: WinUI restores must not prompt). The feeds of upstream main are public.
$env:NUGET_CREDENTIALPROVIDER_FORCE_CANSHOWDIALOG_TO = 'false'

$start = Get-Date
.\init.ps1 x64fre /nopgo /notitle
if ($LASTEXITCODE -ne 0) { throw "WinUI initialization failed with exit code $LASTEXITCODE" }
.\build.cmd product /restore /q
if ($LASTEXITCODE -ne 0) { throw "WinUI product build failed with exit code $LASTEXITCODE" }
$payload = Join-Path $Source 'BuildOutput\packaging\Release\runtimes\win10-x64\native'
if (-not (Test-Path $payload)) { throw "Missing product payload: $payload" }
"Built $((Get-ChildItem $payload -File -Recurse).Count) runtime files in $([int]((Get-Date) - $start).TotalMinutes) min: $payload"

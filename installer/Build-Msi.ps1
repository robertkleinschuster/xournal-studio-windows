# Builds the per-user MSI of Xournal Studio (Win32) from a staged app folder with the WiX Toolset (the `wix` .NET tool,
# pinned in dotnet-tools.json next to this script; needs the .NET SDK).
#   installer\Build-Msi.ps1 -Version 0.2.0                 stage = .local-build\install-stage (install.ps1 -SkipInstall)
#   installer\Build-Msi.ps1 -Version 0.2.0 -Stage <folder> -Out <file.msi>
#   installer\Build-Msi.ps1                                no -Version: a local build, numbered as the NEXT release (the
#       newest v* tag with its patch + 1, e.g. tag v0.3.8 -> 0.3.9) and named ...-<version>-local-x64.msi. One numbering for
#       releases and local builds: the release that gets that number replaces a local build (AllowSameVersionUpgrades).
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Stage = (Join-Path (Split-Path $PSScriptRoot) '.local-build\install-stage'),
    [string]$Out
)
$ErrorActionPreference = 'Stop'
$local = -not $Version
if ($local) {
    $tag = (& git -C $PSScriptRoot describe --tags --abbrev=0 --match 'v[0-9]*').Trim()
    if ($LASTEXITCODE -or $tag -notmatch '^v(\d+)\.(\d+)\.(\d+)$') { throw "no release tag vX.Y.Z to number a local build from (got '$tag')" }
    $Version = "$($Matches[1]).$($Matches[2]).$([int]$Matches[3] + 1)"
}
if (-not $Out) {
    $Out = Join-Path (Split-Path $PSScriptRoot) ".local-build\XournalStudio-Win32-$Version$(if ($local) { '-local' })-x64.msi"
}
$Stage = (Resolve-Path $Stage).Path
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)   # (wix runs in installer\)
if (-not (Test-Path (Join-Path $Stage 'XournalStudio.exe'))) { throw "no XournalStudio.exe in $Stage" }
$icon = Join-Path (Split-Path $PSScriptRoot) 'app\app.ico'

Push-Location $PSScriptRoot
try {
    & dotnet tool restore
    if ($LASTEXITCODE) { throw "dotnet tool restore failed ($LASTEXITCODE)" }
    & dotnet tool run wix build XournalStudio.wxs -arch x64 -d "Version=$Version" -d "Stage=$Stage" -d "AppIcon=$icon" `
        -o $Out
    if ($LASTEXITCODE) { throw "wix build failed ($LASTEXITCODE)" }
    # The ICE checks (wix build does not run them), except three about roaming profiles: ICE38 (file key paths in the
    # profile), ICE64 (profile folders in RemoveFile), ICE91 (per-user folders). This package is per-user only and
    # installs into the local, not roaming, AppData; the sandbox test checks that uninstalling leaves no folder behind.
    & dotnet tool run wix msi validate -sice ICE38 -sice ICE64 -sice ICE91 $Out
    if ($LASTEXITCODE) { throw "MSI validation failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}
"Built $Out"

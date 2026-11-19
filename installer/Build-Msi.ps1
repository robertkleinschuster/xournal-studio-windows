# Builds the per-user MSI of Xournal Studio (Win32) from a staged app folder with the WiX Toolset (the `wix` .NET tool,
# pinned in dotnet-tools.json next to this script; needs the .NET SDK).
#   installer\Build-Msi.ps1 -Version 0.2.0                 stage = .local-build\install-stage (install.ps1 -SkipInstall)
#   installer\Build-Msi.ps1 -Version 0.2.0 -Stage <folder> -Out <file.msi>
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Stage = (Join-Path (Split-Path $PSScriptRoot) '.local-build\install-stage'),
    [string]$Out = (Join-Path (Split-Path $PSScriptRoot) ".local-build\XournalStudio-Win32-$Version-x64.msi")
)
$ErrorActionPreference = 'Stop'
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

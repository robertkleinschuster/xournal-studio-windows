# Builds the per-user installer of Xournal Studio (Win32) from a staged app folder with Inno Setup 6 (ISCC.exe, a native
# compiler: `winget install JRSoftware.InnoSetup --scope user`; the CI installs it the same way).
#   installer\Build-Installer.ps1 -Version 0.3.9           stage = .local-build\install-stage (install.ps1 -SkipInstall)
#   installer\Build-Installer.ps1 -Version 0.3.9 -Stage <folder> -Out <file.exe>
#   installer\Build-Installer.ps1                          no -Version: a local build, numbered as the NEXT release (the
#       newest v* tag with its patch + 1, e.g. tag v0.3.8 -> 0.3.9), named ...-<version>-local-x64-setup.exe. One numbering
#       for releases and local builds: a setup of the same or a newer version installs over the installed one.
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Stage = (Join-Path (Split-Path $PSScriptRoot) '.local-build\install-stage'),
    [string]$Out
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$local = -not $Version
if ($local) {
    $tag = (& git -C $PSScriptRoot describe --tags --abbrev=0 --match 'v[0-9]*').Trim()
    if ($LASTEXITCODE -or $tag -notmatch '^v(\d+)\.(\d+)\.(\d+)$') { throw "no release tag vX.Y.Z to number a local build from (got '$tag')" }
    $Version = "$($Matches[1]).$($Matches[2]).$([int]$Matches[3] + 1)"
}
if (-not $Out) { $Out = Join-Path $repo ".local-build\XournalStudio-Win32-$Version$(if ($local) { '-local' })-x64-setup.exe" }
$Stage = (Resolve-Path $Stage).Path
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
if (-not (Test-Path (Join-Path $Stage 'XournalStudio.exe'))) { throw "no XournalStudio.exe in $Stage" }

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw 'Inno Setup 6 not found (ISCC.exe): winget install JRSoftware.InnoSetup --scope user' }

& $iscc /Q "/DVersion=$Version" "/DStage=$Stage" "/DAppIcon=$(Join-Path $repo 'app\app.ico')" `
    "/DOutputDir=$(Split-Path $Out)" "/DOutputName=$([IO.Path]::GetFileNameWithoutExtension($Out))" `
    (Join-Path $PSScriptRoot 'XournalStudio.iss')
if ($LASTEXITCODE) { throw "ISCC failed ($LASTEXITCODE)" }
if (-not (Test-Path $Out)) { throw "ISCC did not write $Out" }
"Built $Out"

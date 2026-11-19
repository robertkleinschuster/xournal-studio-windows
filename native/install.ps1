# Builds the native Xournal Studio (C++/Win32) and installs it for the current user, side by side with the C# app:
#   %LOCALAPPDATA%\Programs\Xournal Studio (Win32)  +  Start menu entry "Xournal Studio (Win32)".
# The C# app's folder and shortcut are not touched; no file associations are registered.
# What is installed is the committed state (HEAD): it is built in a clean git worktree (.local-build\install-source,
# with its own build folder), so changes in the working tree - also other agents' work in progress - never reach it.
#   .\install.ps1              unit tests, Release build, install, start check
#   .\install.ps1 -SkipTests   without the unit tests
#   .\install.ps1 -SkipInstall build and stage only (.local-build\install-stage)
# Next to the exe: pdfium.dll (when the build puts it there) and licenses\ - the license texts of the third-party code
# the exe contains (THIRD-PARTY.md); a missing license text stops the install.
param([switch]$SkipTests, [switch]$SkipInstall, [string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
$native = $PSScriptRoot
$repo = Split-Path $native
$source = Join-Path $native '.local-build\install-source'
$build = Join-Path $source 'native\.local-build\cmake'
$stage = Join-Path $native '.local-build\install-stage'
$target = Join-Path $env:LOCALAPPDATA 'Programs\Xournal Studio (Win32)'
$programs = [Environment]::GetFolderPath('Programs')
$exeName = 'XournalStudio.exe'

# Refused while the installed native app runs (the C# app's process lives elsewhere and does not count).
function Assert-NotRunning {
    $running = Get-Process XournalStudio -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$target\*" }
    if ($running) { throw 'Xournal Studio (Win32) is running; close it first or use -SkipInstall.' }
}
if (-not $SkipInstall) { Assert-NotRunning }

# The committed state in its own worktree (kept between installs: its build folder makes the next build incremental).
# (git reports progress on stderr: with its output redirected, Windows PowerShell would turn that into an error record
# and stop - the exit code decides instead.)
function Invoke-Git {
    $ErrorActionPreference = 'Continue'
    & git.exe @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "git $args failed ($LASTEXITCODE)" }
}
$commit = (& git.exe -C $repo rev-parse HEAD).Trim()
if (-not (Test-Path (Join-Path $source '.git'))) {
    Invoke-Git -C $repo worktree prune
    Invoke-Git -C $repo worktree add --detach $source $commit
}
else {
    Invoke-Git -C $source checkout --detach --force $commit
}
Invoke-Git -C $source submodule update --init native/external/InkPages

# Configure, build (all modules) and run the unit tests (CTest) - the worktree's build.ps1.
$buildArgs = @{ Config = $Config }
if ($SkipTests) { $buildArgs.NoTests = $true }
& (Join-Path $source 'native\build.ps1') @buildArgs

# Stage: the exe, the DLLs the build put next to it (pdfium.dll), its licenses folder, and the license texts of the
# statically linked third-party code (from the fetched sources in the build tree).
$output = Join-Path $build "app\$Config"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $output $exeName) $stage
Get-ChildItem $output -Filter *.dll | Copy-Item -Destination $stage
if (Test-Path (Join-Path $output 'licenses')) { Copy-Item -Recurse (Join-Path $output 'licenses') $stage }
$licenses = @(
    @{ Name = 'zlib'; Files = @('_deps\zlib-src\LICENSE') },
    @{ Name = 'qpdf'; Files = @('_deps\qpdf-src\LICENSE.txt', '_deps\qpdf-src\NOTICE.md') },
    @{ Name = 'libjpeg-turbo'; Files = @('pdf\libjpeg_turbo_build-prefix\src\libjpeg_turbo_build\LICENSE.md') },
    @{ Name = 'protozero'; Files = @('_deps\protozero-src\LICENSE.md', '_deps\protozero-src\LICENSE.from_folly') },
    @{ Name = 'libgit2'; Files = @('services\git\libgit2_build-prefix\src\libgit2_build\COPYING') }
)
foreach ($license in $licenses) {
    $folder = Join-Path $stage "licenses\$($license.Name)"
    New-Item -ItemType Directory -Force $folder | Out-Null
    foreach ($file in $license.Files) {
        $source = Join-Path $build $file
        if (-not (Test-Path $source)) { throw "license text missing: $source" }
        Copy-Item $source $folder
    }
}
Copy-Item (Join-Path $source 'native\THIRD-PARTY.md') (Join-Path $stage 'licenses')

if ($SkipInstall) { "Staged in $stage"; return }

Assert-NotRunning # (the build took a while)
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse $stage $target

$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path $programs 'Xournal Studio (Win32).lnk'))
$link.TargetPath = Join-Path $target $exeName
$link.WorkingDirectory = $target
$link.IconLocation = (Join-Path $target $exeName) + ',0'
$link.Save()

# Start check: the installed exe opens its window and closes with exit code 0. Its own settings folder for the check,
# so the user's settings, last document and recovery copies are not touched.
$check = Join-Path $env:TEMP 'xournal-win32-install-check'
if (Test-Path $check) { Remove-Item -Recurse -Force $check }
New-Item -ItemType Directory -Force $check | Out-Null
$env:XOURNAL_STUDIO_CONFIG_DIR = $check
try {
    $process = Start-Process (Join-Path $target $exeName) -PassThru
    for ($i = 0; $i -lt 40 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw "the installed app showed no window (exited: $($process.HasExited))" }
    $process.CloseMainWindow() | Out-Null
    if (-not $process.WaitForExit(10000)) { $process.Kill(); throw 'the installed app did not close' }
    if ($process.ExitCode -ne 0) { throw "the installed app exited with code $($process.ExitCode)" }
}
finally {
    Remove-Item Env:XOURNAL_STUDIO_CONFIG_DIR
    if (Test-Path "$check\errors.log") { Get-Content "$check\errors.log" | ForEach-Object { "errors.log: $_" } }
}
"Installed $($commit.Substring(0, 7)) to $target (start check passed)"

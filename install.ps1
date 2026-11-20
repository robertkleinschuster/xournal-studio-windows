# Builds the native Xournal Studio (C++/Win32) and installs it for the current user, side by side with the C# app:
#   %LOCALAPPDATA%\Programs\Xournal Studio (Win32)  +  Start menu entry "Xournal Studio (Win32)".
# The C# app's folder and shortcut are not touched; no file associations are registered.
# What is installed is the committed state (HEAD): it is built in a clean git worktree (.local-build\install-source,
# with its own build folder), so changes in the working tree - also other agents' work in progress - never reach it.
#   .\install.ps1              unit tests, Release build, install, start check
#   .\install.ps1 -SkipTests   without the unit tests
#   .\install.ps1 -SkipInstall build and stage only (.local-build\install-stage)
#   .\install.ps1 -FromCi      no local build: the app CI staged for main (the newest green push run, gh run
#                              download; tools\CiArtifact.ps1); -Commit <rev>: that commit's run. Same checks and install.
#   -SourceDir <dir>           the worktree elsewhere (CI: a short path - MSBuild's file tracker fails beyond MAX_PATH
#                              in the nested libjpeg-turbo build under the runner's long checkout path)
# Next to the exe: pdfium.dll (when the build puts it there) and licenses\ - the license texts of the third-party code
# the exe contains (THIRD-PARTY.md); a missing license text stops the install.
param([switch]$SkipTests, [switch]$SkipInstall, [string]$Config = 'Release', [string]$SourceDir = '', [switch]$FromCi,
    [string]$Commit = '')

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$source = if ($SourceDir) { $SourceDir } else { Join-Path $repo '.local-build\install-source' }
$build = Join-Path $source '.local-build\cmake'
$stage = Join-Path $repo '.local-build\install-stage'
$target = Join-Path $env:LOCALAPPDATA 'Programs\Xournal Studio (Win32)'
$programs = [Environment]::GetFolderPath('Programs')
$exeName = 'XournalStudio.exe'

# Refused while the installed native app runs (the C# app's process lives elsewhere and does not count).
function Assert-NotRunning {
    $running = Get-Process XournalStudio -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$target\*" }
    if ($running) { throw 'Xournal Studio (Win32) is running; close it first or use -SkipInstall.' }
}
# install.ps1 is for development, the MSI (installer\, GitHub releases) for releases; both use the same folder and
# Start menu entry. While the MSI is installed this script refuses: replacing the folder under Windows Installer's feet
# would leave the MSI's registration pointing at files it no longer owns (repair or uninstall would remove this build).
# The MSI is found by its UpgradeCode (installer\XournalStudio.wxs).
function Assert-NoMsi {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $products = @($installer.GetType().InvokeMember('RelatedProducts', 'GetProperty', $null, $installer,
        @('{2ED20F25-015E-4F3B-9B2A-7572D4BBBB6A}')))
    if ($products.Count -gt 0) {
        throw "Xournal Studio (Win32) is installed from its MSI ($($products -join ', ')); uninstall it first (Apps & Features, or msiexec /x $($products[0])) or use -SkipInstall."
    }
}
if (-not $SkipInstall) { Assert-NotRunning; Assert-NoMsi }

if ($FromCi) {
    # The staged app of the newest green CI run of that commit (tools\CiArtifact.ps1), instead of a local build.
    . (Join-Path $repo 'tools\CiArtifact.ps1')
    $run = Get-XournalCiApp -Repo $repo -Commit $Commit -Destination $stage
    $installed = "CI run $($run.databaseId) of $($run.headSha.Substring(0, 7)) ($($run.url))"
}
else {
    # One build at a time on this machine (tools\BuildLock.ps1): held from the checkout to the staged files; the
    # worktree's build.ps1 enters the same lock again (same thread).
    . (Join-Path $repo 'tools\BuildLock.ps1')
    $lock = Enter-XournalBuildLock "install.ps1 in $repo"
    try {
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
        Invoke-Git -C $source submodule update --init external/InkPages

        # Configure, build (all modules) and run the unit tests (CTest) - the worktree's build.ps1.
        $buildArgs = @{ Config = $Config }
        if ($SkipTests) { $buildArgs.NoTests = $true }
        & (Join-Path $source 'build.ps1') @buildArgs

        # Stage: the exe, the DLLs the build put next to it (pdfium.dll), its licenses folder, and the license texts of the
        # statically linked third-party code (the configure copies them from the dependency cache to <build>\licenses,
        # cmake/DependencyCache.cmake).
        $output = Join-Path $build "app\$Config"
        if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
        New-Item -ItemType Directory -Force $stage | Out-Null
        Copy-Item (Join-Path $output $exeName) $stage
        Get-ChildItem $output -Filter *.dll | Copy-Item -Destination $stage
        if (Test-Path (Join-Path $output 'licenses')) { Copy-Item -Recurse (Join-Path $output 'licenses') $stage }
        $licenses = @(
            @{ Name = 'zlib'; Files = @('LICENSE') },
            @{ Name = 'qpdf'; Files = @('LICENSE.txt', 'NOTICE.md') },
            @{ Name = 'libjpeg-turbo'; Files = @('LICENSE.md') },
            @{ Name = 'protozero'; Files = @('LICENSE.md', 'LICENSE.from_folly') },
            @{ Name = 'libgit2'; Files = @('COPYING') }
        )
        foreach ($license in $licenses) {
            $folder = Join-Path $stage "licenses\$($license.Name)"
            New-Item -ItemType Directory -Force $folder | Out-Null
            foreach ($file in $license.Files) {
                $text = Join-Path $build "licenses\$($license.Name)\$file"
                if (-not (Test-Path $text)) { throw "license text missing: $text" }
                Copy-Item $text $folder
            }
        }
        Copy-Item (Join-Path $source 'THIRD-PARTY.md') (Join-Path $stage 'licenses')
    }
    finally { Exit-XournalBuildLock $lock }
    $installed = $commit.Substring(0, 7)
}

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
"Installed $installed to $target (start check passed)"

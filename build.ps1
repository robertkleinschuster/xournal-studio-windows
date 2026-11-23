# Builds the native Xournal Studio and runs the unit tests: a thin wrapper around CMake's presets (CMakePresets.json:
# generator Ninja Multi-Config, vcpkg toolchain and triplet, ccache, build and test presets). It adds what presets cannot:
# the x64 developer environment (tools\VsDevEnv.ps1; also sets VCPKG_ROOT from the user environment) and the machine-wide
# build lock (tools\BuildLock.ps1: one build at a time across all worktrees; a second build waits and names the holder).
# - Third-party libraries: vcpkg, manifest mode (vcpkg.json), binary cache %LOCALAPPDATA%\vcpkg\archives. The project's
#   own code: the installed ccache (winget install Ccache.Ccache), one cache for all worktrees (%LOCALAPPDATA%\ccache).
#   .\build.ps1                              build everything + all unit tests
#   .\build.ps1 -NoTests                     build only
#   .\build.ps1 -Target XournalStudio        build one target (and what it needs); no tests unless -Tests is given
#   .\build.ps1 -Tests 'Pdf|Xml'             only the tests whose name matches the CTest regex (ctest -R)
#   .\build.ps1 -Target XournalTests -Tests '^Xml'
#   .\build.ps1 -Config Debug                the debug presets
# CI's regex of tests the machine cannot run: $env:XOURNAL_TEST_EXCLUDE (ctest -E). Build folder .local-build\cmake.
param([switch]$NoTests, [string]$Config = 'Release', [string[]]$Target, [string]$Tests)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools\VsDevEnv.ps1')
. (Join-Path $PSScriptRoot 'tools\BuildLock.ps1')
$vs = Enter-XournalVsDevEnv # Ninja calls cl.exe directly: the developer prompt's environment
$cmake = Get-XournalCMake $vs
$preset = $Config.ToLower()
$lock = Enter-XournalBuildLock "build.ps1 in $PSScriptRoot"
try {
    Push-Location $PSScriptRoot
    & $cmake --preset default; if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
    $targets = @(); if ($Target) { $targets = @('--target') + $Target }
    & $cmake --build --preset $preset @targets | Tee-Object (Join-Path $PSScriptRoot '.local-build/build-output.log') # CI's job summary lists its errors
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
    if (-not $NoTests -and (-not $Target -or $Tests)) {
        $filter = @(); if ($Tests) { $filter += '-R', $Tests }; if ($env:XOURNAL_TEST_EXCLUDE) { $filter += '-E', $env:XOURNAL_TEST_EXCLUDE }
        & (Get-XournalCMake $vs 'ctest') --preset $preset @filter; if ($LASTEXITCODE) { throw "unit tests failed ($LASTEXITCODE)" }
    }
}
finally { Pop-Location; Exit-XournalBuildLock $lock }

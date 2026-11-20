# Builds the native Xournal Studio with the CMake that ships with Visual Studio and runs the unit tests.
# Third-party code comes from the shared dependency cache (cmake/DependencyCache.cmake): built once per pin,
# configuration and toolchain into %LOCALAPPDATA%\XournalStudioBuildCache ($env:XOURNAL_DEPS_CACHE), reused by every
# worktree.
#   .\build.ps1                              build everything + all unit tests
#   .\build.ps1 -NoTests                     build only
#   .\build.ps1 -Target XournalStudio        build one target (and what it needs); no tests unless -Tests is given
#   .\build.ps1 -Tests 'Pdf|Xml'             only the tests whose name matches the CTest regex (ctest -R)
#   .\build.ps1 -Target XournalTests -Tests '^Xml'
#   .\build.ps1 -Config Debug                another configuration (its dependencies are built into the cache once)
# One build at a time on this machine (tools\BuildLock.ps1): a second build waits and names the holder (worktree, PID).
# Agents build only what they changed (-Target ... -Tests ...), so each build holds the lock briefly.
param([switch]$NoTests, [string]$Config = 'Release', [string[]]$Target, [string]$Tests)
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
if (-not $vs) { throw 'Visual Studio not found (vswhere)' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $PSScriptRoot '.local-build\cmake'

# Machine-wide build lock (one build at a time across all worktrees), released when the script ends in any way.
. (Join-Path $PSScriptRoot 'tools\BuildLock.ps1')
$lock = Enter-XournalBuildLock "build.ps1 in $PSScriptRoot"
try {
    # Configure only a new build tree or for a configuration whose dependencies it has not set up yet; otherwise the
    # build's ZERO_CHECK re-runs CMake by itself when a CMake file changed (keeps builds incremental and quick).
    $cacheFile = Join-Path $build 'CMakeCache.txt'
    $configs = @()
    if (Test-Path $cacheFile) {
        $line = Select-String -Path $cacheFile -Pattern '^XOURNAL_DEPS_CONFIGS:STRING=(.*)$' | Select-Object -First 1
        if ($line) { $configs = @($line.Matches[0].Groups[1].Value -split ';' | Where-Object { $_ }) }
    }
    # (CMakeFiles\generate.stamp: written when a configure and generate finished.)
    $generated = Test-Path (Join-Path $build 'CMakeFiles\generate.stamp')
    if (-not $generated -or $configs -notcontains $Config) {
        $configs = @($configs + $Config | Select-Object -Unique)
        & $cmake -S $PSScriptRoot -B $build -A x64 "-DXOURNAL_DEPS_CONFIGS=$($configs -join ';')"
        if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
    }
    $targetArgs = @()
    if ($Target) { $targetArgs = @('--target') + $Target }
    # The build's output also goes to .local-build\build-output.log (CI lists its compiler errors in the job summary).
    $buildLog = Join-Path $PSScriptRoot '.local-build\build-output.log'
    & $cmake --build $build --config $Config --parallel @targetArgs | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

    if (-not $NoTests -and (-not $Target -or $Tests)) {
        $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
        # Only this project's tests: InkPages' tests (excluded from the build here) run in its own repository.
        # XOURNAL_TEST_EXCLUDE: a CTest regex of tests the machine cannot run (CI: the runner has only the en-US
        # handwriting recognizer, the search test writes German).
        $filter = @()
        if ($Tests) { $filter += @('-R', $Tests); "tests: $Tests" }
        if ($env:XOURNAL_TEST_EXCLUDE) { $filter += @('-E', $env:XOURNAL_TEST_EXCLUDE); "excluded tests: $env:XOURNAL_TEST_EXCLUDE" }
        # In parallel (one process per test); results also as JUnit in .local-build\ctest-junit.xml (CI's job summary).
        $junit = Join-Path $PSScriptRoot '.local-build\ctest-junit.xml'
        & $ctest --test-dir (Join-Path $build 'tests') -C $Config --output-on-failure --no-tests=error `
            --parallel $env:NUMBER_OF_PROCESSORS --output-junit $junit @filter
        if ($LASTEXITCODE) { throw "unit tests failed ($LASTEXITCODE)" }
    }
}
finally { Exit-XournalBuildLock $lock }

# Builds the native Xournal Studio with Visual Studio's CMake, Ninja (Multi-Config) and MSVC, and runs the unit tests.
# - Third-party code comes from the shared dependency cache (cmake/DependencyCache.cmake): built once per pin,
#   configuration and toolchain into %LOCALAPPDATA%\XournalStudioBuildCache ($env:XOURNAL_DEPS_CACHE).
# - The project's own code is compiled through ccache (pinned, XOURNAL_CCACHE): one cache for all worktrees
#   (%LOCALAPPDATA%\ccache, or $env:CCACHE_DIR).
# - One build at a time on this machine (tools\BuildLock.ps1): a second build waits and names the holder (worktree,
#   PID). Agents build only what they changed (-Target ... -Tests ...), so each build holds the lock briefly.
#   .\build.ps1                              build everything + all unit tests
#   .\build.ps1 -NoTests                     build only
#   .\build.ps1 -Target XournalStudio        build one target (and what it needs); no tests unless -Tests is given
#   .\build.ps1 -Tests 'Pdf|Xml'             only the tests whose name matches the CTest regex (ctest -R)
#   .\build.ps1 -Target XournalTests -Tests '^Xml'
#   .\build.ps1 -Config Debug                another configuration (its dependencies are built into the cache once)
# Build folder .local-build\cmake (app\<Config>\XournalStudio.exe, tests\...).
param([switch]$NoTests, [string]$Config = 'Release', [string[]]$Target, [string]$Tests)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools\VsDevEnv.ps1')
. (Join-Path $PSScriptRoot 'tools\BuildLock.ps1')
$vs = Enter-XournalVsDevEnv # Ninja calls cl.exe directly: the developer prompt's environment
$cmake = Get-XournalCMake $vs
$build = Join-Path $PSScriptRoot '.local-build\cmake'
$cacheFile = Join-Path $build 'CMakeCache.txt'

# Machine-wide build lock (one build at a time across all worktrees), released when the script ends in any way.
$lock = Enter-XournalBuildLock "build.ps1 in $PSScriptRoot"
try {
    # A tree of the earlier Visual Studio generator cannot switch generators: it is removed and configured again.
    if (Test-Path $cacheFile) {
        $generator = Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1
        if ($generator -and $generator.Matches[0].Groups[1].Value -ne 'Ninja Multi-Config') {
            "build folder of the generator '$($generator.Matches[0].Groups[1].Value)': removed for Ninja ($build)"
            Remove-Item -Recurse -Force $build
        }
    }
    # Configure only a new build tree or for a configuration whose dependencies it has not set up yet; otherwise Ninja
    # re-runs CMake by itself when a CMake file changed (keeps builds incremental and quick).
    $configs = @()
    if (Test-Path $cacheFile) {
        $line = Select-String -Path $cacheFile -Pattern '^XOURNAL_DEPS_CONFIGS:STRING=(.*)$' | Select-Object -First 1
        if ($line) { $configs = @($line.Matches[0].Groups[1].Value -split ';' | Where-Object { $_ }) }
    }
    if (-not (Test-Path (Join-Path $build 'build.ninja')) -or $configs -notcontains $Config) {
        $configs = @($configs + $Config | Select-Object -Unique)
        & $cmake -S $PSScriptRoot -B $build -G 'Ninja Multi-Config' "-DCMAKE_MAKE_PROGRAM=$(Get-XournalNinja $vs)" `
            -DXOURNAL_CCACHE=ON "-DXOURNAL_DEPS_CONFIGS=$($configs -join ';')"
        if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
    }
    $line = Select-String -Path $cacheFile -Pattern '^XOURNAL_CCACHE_EXE:INTERNAL=(.*)$' | Select-Object -First 1
    if (-not $line) { throw "XOURNAL_CCACHE_EXE missing in $cacheFile" }
    $ccache = $line.Matches[0].Groups[1].Value
    & $ccache --zero-stats | Out-Null
    if ($LASTEXITCODE) { throw "ccache --zero-stats failed ($LASTEXITCODE)" }

    $targetArgs = @()
    if ($Target) { $targetArgs = @('--target') + $Target }
    # The build's output also goes to .local-build\build-output.log (CI lists its compiler errors in the job summary).
    $buildLog = Join-Path $PSScriptRoot '.local-build\build-output.log'
    & $cmake --build $build --config $Config --parallel @targetArgs | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
    & $ccache --show-stats
    if ($LASTEXITCODE) { throw "ccache --show-stats failed ($LASTEXITCODE)" }

    if (-not $NoTests -and (-not $Target -or $Tests)) {
        $ctest = Get-XournalCMake $vs 'ctest'
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

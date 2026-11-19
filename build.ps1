# Builds the native Xournal Studio (Release) with the CMake that ships with Visual Studio and runs the unit tests
# (GoogleTest and zlib are fetched once at configure time, pinned releases).
#   .\build.ps1            build + unit tests
#   .\build.ps1 -NoTests   build only
param([switch]$NoTests, [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
if (-not $vs) { throw 'Visual Studio not found (vswhere)' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $PSScriptRoot '.local-build\cmake'

& $cmake -S $PSScriptRoot -B $build -A x64
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config $Config
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

if (-not $NoTests) {
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    # Only this project's tests: InkPages' tests (excluded from the build here) run in its own repository.
    & $ctest --test-dir (Join-Path $build 'tests') -C $Config --output-on-failure
    if ($LASTEXITCODE) { throw "unit tests failed ($LASTEXITCODE)" }
}

# Runs a command under the machine-wide build lock (tools\BuildLock.ps1), for builds that do not go through build.ps1:
#   .\tools\with-build-lock.ps1 cmake --build .local-build\cmake --config Release --target XournalTests
#   .\tools\with-build-lock.ps1 ctest --test-dir .local-build\cmake\tests -C Release -R '^Pdf'
# "cmake" and "ctest" mean the ones that ship with Visual Studio. The command's exit code is the script's.
$ErrorActionPreference = 'Stop'
if ($args.Count -eq 0) { throw 'usage: with-build-lock.ps1 <command> [arguments...]' }
$command = [string]$args[0]
$arguments = @($args | Select-Object -Skip 1)
if ($command -eq 'cmake' -or $command -eq 'ctest') {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
    if (-not $vs) { throw 'Visual Studio not found (vswhere)' }
    $command = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$command.exe"
}
. (Join-Path $PSScriptRoot 'BuildLock.ps1')
$lock = Enter-XournalBuildLock "$command in $((Get-Location).Path)"
try {
    & $command @arguments
    $code = $LASTEXITCODE
}
finally { Exit-XournalBuildLock $lock }
exit $code

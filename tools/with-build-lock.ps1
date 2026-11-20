# Runs a command under the machine-wide build lock (tools\BuildLock.ps1) in Visual Studio's x64 developer environment
# (tools\VsDevEnv.ps1; Ninja calls cl.exe directly), for builds that do not go through build.ps1:
#   .\tools\with-build-lock.ps1 cmake --build .local-build\cmake --config Release --target XournalTests
#   .\tools\with-build-lock.ps1 ctest --test-dir .local-build\cmake\tests -C Release -R '^Pdf'
# "cmake" and "ctest" mean the ones that ship with Visual Studio. The command's exit code is the script's.
$ErrorActionPreference = 'Stop'
if ($args.Count -eq 0) { throw 'usage: with-build-lock.ps1 <command> [arguments...]' }
$command = [string]$args[0]
$arguments = @($args | Select-Object -Skip 1)
. (Join-Path $PSScriptRoot 'VsDevEnv.ps1')
. (Join-Path $PSScriptRoot 'BuildLock.ps1')
$vs = Enter-XournalVsDevEnv
if ($command -eq 'cmake' -or $command -eq 'ctest') { $command = Get-XournalCMake $vs $command }
$lock = Enter-XournalBuildLock "$command in $((Get-Location).Path)"
try {
    & $command @arguments
    $code = $LASTEXITCODE
}
finally { Exit-XournalBuildLock $lock }
exit $code

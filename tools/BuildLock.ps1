# Machine-wide locks across all worktrees: named mutexes, which Windows releases when their process ends (also on
# Ctrl+C or a crash); the same thread may enter one again (install.ps1 holds the build lock while it runs the
# worktree's build.ps1). Who holds a lock is written to %LOCALAPPDATA%\XournalStudioLocks\<lock>-holder.txt, so a waiting script
# can say whom it waits for.
#   Global\XournalStudioBuild    build-lock     one build at a time (build.ps1, install.ps1, tools\with-build-lock.ps1)
#   Global\XournalStudioSandbox  sandbox-lock   one sandbox UI test at a time (UiTests\Run-AppTest.ps1)
#   . (Join-Path $PSScriptRoot 'tools\BuildLock.ps1')
#   $lock = Enter-XournalBuildLock 'build.ps1 in D:\xsui'
#   try { ... } finally { Exit-XournalBuildLock $lock }

function Enter-XournalMachineLock([string]$MutexName, [string]$Lock, [string]$What) {
    $root = Join-Path $env:LOCALAPPDATA 'XournalStudioLocks'
    New-Item -ItemType Directory -Force $root | Out-Null
    $holderFile = Join-Path $root "$Lock-holder.txt"
    $mutex = New-Object System.Threading.Mutex($false, $MutexName)
    $acquired = $false
    $shown = $null
    $started = Get-Date
    while (-not $acquired) {
        # Waits on the mutex itself (no sleep); the 2 s timeout only lets Ctrl+C through and the holder be reported.
        try { $acquired = $mutex.WaitOne(2000) }
        catch [System.Threading.AbandonedMutexException] {
            $acquired = $true # the mutex is ours; its previous holder ended without releasing it
            Write-Host "${Lock}: the previous holder ended without releasing it ($($_.Exception.Message))"
        }
        if (-not $acquired) {
            $holder = 'unknown holder'
            if (Test-Path $holderFile) {
                try { $holder = (Get-Content -Raw $holderFile).Trim() }
                catch { $holder = "unknown holder ($holderFile not readable: $($_.Exception.Message))" }
            }
            if ($holder -ne $shown) { Write-Host "${Lock}: waiting for $holder"; $shown = $holder }
        }
    }
    if ($shown) { Write-Host ("${Lock}: acquired after {0:n0} s" -f ((Get-Date) - $started).TotalSeconds) }
    Set-Content -Encoding utf8 $holderFile "$What (PID $PID, since $(Get-Date -Format s))"
    $mutex
}

function Exit-XournalMachineLock($Mutex) {
    $Mutex.ReleaseMutex()
    $Mutex.Dispose()
}

function Enter-XournalBuildLock([string]$What) { Enter-XournalMachineLock 'Global\XournalStudioBuild' 'build-lock' $What }
function Exit-XournalBuildLock($Mutex) { Exit-XournalMachineLock $Mutex }
function Enter-XournalSandboxLock([string]$What) { Enter-XournalMachineLock 'Global\XournalStudioSandbox' 'sandbox-lock' $What }
function Exit-XournalSandboxLock($Mutex) { Exit-XournalMachineLock $Mutex }

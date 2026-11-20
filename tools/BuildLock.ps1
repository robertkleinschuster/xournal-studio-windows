# Machine-wide build lock: one build at a time across all worktrees (build.ps1, install.ps1). A named mutex
# (Global\XournalStudioBuild): Windows releases it when its process ends, also on Ctrl+C or a crash; the same thread
# may enter it again (install.ps1 holds it while it runs the worktree's build.ps1). Who holds it is written to
# <dependency cache root>\build-lock-holder.txt, so a waiting build can say whom it waits for.
#   . (Join-Path $PSScriptRoot 'tools\BuildLock.ps1')
#   $lock = Enter-XournalBuildLock 'build.ps1 in D:\xsui'
#   try { ... } finally { Exit-XournalBuildLock $lock }

function Get-XournalBuildLockHolderFile {
    $root = $env:XOURNAL_DEPS_CACHE
    if (-not $root) { $root = Join-Path $env:LOCALAPPDATA 'XournalStudioBuildCache' }
    New-Item -ItemType Directory -Force $root | Out-Null
    Join-Path $root 'build-lock-holder.txt'
}

function Enter-XournalBuildLock([string]$What) {
    $holderFile = Get-XournalBuildLockHolderFile
    $mutex = New-Object System.Threading.Mutex($false, 'Global\XournalStudioBuild')
    $acquired = $false
    $shown = $null
    $started = Get-Date
    while (-not $acquired) {
        # Waits on the mutex itself (no sleep); the 2 s timeout only lets Ctrl+C through and the holder be reported.
        try { $acquired = $mutex.WaitOne(2000) }
        catch [System.Threading.AbandonedMutexException] {
            $acquired = $true # the mutex is ours; its previous holder ended without releasing it
            Write-Host "build lock: the previous holder ended without releasing it ($($_.Exception.Message))"
        }
        if (-not $acquired) {
            $holder = 'unknown holder'
            if (Test-Path $holderFile) {
                try { $holder = (Get-Content -Raw $holderFile).Trim() }
                catch { $holder = "unknown holder ($holderFile not readable: $($_.Exception.Message))" }
            }
            if ($holder -ne $shown) { Write-Host "build lock: waiting for $holder"; $shown = $holder }
        }
    }
    if ($shown) { Write-Host ("build lock: acquired after {0:n0} s" -f ((Get-Date) - $started).TotalSeconds) }
    Set-Content -Encoding utf8 $holderFile "$What (PID $PID, since $(Get-Date -Format s))"
    $mutex
}

function Exit-XournalBuildLock($Mutex) {
    $Mutex.ReleaseMutex()
    $Mutex.Dispose()
}

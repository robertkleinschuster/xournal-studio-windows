# Start time (Run-SandboxTest.ps1 -Test this): starts the app three times in a fresh sandbox - the first a cold start -
# with a project folder (the examples), the explorer open and a document with a PDF background, and records when the
# window appeared (seen from outside) and the app's own "start:" log lines, page views and PDF renders per start.
$ErrorActionPreference = 'Stop'
$root = 'C:\sbx'; $out = "$root\out"
New-Item -ItemType Directory -Force $out | Out-Null
Start-Transcript "$out\transcript.txt" | Out-Null
try {
    $work = 'C:\work'; New-Item -ItemType Directory -Force "$work\cfg" | Out-Null
    Copy-Item "$root\data\*" $work -Force
    Set-Content "$work\cfg\settings.json" '{"ExplorerOpen":true,"ProjectFolder":"C:\\work"}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$work\cfg"
    for ($run = 1; $run -le 3; $run++) {
        $env:XOURNAL_STUDIO_LOG = "$out\start$run.log"
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process "$root\app\XournalStudio.exe" -ArgumentList "`"$work\test.xopp`"" -PassThru
        while ($watch.Elapsed.TotalSeconds -lt 60) {
            Start-Sleep -Milliseconds 50
            $p.Refresh()
            if ($p.MainWindowHandle -ne 0) { break }
        }
        "run ${run}: window handle after $($watch.ElapsedMilliseconds) ms"
        Start-Sleep 10 # pages, PDF rendering, explorer
        "exited ${run}: $($p.HasExited)"
        $p.Kill(); $p.WaitForExit()
        Start-Sleep 3
        Get-Content "$out\start$run.log" | Select-String -Pattern '^\S+ (start:|page \d+: view created|pdf page|explorer: tree rebuilt|position:)' |
            ForEach-Object { "  $_" }
    }
}
catch {
    "ERROR: $_"
}
finally {
    Stop-Transcript | Out-Null
    Set-Content "$out\done.txt" 'done'
}

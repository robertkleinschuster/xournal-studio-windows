# Crash recovery in Windows Sandbox: a change, the copy written 5 s later (Recovery-native), the process killed; the next
# start offers the copy (Wiederherstellen), the restored document is changed (unsaved); closing it with "Nicht speichern"
# removes the copy. A second crash's copy is discarded with Verwerfen (to the recycle bin).
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\RecoveryTest.ps1
. C:\sbx\TestKit.ps1

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function Stroke($x0, $y0, $x1, $y1) {
    Hover $x0 $y0
    [Inj]::PenFrame($x0, $y0, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    $n = [Math]::Max(1, [int]([Math]::Sqrt(($x1 - $x0) * ($x1 - $x0) + ($y1 - $y0) * ($y1 - $y0)) / 4))
    for ($s = 1; $s -le $n; $s++) {
        [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, 0, 600)
    }
    [Inj]::PenFrame($x1, $y1, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x1, $y1, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 700
    "stroke $x0,$y0 -> $x1,$y1"
}
function Title { $script:p.Refresh(); $script:p.MainWindowTitle }
function Copies { @(Get-ChildItem "$out\config\Recovery-native" -Filter *.json -ErrorAction SilentlyContinue).Count }
function Crash { $script:p.Kill(); $script:p.WaitForExit(); Start-Sleep 1; "killed the app" }
# Starts the app without waiting for its main window to be ready for input (the recovery question comes first).
function StartWithQuestion {
    $script:p = Start-Process 'C:\sbx\app\XournalStudio.exe' -PassThru
    for ($i = 0; $i -lt 40 -and (Find-Anywhere 'Wiederherstellen') -eq $null; $i++) { Start-Sleep -Milliseconds 500 }
}
function Find-Anywhere($name) {
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
    [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}

Invoke-Test {
    New-Item -ItemType Directory -Force "$out\config" | Out-Null
    Set-Content "$out\config\settings-native.json" '{"paneOpen":false}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"

    # 1. A change in a new document; 5 s later the copy is there; the app is killed.
    Start-TestApp 'XournalStudio.exe'
    Stroke 200 300 500 330
    Start-Sleep 7
    "copies after the change: $(Copies)"
    if ((Copies) -ne 1) { throw 'no recovery copy 5 s after the change' }
    Crash

    # 2. The next start offers it; restored, the document is changed (unsaved).
    StartWithQuestion
    Shot '01-offer'
    ClickAnywhere 'Wiederherstellen' | Out-Null
    Start-Sleep 4
    Shot '02-restored'
    "title after restore: $(Title); copies: $(Copies)"
    if ((Title) -notlike '•*') { throw "the restored document must be changed (title: $(Title))" }
    if ((Copies) -ne 1) { throw 'the restored copy must be this session''s copy now (one copy)' }

    # 3. Closing with "Nicht speichern" removes the copy.
    $script:p.CloseMainWindow() | Out-Null
    Start-Sleep 1
    ClickAnywhere 'Nicht speichern' | Out-Null
    if (-not $script:p.WaitForExit(8000)) { throw 'the app did not close' }
    "copies after Nicht speichern: $(Copies)"
    if ((Copies) -ne 0) { throw 'Nicht speichern must remove the copy' }

    # 4. Another crash, then Verwerfen: the copy goes to the recycle bin, a new document opens.
    Start-TestApp 'XournalStudio.exe'
    Stroke 200 300 500 330
    Start-Sleep 7
    Crash
    StartWithQuestion
    ClickAnywhere 'Verwerfen' | Out-Null
    Start-Sleep 4
    "copies after Verwerfen: $(Copies); title: $(Title)"
    if ((Copies) -ne 0) { throw 'Verwerfen must remove the copy' }
    Shot '03-discarded'

    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

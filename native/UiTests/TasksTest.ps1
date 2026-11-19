# The Aufgaben tab in Windows Sandbox: the todo.txt shown (open by due date), a new task added with Enter, a task
# completed with its check box (only its line changes, the file keeps CRLF), a task deleted from the context menu.
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\TasksTest.ps1 -Extra D:\src\InkPages\.local-build\tools\uiaprobe.exe
. C:\sbx\TestKit.ps1

function Lines { [IO.File]::ReadAllText('C:\work\todo.txt') }
function WaitFor($what, [scriptblock]$condition) {
    for ($i = 0; $i -lt 20 -and -not (& $condition); $i++) { Start-Sleep -Milliseconds 500 }
    if (-not (& $condition)) { Shot "failed-$what"; throw "timed out: $what" }
}
# comctl32 controls and Win32 menus through the native UI Automation client (InkPages' uiaprobe, in C:\sbx\data): the
# managed client of Windows PowerShell does not see their items. ASCII parts of names (the probe matches substrings).
function At($class, $text) {
    for ($i = 0; $i -lt 10; $i++) {
        $r = & C:\sbx\data\uiaprobe.exe $class $text
        if ($LASTEXITCODE -eq 0) { $p = "$r".Trim() -split ' '; return @([int]$p[0], [int]$p[1]) }
        Start-Sleep -Milliseconds 300
    }
    Shot ("missing-" + ($text -replace '[^A-Za-z0-9]', '_')) | Out-Null
    throw "'$text' not found in a $class window"
}
function ClickAt($class, $text) { $a = At $class $text; [Inj]::ClickPixel($a[0], $a[1]); Start-Sleep -Milliseconds 700; "click $text" }
function RightClickAt($class, $text) { $a = At $class $text; [Inj]::RightClickPixel($a[0], $a[1]); Start-Sleep -Milliseconds 700; "right-click $text" }

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    [IO.File]::WriteAllText('C:\work\todo.txt', "2026-11-15 Später fällig due:2099-01-02`r`n2026-11-15 Bald fällig due:2026-01-01`r`n2026-11-15 Ohne Datum`r`n")
    New-Item -ItemType Directory -Force "$out\config" | Out-Null
    Set-Content "$out\config\settings-native.json" '{"paneOpen":true,"paneTab":1,"projectFolder":"C:\\work"}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"

    Start-TestApp 'XournalStudio.exe'
    Start-Sleep 2
    Shot '01-tasks'
    foreach ($title in 'Bald f', 'ter f', 'Ohne Datum') { At 'XournalStudioWindow' $title | Out-Null }

    # 1. A new task: typed into the box, Enter adds it at the end of the file.
    # The new task's box lies right below the header "todo.txt - n offen" (27 DIP below its center).
    $header = At 'XournalStudioWindow' 'offen'
    [Inj]::ClickPixel($header[0], $header[1] + [int](1.5 * 27)); Start-Sleep -Milliseconds 400
    [System.Windows.Forms.SendKeys]::SendWait('Neue Übung abgeben'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'add' { (Lines) -match 'Neue Übung abgeben' }
    "after add: $(Lines)"
    Shot '02-added'

    # 2. Complete a task with its check box: its line only, CRLF kept.
    ClickAt 'XournalStudioWindow' 'Ohne Datum' | Out-Null
    [Inj]::Key(0x20)   # Space toggles the selected row's check box
    WaitFor 'complete' { (Lines) -match '(?m)^x \d{4}-\d\d-\d\d 2026-11-15 Ohne Datum' }
    "after complete: $(Lines)"
    if ((Lines) -notmatch "Bald fällig due:2026-01-01`r`n") { throw 'the other lines changed' }
    Shot '03-completed'

    # 3. Delete a task from its context menu (asked first).
    RightClickAt 'XournalStudioWindow' 'ter f' | Out-Null
    ClickAt '#32768' 'schen' | Out-Null
    Start-Sleep -Milliseconds 800
    Shot '04-delete-question'
    ClickAnywhere 'Löschen' | Out-Null
    WaitFor 'delete' { (Lines) -notmatch 'Später fällig' }
    "after delete: $(Lines)"
    Shot '05-deleted'

    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

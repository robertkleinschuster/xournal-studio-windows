# The Aufgaben tab in Windows Sandbox: the todo.txt shown (open by due date), a new task added with Enter, a task
# completed with its check box (only its line changes, the file keeps CRLF), a task deleted from the context menu.
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\TasksTest.ps1
. C:\sbx\TestKit.ps1

function Lines { [IO.File]::ReadAllText('C:\work\todo.txt') }
function WaitFor($what, [scriptblock]$condition) {
    for ($i = 0; $i -lt 20 -and -not (& $condition); $i++) { Start-Sleep -Milliseconds 500 }
    if (-not (& $condition)) { Shot "failed-$what"; throw "timed out: $what" }
}
function ListItem($name) {
    $cond = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)),
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)))
    $script:window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}

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
    foreach ($title in 'Bald fällig', 'Später fällig', 'Ohne Datum') {
        if ((ListItem $title) -eq $null) { throw "task '$title' not shown" }
    }

    # 1. A new task: typed into the box, Enter adds it at the end of the file.
    $box = Find 'Neue Aufgabe (Eingabe fügt hinzu)'
    if ($box -eq $null) {
        $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Edit)
        $box = $script:window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    }
    $r = $box.Current.BoundingRectangle
    [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 400
    [System.Windows.Forms.SendKeys]::SendWait('Neue Übung abgeben'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'add' { (Lines) -match 'Neue Übung abgeben' }
    "after add: $(Lines)"
    Shot '02-added'

    # 2. Complete a task with its check box: its line only, CRLF kept.
    $item = ListItem 'Ohne Datum'
    $item.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle()
    WaitFor 'complete' { (Lines) -match '(?m)^x \d{4}-\d\d-\d\d 2026-11-15 Ohne Datum' }
    "after complete: $(Lines)"
    if ((Lines) -notmatch "Bald fällig due:2026-01-01`r`n") { throw 'the other lines changed' }
    Shot '03-completed'

    # 3. Delete a task from its context menu (asked first).
    RightClick 'Später fällig' | Out-Null
    ClickAnywhere 'Löschen' | Out-Null
    Start-Sleep -Milliseconds 800
    Shot '04-delete-question'
    ClickAnywhere 'Löschen' | Out-Null
    WaitFor 'delete' { (Lines) -notmatch 'Später fällig' }
    "after delete: $(Lines)"
    Shot '05-deleted'

    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

# The Aufgaben tab in Windows Sandbox: the todo.txt shown (groups Überfällig / Offen, open by due date), a new task
# added with Enter and an optional date (the date picker's check box), a task completed with its check box (only its line changes,
# the file keeps CRLF), a task's text edited in place (F2), a task edited with the dialog (context menu; a double-click
# opens it too), a task deleted from the context menu.
#   UiTests\Run-AppTest.ps1 -Test UiTests\TasksTest.ps1 -Extra D:\src\InkPages\.local-build\tools\uiaprobe.exe
. C:\sbx\TestKit.ps1

# The app may be writing the file just then (a sharing violation, seen on the GitHub runner): read again shortly after,
# logged; the error is thrown when it stays busy.
function Lines {
    for ($try = 1; ; $try++) {
        try { return [IO.File]::ReadAllText('C:\work\todo.txt') }
        catch [IO.IOException] {
            if ($try -ge 20) { throw }
            Write-Host "todo.txt busy (try $try): $($_.Exception.Message)"
            Start-Sleep -Milliseconds 100
        }
    }
}
function WaitFor($what, [scriptblock]$condition) {
    for ($i = 0; $i -lt 20 -and -not (& $condition); $i++) { Start-Sleep -Milliseconds 500 }
    if (-not (& $condition)) { Shot "failed-$what"; throw "timed out: $what - todo.txt: $(Lines)" }
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
    # The groups (Überfällig (1), Offen (2)) as UI Automation reports them: logged (their names come from comctl32).
    foreach ($group in 'Offen (', 'berf') {
        $r = & C:\sbx\data\uiaprobe.exe XournalStudioWindow $group
        "group '$group': $(if ($LASTEXITCODE -eq 0) { "at $r" } else { 'not reported' })"
    }
    & C:\sbx\data\uiaprobe.exe XournalStudioWindow | Out-File "$out\uia-tree.txt" -Encoding utf8

    # 1. Enable the optional date using the native check box at the start of the date picker (id 21).
    $pickerId = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, '21')
    $picker = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $pickerId)
    if ($null -eq $picker) { throw 'the date picker (id 21) was not found' }
    $dateBounds = $picker.Current.BoundingRectangle
    [Inj]::ClickPixel([int]($dateBounds.X + $dateBounds.Height / 2), [int]($dateBounds.Y + $dateBounds.Height / 2))
    Shot '01b-due-date-row'
    # The box by its control id (20, UI Automation's id of a Win32 control; it has no name, only a cue banner).
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, '20')
    $box = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if ($box -eq $null) { Shot 'missing-new-task-box' | Out-Null; throw 'the new task box (id 20) not found' }
    $r = $box.Current.BoundingRectangle
    [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 400
    [Inj]::TypeText('Neue Übung abgeben'); Start-Sleep -Milliseconds 300
    [Inj]::Key(0x0D)
    WaitFor 'add' { (Lines) -match 'Neue Übung abgeben' }
    $today = Get-Date -Format 'yyyy-MM-dd'
    if ((Lines) -notmatch "Neue Übung abgeben due:$today") { throw "the new task has no due:$today" }
    "after add: $(Lines)"
    Shot '02-added'

    # 2. Complete a task with its check box: its line only, CRLF kept.
    ClickAt 'XournalStudioWindow' 'Ohne Datum' | Out-Null
    [Inj]::Key(0x20)   # Space toggles the selected row's check box
    WaitFor 'complete' { (Lines) -match '(?m)^x \d{4}-\d\d-\d\d 2026-11-15 Ohne Datum' }
    "after complete: $(Lines)"
    if ((Lines) -notmatch "Bald fällig due:2026-01-01`r`n") { throw 'the other lines changed' }
    Shot '03-completed'

    # 2b. Edit a task's text in place (F2): the new text, its created date kept.
    ClickAt 'XournalStudioWindow' 'Neue' | Out-Null
    [Inj]::Key(0x71); Start-Sleep -Milliseconds 600   # F2
    Shot '03-editing-in-place'
    # Only type into the edit box: typed into the list, the text would select rows (type-ahead) and a space toggle one.
    $focus = "$(& C:\sbx\data\uiaprobe.exe --focus)"
    "focus after F2: $focus"
    if ($focus -notmatch 'Edit') { throw 'F2 opened no edit box' }
    [System.Windows.Forms.SendKeys]::SendWait('^a'); Start-Sleep -Milliseconds 200
    [System.Windows.Forms.SendKeys]::SendWait('Blatt 4 abgeben {+}ET'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'edit in place' { (Lines) -match "(?m)^\d{4}-\d\d-\d\d Blatt 4 abgeben \+ET due:$today\r$" }
    "after the edit in place: $(Lines)"

    # 3. Edit a task (Bearbeiten: the text without its due: tag, the date in the picker): a new text, the date kept.
    RightClickAt 'XournalStudioWindow' 'Bald f' | Out-Null
    ClickAt '#32768' 'Bearbeiten' | Out-Null
    Start-Sleep -Milliseconds 800
    Shot '04-edit-dialog'
    [System.Windows.Forms.SendKeys]::SendWait('^a'); Start-Sleep -Milliseconds 200
    [System.Windows.Forms.SendKeys]::SendWait('Bald erledigt {+}GET'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'edit' { (Lines) -match '(?m)^2026-11-15 Bald erledigt \+GET due:2026-01-01\r$' }
    "after edit: $(Lines)"

    # 3b. A double-click on a row opens the same dialog (Abbrechen: nothing changes).
    $before = Lines
    $row = At 'XournalStudioWindow' 'Bald erledigt'
    [Inj]::ClickPixel($row[0], $row[1]); Start-Sleep -Milliseconds 80; [Inj]::ClickPixel($row[0], $row[1])
    Start-Sleep -Milliseconds 800
    At '#32770' 'Speichern' | Out-Null   # the dialog "Aufgabe bearbeiten"
    Shot '04b-double-click-dialog'
    [Inj]::Key(0x1B); Start-Sleep -Milliseconds 600   # Escape = Abbrechen
    if ((Lines) -ne $before) { throw 'cancelling the dialog changed todo.txt' }
    "double-click opened the dialog"

    # 4. Delete a task from its context menu (asked first).
    RightClickAt 'XournalStudioWindow' 'ter f' | Out-Null
    ClickAt '#32768' 'schen' | Out-Null
    Start-Sleep -Milliseconds 800
    Shot '05-delete-question'
    ClickAnywhere 'Löschen' | Out-Null
    WaitFor 'delete' { (Lines) -notmatch 'Später fällig' }
    "after delete: $(Lines)"
    Shot '06-deleted'

    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

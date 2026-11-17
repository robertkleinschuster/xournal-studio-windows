# Runs inside Windows Sandbox (see Run-SandboxTest.ps1): drives the app with synthetic pen/touch input,
# finds toolbar buttons by UI Automation, takes a screenshot per step and copies the saved files.
$ErrorActionPreference = 'Stop'
$root = 'C:\sbx'; $out = "$root\out"
New-Item -ItemType Directory -Force $out | Out-Null
Start-Transcript "$out\transcript.txt" | Out-Null
try {
    Add-Type -Path "$root\Inj.cs" -ReferencedAssemblies System.Drawing
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
    Get-Process XournalStudio -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 1
    $work = 'C:\work'; New-Item -ItemType Directory -Force $work | Out-Null
    Copy-Item "$root\data\*" $work -Force
    # Explorer closed: the test's coordinates assume the page starts at the left edge.
    New-Item -ItemType Directory -Force "$work\cfg" | Out-Null
    Set-Content "$work\cfg\settings.json" '{"ExplorerOpen":false,"ProjectFolder":"C:\\work"}'
    $d = Get-Date; $f = { param($days) $d.AddDays($days).ToString('yyyy-MM-dd') }
    Set-Content "$work\todo.txt" -Encoding UTF8 @(
        "$(& $f -10) GET HÜ Abgabe due:$(& $f -1) time:18:00",
        "$(& $f -3) Trigonometrie wiederholen",
        "$(& $f -2) Mathe Altklausur due:$(& $f 0) doc:test.xopp#p1",
        "x $(& $f -1) $(& $f -5) Physik Skript lesen")
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$work\cfg"; $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    $p = Start-Process "$root\app\XournalStudio.exe" -ArgumentList "`"$work\test.xopp`"" -PassThru
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break } }
    if ($p.MainWindowHandle -eq 0) { throw "no window (exited: $($p.HasExited))" }
    Start-Sleep 6
    $h = $p.MainWindowHandle
    [Inj]::SetForegroundWindow($h) | Out-Null
    "init: " + [Inj]::Init($h)
    $W = [Inj]::W
    $window = [System.Windows.Automation.AutomationElement]::FromHandle($h)

    function Shot($name) { Start-Sleep -Milliseconds 900; [Inj]::Shot("$out\$name.png"); if ($p.HasExited) { throw "app exited after $name" }; "shot $name" }
    function L { [double[]]$args }
    function X($f) { [double]($f * $W) }
    function Find($name) {
        foreach ($prop in [System.Windows.Automation.AutomationElement]::AutomationIdProperty, [System.Windows.Automation.AutomationElement]::NameProperty) {
            $cond = New-Object System.Windows.Automation.PropertyCondition($prop, $name)
            $e = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
            if ($e -ne $null) { return $e }
        }
        $null
    }
    # Clicks the first button found by automation id or name (the InkToolbar's own buttons have localized names).
    function Button {
        $e = $null
        foreach ($name in $args) { if ($e -eq $null) { $e = Find $name } }
        if ($e -eq $null) { throw "button '$args' not found" }
        $r = $e.Current.BoundingRectangle
        [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
        Start-Sleep -Milliseconds 400
        "click $args"
    }
    # Flyout content lives in its own popup window: search from the desktop.
    function ClickAnywhere($name) {
        $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
        $e = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e -eq $null) { throw "'$name' not found" }
        $r = $e.Current.BoundingRectangle
        [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 500
        "click $name"
    }
    function Check($name) {
        $cond = New-Object System.Windows.Automation.AndCondition(
            (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)),
            (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::CheckBox)))
        $e = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e -eq $null) { throw "checkbox '$name' not found" }
        $r = $e.Current.BoundingRectangle
        [Inj]::ClickPixel([int]($r.X + 12), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 500
        "check $name"
    }
    function Focus { [Inj]::Click((X 0.6), 20); Start-Sleep -Milliseconds 300 }  # menu bar background

    Shot '01-start'

    # The tools themselves (erasers, eraser end, barrel lasso and move, selection, point eraser, shapes, recognition,
    # dragging across pages) are the library's: InkPages.Demo\UiTests\ToolsTest.ps1 tests them in the demo app. Here: what
    # the app adds - .xopp/PDF content, clipboard, tasks, saving and recovery - plus one pen stroke on the PDF page.
    [Inj]::PenStroke((L (X 0.3) 250 (X 0.6) 250), $false, 512)
    Shot '02-pen-inks'

    # Rectangle on the PDF page copies that area (Ctrl+C); pasted on a new graph page it is selected: scale it at its
    # corner, duplicate it, tap the original to select it, delete it, undo.
    Button 'SelectButton' 'Auswählen'
    Button 'SelectButton' 'Auswählen'   # second tap: the tool's options
    ClickAnywhere 'Rechteck'
    Focus
    [Inj]::PenStroke((L (X 0.12) 280 (X 0.62) 375), $false, 512)
    Shot '05-area-selected'
    Focus; [Inj]::Ctrl(0x43); Start-Sleep 1
    Button 'InsertPageButton' 'Seite einfügen'; Start-Sleep 1
    Focus; [Inj]::Ctrl(0x56); Start-Sleep 2
    Shot '06-area-pasted-selected'
    [Inj]::PenStroke((L 561 218 700 245), $false, 512)
    Shot '07-area-scaled'
    Focus; [Inj]::Ctrl(0x44); Start-Sleep 1
    Shot '08-duplicated'
    [Inj]::PenStroke((L 600 125 600.5 125.5), $false, 512)   # on the original, away from the copy and its commands
    Shot '09-tap-selected'
    Focus; [Inj]::Key(0x2E); Shot '10-deleted'
    Focus; [Inj]::Ctrl(0x5A); Shot '11-delete-undone'

    # A bitmap on the clipboard (as from the Snipping Tool) pastes below what is on the page, selected.
    $bmp = New-Object System.Drawing.Bitmap 600, 120
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::White)
    $g.DrawString('Beispiel 3: Ausschnitt aus der Angabe', (New-Object System.Drawing.Font 'Segoe UI', 24), [System.Drawing.Brushes]::Black, 10, 30)
    [System.Windows.Forms.Clipboard]::SetImage($bmp)
    Focus; [Inj]::Ctrl(0x56); Start-Sleep 2
    Shot '12-bitmap-pasted'

    # Tasks (todo.txt of the project folder): add one with Enter, the text box keeps its keys (Ctrl+A, Del do not touch
    # the selection on the page), check one off.
    Focus; [Inj]::Ctrl(0x42); Start-Sleep 1
    Button 'TasksTab' 'Aufgaben'
    Shot 'T1-tasks'
    Button 'NewTask'
    [System.Windows.Forms.SendKeys]::SendWait('xyz'); [Inj]::Ctrl(0x41); [Inj]::Key(0x2E); Start-Sleep -Milliseconds 500
    [System.Windows.Forms.SendKeys]::SendWait('Übung 4 rechnen{ENTER}'); Start-Sleep 1
    Shot 'T2-added-selection-kept'
    Check 'Trigonometrie wiederholen'
    Shot 'T3-checked'
    Copy-Item "$work\todo.txt" "$out\todo.txt"
    Focus; [Inj]::Ctrl(0x42); Start-Sleep 1

    # Closing with changes asks (Abbrechen keeps the window), then Ctrl+S saves and closing does not ask.
    [Inj]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep 2
    Shot '13-close-asks'
    Button 'Abbrechen'
    Focus
    [Inj]::Ctrl(0x53); Start-Sleep 2; Shot '14-saved'
    Copy-Item "$work\test.xopp" "$out\saved1.xopp"
    [Inj]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $p.WaitForExit(15000) | Out-Null
    "exited: $($p.HasExited)"
    Copy-Item "$work\test.xopp" "$out\saved2.xopp"

    # Crash with unsaved changes: the next start offers the recovery copy; restored, the stroke is back (unsaved).
    $p = Start-Process "$root\app\XournalStudio.exe" -ArgumentList "`"$work\test.xopp`"" -PassThru
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break } }
    Start-Sleep 6; $h = $p.MainWindowHandle; [Inj]::SetForegroundWindow($h) | Out-Null; "init: " + [Inj]::Init($h)
    $window = [System.Windows.Automation.AutomationElement]::FromHandle($h)
    [Inj]::PenStroke((L (X 0.2) 200 (X 0.5) 260), $false, 512); Start-Sleep 7
    "recovery files: " + ((Get-ChildItem "$work\cfg\Recovery" -ErrorAction SilentlyContinue | ForEach-Object Name) -join ', ')
    Stop-Process -Id $p.Id -Force; Start-Sleep 2
    $p = Start-Process "$root\app\XournalStudio.exe" -ArgumentList "`"$work\test.xopp`"" -PassThru
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break } }
    Start-Sleep 6; $h = $p.MainWindowHandle; [Inj]::SetForegroundWindow($h) | Out-Null
    $window = [System.Windows.Automation.AutomationElement]::FromHandle($h)
    Shot '15-recovery-offered'
    Button 'Wiederherstellen'; Start-Sleep 2
    Shot '16-recovered'
    [Inj]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep 2
    Shot '17-close-asks-again'
    Button 'Nicht speichern'
    $p.WaitForExit(15000) | Out-Null
    "exited: $($p.HasExited)"
    "recovery files after: " + ((Get-ChildItem "$work\cfg\Recovery" -ErrorAction SilentlyContinue | ForEach-Object Name) -join ', ')
}
catch { "ERROR: $_"; $_.ScriptStackTrace; try { [Inj]::Shot("$out\error.png") } catch { "error screenshot failed: $_" } }
finally { Stop-Transcript | Out-Null; Set-Content "$out\done.txt" 'done' }

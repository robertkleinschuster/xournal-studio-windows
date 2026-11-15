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

    # Native eraser (toolbar) and the pen's eraser end: each removes the stroke it touches.
    [Inj]::PenStroke((L (X 0.3) 250 (X 0.6) 250), $false, 512)
    Button 'InkToolbarEraserButton' 'Radierer'
    [Inj]::PenStroke((L (X 0.45) 235 (X 0.45) 265), $false, 512)
    Shot '02-eraser-tool'
    Button 'InkToolbarBallpointPenButton'
    [Inj]::PenStroke((L (X 0.3) 290 (X 0.6) 290), $false, 512)
    [Inj]::PenStroke((L (X 0.4) 275 (X 0.4) 305), $true, 512)
    Shot '03-eraser-end'

    # Pen's barrel button while the pen is active (no tool switch): a barrel drag lassos, a barrel drag inside the
    # selection moves it, the pen still inks, Del deletes the selection.
    [Inj]::PenStroke((L (X 0.7) 250 (X 0.85) 250), $false, 512)
    [Inj]::PenStrokeFlags((L (X 0.68) 232 (X 0.87) 232 (X 0.87) 268 (X 0.68) 268 (X 0.68) 232), 1, 512)
    Shot '03b-barrel-lasso'
    [Inj]::PenStrokeFlags((L (X 0.77) 250 (X 0.77) 300), 1, 512)
    Shot '03c-barrel-move'
    [Inj]::PenStroke((L (X 0.7) 350 (X 0.85) 350), $false, 512)
    Shot '03d-pen-still-inks'
    Focus; [Inj]::Key(0x2E); Shot '03e-selection-deleted'

    # Selection tool (lasso): a stroke selected shows its commands; the corner handle scales it, Ctrl+Z undoes that.
    [Inj]::PenStroke((L (X 0.6) 430 (X 0.8) 430), $false, 512)
    Button 'SelectButton' 'Auswählen'
    [Inj]::PenStroke((L (X 0.58) 415 (X 0.82) 415 (X 0.82) 445 (X 0.58) 445 (X 0.58) 415), $false, 512)
    Shot '03f-selected-with-commands'
    [Inj]::PenStroke((L ((X 0.8) + 8) 438 ((X 0.8) + 110) 445), $false, 512)
    Shot '03g-scaled'
    # Strokes alone go through Windows ink's clipboard: the scaled size must come along (PointTransform).
    Focus; [Inj]::Ctrl(0x43); [Inj]::Ctrl(0x56); Start-Sleep 1; Shot '03g2-scaled-copy-pasted'
    Focus; [Inj]::Ctrl(0x5A); Start-Sleep 1
    Focus; [Inj]::Ctrl(0x5A); Shot '03h-scale-undone'

    # Partial eraser (toggle) with the eraser tool: a line is cut exactly where the circle is; the circle shows while
    # hovering (shot at once: an injected pen leaves range ~0.6 s after its last frame). The eraser end still erases
    # whole strokes (WinUI ignores IsEraserInputEnabled = false, see ToolController).
    Button 'InkToolbarBallpointPenButton'
    Button 'PartialEraserButton' 'Teilweise radieren'
    [Inj]::PenStroke((L (X 0.3) 480 (X 0.6) 480), $false, 512)
    Button 'InkToolbarEraserButton' 'Radierer'
    [Inj]::PenStroke((L (X 0.45) 465 (X 0.45) 495), $false, 512)
    for ($k = 0; $k -lt 6; $k++) { [Inj]::PenFrame((X 0.5) + 3 * $k, 480, 0x22002, 0, 0) }
    [Inj]::Shot("$out\03i-partial-erased-circle.png"); "shot 03i-partial-erased-circle"
    [Inj]::PenFrame((X 0.52), 480, 0x22000, 0, 0)
    Focus; [Inj]::Ctrl(0x5A); Shot '03j-partial-undone'
    Button 'PartialEraserButton' 'Teilweise radieren'
    Button 'SelectButton' 'Auswählen'

    # Shape recognition (Windows ink analysis): a hand-drawn rectangle and circle become clean shapes.
    Button 'InkToolbarBallpointPenButton'
    Button 'RecognizeButton' 'Formerkennung'
    [Inj]::PenStroke((L (X 0.32) 330 (X 0.5) 332 (X 0.499) 400 (X 0.321) 398 (X 0.32) 331), $false, 512)
    $c = @(); for ($k = 0; $k -le 36; $k++) { $t = 2 * [Math]::PI * $k / 36; $c += (X 0.65) + 45 * [Math]::Cos($t) + 2 * [Math]::Sin(5 * $t); $c += 365 + 40 * [Math]::Sin($t) }
    [Inj]::PenStroke([double[]]$c, $false, 512); Start-Sleep 2
    Shot '04-recognized-shapes'
    Button 'RecognizeButton' 'Formerkennung'

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
catch { "ERROR: $_"; $_.ScriptStackTrace; try { [Inj]::Shot("$out\error.png") } catch {} }
finally { Stop-Transcript | Out-Null; Set-Content "$out\done.txt" 'done' }

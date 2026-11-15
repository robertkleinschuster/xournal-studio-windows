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
    Set-Content "$work\cfg\settings.json" '{"ExplorerOpen":false}'
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

    # Shape recognition (Windows ink analysis): a hand-drawn rectangle and circle become clean shapes.
    Button 'RecognizeButton' 'Formerkennung'
    [Inj]::PenStroke((L (X 0.32) 330 (X 0.5) 332 (X 0.499) 400 (X 0.321) 398 (X 0.32) 331), $false, 512)
    $c = @(); for ($k = 0; $k -le 36; $k++) { $t = 2 * [Math]::PI * $k / 36; $c += (X 0.65) + 45 * [Math]::Cos($t) + 2 * [Math]::Sin(5 * $t); $c += 365 + 40 * [Math]::Sin($t) }
    [Inj]::PenStroke([double[]]$c, $false, 512); Start-Sleep 2
    Shot '04-recognized-shapes'
    Button 'RecognizeButton' 'Formerkennung'

    # A bitmap on the clipboard (as from the Snipping Tool), pasted onto a new graph page.
    $bmp = New-Object System.Drawing.Bitmap 600, 120
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::White)
    $g.DrawString('Beispiel 3: Ausschnitt aus der Angabe', (New-Object System.Drawing.Font 'Segoe UI', 24), [System.Drawing.Brushes]::Black, 10, 30)
    [System.Windows.Forms.Clipboard]::SetImage($bmp)
    Button 'InsertPageButton' 'Seite einfügen'
    for ($k = 0; $k -lt 5; $k++) { [Inj]::TouchDrag((X 0.85), 480, (X 0.85), 160, 30) }
    Shot '05-new-page'
    Focus
    [Inj]::Ctrl(0x56); Start-Sleep 2
    Shot '06-pasted'
    Button 'UndoButton' 'Rückgängig'; Shot '07-undo-paste'
    Button 'RedoButton' 'Wiederholen'; Shot '08-redo-paste'

    # Page 3 of the document (graph page with pictures: a teximage with a PDF and PNG images).
    for ($k = 0; $k -lt 4; $k++) { [Inj]::TouchDrag((X 0.85), 480, (X 0.85), 160, 30) }
    Shot '09-pictures'

    Focus
    [Inj]::Ctrl(0x53); Start-Sleep 2; Shot '10-saved'
    Copy-Item "$work\test.xopp" "$out\saved1.xopp"
    [Inj]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $p.WaitForExit(15000) | Out-Null
    "exited: $($p.HasExited)"
    Copy-Item "$work\test.xopp" "$out\saved2.xopp"
}
catch { "ERROR: $_"; $_.ScriptStackTrace; try { [Inj]::Shot("$out\error.png") } catch {} }
finally { Stop-Transcript | Out-Null; Set-Content "$out\done.txt" 'done' }

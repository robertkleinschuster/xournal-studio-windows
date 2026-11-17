# Experiment for Run-SandboxTest.ps1 -Test: a selection dragged with the barrel button to the bottom edge of the view and
# held there while the pages scroll (DragAutoScroll): the content must stay under the pen. Screenshots + app log.
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

    # POINTER_FLAGs / PEN_FLAGs (see Inj.cs)
    $INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $SECOND = 0x20; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
    $BARREL = 0x1
    $held = $INRANGE -bor $INCONTACT -bor $FIRST -bor $SECOND -bor $PRIMARY
    function Frame($x, $y, $flags) { [Inj]::PenFrame($x, $y, $flags, $BARREL, 512); Start-Sleep -Milliseconds 8 }
    function Hold($x, $y, $ms) { for ($t = 0; $t -lt $ms; $t += 16) { [Inj]::PenFrame($x, $y, $held -bor $UPDATE, $BARREL, 512); Start-Sleep -Milliseconds 16 } }

    Shot '01-start'
    # A stroke, then a barrel lasso around it (the pen tool stays active).
    [Inj]::PenStroke((L (X 0.4) 250 (X 0.6) 250), $false, 512)
    [Inj]::PenStrokeFlags((L (X 0.38) 232 (X 0.62) 232 (X 0.62) 268 (X 0.38) 268 (X 0.38) 232), $BARREL, 512)
    Shot '02-selected'

    # Barrel drag from the stroke down to the bottom edge of the view; held there while the pages scroll.
    $x = X 0.5; $y0 = 250; $edge = [Inj]::H - 12
    "pen goes to x=$x y=$edge (client DIPs; view height $([Inj]::H))"
    # A real pen hovers before it touches: over the selection the app takes it (moving is XAML pointer input).
    for ($t = 0; $t -lt 300; $t += 16) { [Inj]::PenFrame($x, $y0 - 1, $INRANGE -bor $UPDATE -bor $PRIMARY, $BARREL, 0); Start-Sleep -Milliseconds 16 }
    Frame $x $y0 ($held -bor $DOWN)
    for ($y = $y0; $y -le $edge; $y += 4) { Frame $x $y ($held -bor $UPDATE) }
    Hold $x $edge 1000
    [Inj]::Shot("$out\03-held-1s.png"); "shot 03-held-1s"
    Hold $x $edge 2000
    [Inj]::Shot("$out\04-held-3s.png"); "shot 04-held-3s"
    # Up out of the scrolling band, still pressed: the content must be right under the pen.
    for ($y = $edge; $y -ge $edge - 160; $y -= 4) { Frame $x $y ($held -bor $UPDATE) }
    Hold $x ($edge - 160) 600
    [Inj]::Shot("$out\05-moved-up.png"); "shot 05-moved-up (pen at y=$($edge - 160))"
    [Inj]::PenFrame($x, $edge - 160, $INRANGE -bor $UP -bor $PRIMARY, $BARREL, 0)
    [Inj]::PenFrame($x, $edge - 160, $UPDATE -bor $PRIMARY, 0, 0)
    Shot '06-released'
}
catch { "ERROR: $_"; $_.ScriptStackTrace; try { [Inj]::Shot("$out\error.png") } catch {} }
finally { Stop-Transcript | Out-Null; Set-Content "$out\done.txt" 'done' }

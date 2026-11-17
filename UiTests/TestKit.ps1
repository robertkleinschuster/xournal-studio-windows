# Helpers for scripts that run inside Windows Sandbox (dot-source: . C:\sbx\TestKit.ps1). Starts an app from
# C:\sbx\app, finds controls by UI Automation, injects pen input (Inj.cs) and takes screenshots into C:\sbx\out.
# PowerShell 5.1 in the sandbox: Add-Type compiles C# 5 only.
$ErrorActionPreference = 'Stop'
$root = 'C:\sbx'; $out = "$root\out"
New-Item -ItemType Directory -Force $out | Out-Null
Add-Type -Path "$root\Inj.cs" -ReferencedAssemblies System.Drawing
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing

# Starts the app (closing a running one), waits for its window and makes it the injection target.
function Start-TestApp([string]$exe, [string]$arguments = '') {
    Get-Process ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 1
    $env:INKPAGES_DEMO_LOG = "$out\demo.log"
    if ($arguments) { $script:p = Start-Process "$root\app\$exe" -ArgumentList $arguments -PassThru }
    else { $script:p = Start-Process "$root\app\$exe" -PassThru }
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $script:p.Refresh(); if ($script:p.MainWindowHandle -ne 0) { break } }
    if ($script:p.MainWindowHandle -eq 0) { throw "no window (exited: $($script:p.HasExited))" }
    Start-Sleep 5
    $script:h = $script:p.MainWindowHandle
    [Inj]::SetForegroundWindow($script:h) | Out-Null
    "init: " + [Inj]::Init($script:h)
    $script:W = [Inj]::W
    $script:window = [System.Windows.Automation.AutomationElement]::FromHandle($script:h)
}

function Stop-TestApp { if ($script:p -and -not $script:p.HasExited) { $script:p.Kill(); $script:p.WaitForExit() } }

function Shot($name) { Start-Sleep -Milliseconds 900; [Inj]::Shot("$out\$name.png"); if ($script:p.HasExited) { throw "app exited after $name" }; "shot $name" }
function L { [double[]]$args }
function X($f) { [double]($f * $script:W) }

function Find($name) {
    foreach ($prop in [System.Windows.Automation.AutomationElement]::AutomationIdProperty, [System.Windows.Automation.AutomationElement]::NameProperty) {
        $cond = New-Object System.Windows.Automation.PropertyCondition($prop, $name)
        $e = $script:window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e -ne $null) { return $e }
    }
    $null
}

# Clicks the first control found by automation id or name (the InkToolbar's own buttons have localized names).
function Button {
    $e = $null
    foreach ($name in $args) { if ($e -eq $null) { $e = Find $name } }
    if ($e -eq $null) { throw "button '$args' not found" }
    $r = $e.Current.BoundingRectangle
    [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
    Start-Sleep -Milliseconds 400
    # The mouse leaves the toolbar: a button's tooltip (a windowed popup) open over the window swallows the release of a
    # barrel drag (WinUI, sandbox 2026-11-17) - with the pen in use the mouse does not rest on the toolbar.
    [Inj]::MoveMouse(980, 500)
    "click $args"
}

# Right-clicks a control found by automation id or name (its context flyout opens).
function RightClick($name) {
    $e = Find $name
    if ($e -eq $null) { throw "'$name' not found" }
    $r = $e.Current.BoundingRectangle
    [Inj]::RightClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
    Start-Sleep -Milliseconds 600
    "right-click $name"
}

# Flyout content lives in its own popup window: searched from the desktop.
function ClickAnywhere($name) {
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
    $e = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if ($e -eq $null) { throw "'$name' not found" }
    $r = $e.Current.BoundingRectangle
    [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 500
    "click $name"
}

# Opens a tool's options (a tap on the active tool's button) and clicks an entry there. The flyout takes a moment to
# open: tries a few times before it fails.
function ClickInOptions($button, $name) {
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
    for ($try = 0; $try -lt 3; $try++) {
        Button $button | Out-Null
        for ($wait = 0; $wait -lt 10; $wait++) {
            Start-Sleep -Milliseconds 200
            $e = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
            if ($e -ne $null) {
                $r = $e.Current.BoundingRectangle
                [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 500
                return "click $button > $name"
            }
        }
    }
    throw "'$name' not found in the options of $button"
}

# Closes an open flyout (Escape).
function Escape { [Inj]::Key(0x1B); Start-Sleep -Milliseconds 400 }

# Runs the test body; writes done.txt (the host waits for it) and any error.
function Invoke-Test([scriptblock]$body) {
    Start-Transcript "$out\transcript.txt" | Out-Null
    try { & $body }
    catch { "ERROR: $_"; $_.ScriptStackTrace }
    finally { Stop-TestApp; Stop-Transcript | Out-Null; Set-Content "$out\done.txt" 'done' }
}

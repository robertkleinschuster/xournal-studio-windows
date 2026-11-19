# The message toast (a windowed popup over the pages) must not disturb pen gestures: while it is open, a stroke is
# drawn, lassoed with the barrel button and moved with a barrel drag – once beside the toast and once ending over it.
# Run-SandboxTest.ps1 -Test this (no document: a new graph page). The log (out\app.log) shows the gestures and undo steps.
. C:\sbx\TestKit.ps1

function Alive($step) { if ($script:p.HasExited) { throw "app exited after $step (exit code $($script:p.ExitCode))" }; "alive after $step" }
function HoverAt($x, $y, $ms = 120) { for ($t = 0; $t -lt $ms; $t += 16) { [Inj]::PenFrame($x, $y, 0x22002, 1, 0); Start-Sleep -Milliseconds 16 } }
function Toast { [Inj]::Click(700, 20); Start-Sleep -Milliseconds 300; [Inj]::Ctrl(0x56); Start-Sleep 1 } # Ctrl+V, empty clipboard: a message

Invoke-Test {
    New-Item -ItemType Directory -Force 'C:\cfg' | Out-Null
    Set-Content 'C:\cfg\settings.json' '{"ExplorerOpen":false}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = 'C:\cfg'; $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe'
    Start-Sleep 4

    Toast
    Shot '01-toast'
    [Inj]::PenStroke((L 300 260 460 260), $false, 512)
    [Inj]::PenStrokeFlags((L 290 245 470 245 470 275 290 275 290 245), 1, 512)
    Shot '02-lassoed-with-toast'
    HoverAt 380 259
    [Inj]::PenStrokeFlags((L 380 260 380 300), 1, 512)
    Shot '03-moved-beside-toast'
    Alive 'move beside'

    Toast
    HoverAt 380 299
    [Inj]::PenStrokeFlags((L 380 300 420 330 470 380 500 440 500 470), 1, 512) # ends over the toast
    Shot '04-moved-onto-toast'
    [Inj]::PenStroke((L 600 200 700 200), $false, 512)
    Shot '05-pen-still-inks'
    Start-Sleep 6
    Shot '06-toast-closed'
    Alive 'end'
}

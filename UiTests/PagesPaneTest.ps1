# The pages pane (Run-SandboxTest.ps1 -Test this -Document examples\Schaltungen.xopp): thumbnails of every page, tapping
# thumbnails (mouse, pen, finger), insert a blank page, move a page up, delete a page (with its question) - screenshots
# per step; the app log is out\app.log (an unhandled exception is logged there before the app ends).
. C:\sbx\TestKit.ps1

function Alive($step) { if ($script:p.HasExited) { throw "app exited after $step (exit code $($script:p.ExitCode))" }; "alive after $step" }

Invoke-Test {
    $work = 'C:\work'; New-Item -ItemType Directory -Force "$work\cfg" | Out-Null
    Copy-Item "$root\data\*" $work -Force
    Set-Content "$work\cfg\settings.json" '{"ExplorerOpen":true,"PagesShown":true,"ProjectFolder":"C:\\work"}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$work\cfg"; $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' "`"$work\test.xopp`""
    Start-Sleep 3
    Shot '01-pane-one-page'
    Button 'InsertPageButton'; Button 'InsertPageButton'
    [Inj]::PenStroke((L 420 260 560 300), $false, 600) # ink on the page in view: its thumbnail shows it
    Start-Sleep 3
    Shot '02-three-pages'
    # Tapping thumbnails (the pane's list starts below its header; a thumbnail is ~180 x 250 DIPs): select / go to.
    [Inj]::Click(150, 260); Start-Sleep 2; Alive 'mouse click on a thumbnail'
    [Inj]::PenStroke((L 150 330 150 331), $false, 600); Start-Sleep 2; Alive 'pen tap on a thumbnail'
    [Inj]::TouchDrag(150, 300, 150, 302, 2); Start-Sleep 2; Alive 'finger tap on a thumbnail'
    Shot '03-after-taps'
    Button 'Nach oben'
    Start-Sleep 3
    Shot '04-moved-up'
    Button 'Seite löschen'
    Start-Sleep -Milliseconds 800
    Shot '05-delete-question'
    Button 'Löschen'
    Start-Sleep 3
    Shot '06-deleted'
    Alive 'delete'
}

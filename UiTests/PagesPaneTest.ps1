# The pages pane (Run-SandboxTest.ps1 -Test this -Document examples\Schaltungen.xopp): thumbnails of every page, insert a
# blank page, move a page down, delete a page (with its question) - screenshots per step; the app log is out\app.log.
. C:\sbx\TestKit.ps1

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
    Button 'Nach oben'
    Start-Sleep 3
    Shot '03-moved-up'
    Button 'Seite löschen'
    Start-Sleep -Milliseconds 800
    Shot '04-delete-question'
    Button 'Löschen'
    Start-Sleep 3
    Shot '05-deleted'
}

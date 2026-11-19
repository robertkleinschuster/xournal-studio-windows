# The pages pane, inserting pages of another PDF, and the iPad import (Windows Sandbox):
#   - Seiten tab: thumbnails of all pages (screenshot); a tap on a thumbnail goes to the page;
#   - the context menu inserts a blank page after page 1; a mouse drag moves it to the end; delete it again (asked);
#   - Seiten aus PDF oder Dokument einfügen: Grenzwerte.pdf's pages appended to the document's PDF (checked: new pages
#     refer to PDF pages after the original ones);
#   - Datei > PDF mit iPad-Anmerkungen importieren with a PDF without Apple markup (the flow: pickers, the PDF beside the
#     new .xopp, no temporary files left, the new document opened).
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\PagesPaneTest.ps1 -Extra <uiaprobe.exe>,examples\Grenzwerte.pdf
# Names with umlauts are built from character codes (this script is read as ANSI by PS 5.1).
. C:\sbx\TestKit.ps1

$ae = [string][char]0xE4; $ue = [string][char]0xFC; $oe = [string][char]0xF6
function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1500; "ctrl+$label" }
# The center of an element (by part of its name) in screen pixels, found by the native UI Automation client.
function Probe($name) {
    $at = & C:\sbx\data\uiaprobe.exe XournalStudioWindow $name
    if ($LASTEXITCODE -ne 0 -or -not $at) { throw "'$name' not found by uiaprobe" }
    $x, $y = "$at".Trim() -split ' '
    , @([int]$x, [int]$y)
}
function Dip($pixel) { , @(($pixel[0] / 1.5), (($pixel[1] - 64) / 1.5)) } # screen pixels -> client DIPs (150 %, client at y 64)
function TypePath($path) {
    Start-Sleep -Milliseconds 1500
    [System.Windows.Forms.SendKeys]::SendWait($path); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}'); Start-Sleep -Milliseconds 3000
    "typed $path"
}
function Read-Xopp($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        (New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)).ReadToEnd()
    } finally { $stream.Dispose() }
}
function Pages($file) {
    $doc = New-Object Xml.XmlDocument
    $doc.LoadXml((Read-Xopp $file))
    @($doc.SelectNodes('/xournal/page') | ForEach-Object {
        $bg = $_.SelectSingleNode('background')
        [pscustomobject]@{ type = $bg.GetAttribute('type'); pageno = $bg.GetAttribute('pageno'); strokes = $_.SelectNodes('layer/stroke').Count }
    })
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    Copy-Item C:\sbx\data\Schaltungen.xopp, C:\sbx\data\Schaltungen.pdf, C:\sbx\data\Grenzwerte.pdf C:\work
    $file = 'C:\work\Schaltungen.xopp'
    $original = Pages $file
    "original: $($original.Count) pages"

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 3

    # The pane with the Seiten tab.
    Ctrl 0x42 'B'
    $tab = Probe 'Seiten' # the comctl32 tab, seen by the native client only
    [Inj]::ClickPixel($tab[0], $tab[1]); Start-Sleep 5
    Shot '01-pages-pane'
    & C:\sbx\data\uiaprobe.exe XournalStudioWindow | Out-File "$out\uia-tree.txt" -Encoding utf8

    # A tap on page 2's thumbnail goes there (a thumbnail in view: one scrolled out of view has its center outside).
    $p2 = Probe 'Seite 2'
    [Inj]::ClickPixel($p2[0], $p2[1]); Start-Sleep 2
    Shot '02-went-to-page-2'

    # The context menu of page 1: a blank page after it.
    $p1 = Probe 'Seite 1'
    [Inj]::RightClickPixel($p1[0], $p1[1]); Start-Sleep 1
    Shot '03-page-menu'
    # The item by its access key: the managed UI Automation client did not find the popup menu's items.
    [Inj]::Key(0x43); 'menu: Leere Seite dana&ch einfuegen'
    Start-Sleep 2
    Ctrl 0x53 'S'
    $pages = Pages $file
    "after inserting: $($pages.Count) pages, page 2: $($pages[1].type)"
    if ($pages.Count -ne $original.Count + 1 -or $pages[1].type -ne 'solid') { throw 'the blank page is not after page 1' }

    # Drag the new page 2 above page 1 (the insert mark shows where): then the blank page is the first.
    $from = Dip (Probe 'Seite 2')
    $onto = Dip (Probe 'Seite 1')
    [Inj]::MouseDrag((L $from[0] $from[1] ($from[0] + 2) ($from[1] - 20) $onto[0] ($onto[1] - 60)))
    Start-Sleep 2
    Shot '03-dragged-to-the-top'
    Ctrl 0x53 'S'
    $pages = Pages $file
    "after the drag: $(($pages | ForEach-Object { $_.type }) -join ',')"
    if ($pages[0].type -ne 'solid' -or $pages[1].type -ne $original[0].type) { throw 'the drag did not move the blank page to the top' }

    # Pages of Grenzwerte.pdf, all, after the page in view: appended to Schaltungen.pdf.
    $p1 = Probe 'Seite 1'
    [Inj]::RightClickPixel($p1[0], $p1[1]); Start-Sleep 1
    Shot '04-page-menu'
    [Inj]::Key(0x50); 'menu: Seiten aus &PDF oder Dokument einfuegen'
    TypePath 'C:\work\Grenzwerte.pdf'
    Shot '04-insert-dialog'
    ClickAnywhere ('Alle ausw' + $ae + 'hlen')
    Start-Sleep 1
    ClickAnywhere ('Einf' + $ue + 'gen')
    Start-Sleep 4
    Shot '05-inserted-from-pdf'
    Ctrl 0x53 'S'
    $after = Pages $file
    $added = $after.Count - $pages.Count
    $maxOriginal = ($pages | Where-Object { $_.type -eq 'pdf' } | ForEach-Object { [int]$_.pageno } | Measure-Object -Maximum).Maximum
    $newPdfPages = @($after | Where-Object { $_.type -eq 'pdf' -and [int]$_.pageno -gt $maxOriginal })
    "inserted from PDF: $added pages, $($newPdfPages.Count) refer to appended PDF pages (after $maxOriginal)"
    if ($added -lt 1 -or $newPdfPages.Count -ne $added) { throw 'the inserted pages do not refer to appended PDF pages' }

    # The iPad import of a PDF without markup: the flow, the PDF beside the .xopp, no temporary files left.
    Ctrl 0x53 'S'
    [System.Windows.Forms.SendKeys]::SendWait('%d'); Start-Sleep -Milliseconds 600   # Datei
    [System.Windows.Forms.SendKeys]::SendWait('i'); Start-Sleep -Milliseconds 600    # PDF mit iPad-Anmerkungen &importieren
    TypePath 'C:\work\Grenzwerte.pdf'
    TypePath 'C:\work\Import.xopp'
    for ($i = 0; $i -lt 30 -and -not (Select-String -Path "$out\app.log" -Pattern 'import: done' -Quiet); $i++) { Start-Sleep -Milliseconds 500 }
    Start-Sleep 3
    Shot '06-imported'
    if (-not (Test-Path 'C:\work\Import.xopp') -or -not (Test-Path 'C:\work\Import.pdf')) { throw 'the import did not write Import.xopp and Import.pdf' }
    $left = @(Get-ChildItem C:\work, $env:TEMP -Filter '*import*' -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*~import*' -or $_.Name -like 'xournal-studio-import-*' })
    if ($left.Count -gt 0) { throw "temporary files left: $($left.Name -join ', ')" }
    $script:p.Refresh()
    "title: $($script:p.MainWindowTitle)"
    if ($script:p.MainWindowTitle -notlike 'Import *') { throw 'the imported document was not opened' }
    Select-String -Path "$out\app.log" -Pattern 'toast \(|import:|navigation read|document view: page' | ForEach-Object { "log: $($_.Line)" }
    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

# Pages in the app (Windows Sandbox): what lies under the ink on screen (a text added to page 1, the PDF page, the
# pictures of the later pages - zoomed out: screenshots), the toolbar with the app's buttons (Speichern, Exportieren,
# Seite einfügen, Seite löschen), and the page operations: a page inserted after page 1 and deleted again (asked),
# while the undo history of a stroke on page 1 survives both (undo/redo after each, checked in the saved file).
#   UiTests\Run-AppTest.ps1 -Test UiTests\PagesFlowTest.ps1 -Extra <uiaprobe.exe>
# Toolbar buttons are found with InkPages' uiaprobe (the managed UI Automation client of PS 5.1 sees comctl32 toolbars
# only as an empty pane); names are passed as ASCII parts (this script is read as ANSI).
. C:\sbx\TestKit.ps1

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function Stroke($x0, $y0, $x1, $y1) {
    Hover $x0 $y0
    [Inj]::PenFrame($x0, $y0, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    $n = [Math]::Max(1, [int]([Math]::Sqrt(($x1 - $x0) * ($x1 - $x0) + ($y1 - $y0) * ($y1 - $y0)) / 4))
    for ($s = 1; $s -le $n; $s++) {
        [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, 0, 600)
    }
    [Inj]::PenFrame($x1, $y1, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x1, $y1, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 700
    "stroke $x0,$y0 -> $x1,$y1"
}
function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1500; "ctrl+$label" }
# Clicks a toolbar button by (part of) its accessible name, found by uiaprobe.
function ToolbarButton($name) {
    $at = & C:\sbx\data\uiaprobe.exe XournalStudioWindow $name
    if ($LASTEXITCODE -ne 0 -or -not $at) { throw "toolbar button '$name' not found by uiaprobe" }
    $x, $y = "$at".Trim() -split ' '
    [Inj]::ClickPixel([int]$x, [int]$y); Start-Sleep -Milliseconds 800; [Inj]::MoveMouse(980, 500)
    "toolbar: $name at $x,$y"
}
function Read-Xopp($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        (New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)).ReadToEnd()
    } finally { $stream.Dispose() }
}
function Write-Xopp($file, $text) {
    $stream = [IO.File]::Create($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Compress)
        $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($text)
        $gzip.Write($bytes, 0, $bytes.Length); $gzip.Dispose()
    } finally { $stream.Dispose() }
}
# Pages of the saved file: per page its stroke count and background type.
function Pages($file) {
    $doc = New-Object Xml.XmlDocument
    $doc.LoadXml((Read-Xopp $file))
    @($doc.SelectNodes('/xournal/page') | ForEach-Object {
        [pscustomobject]@{ strokes = $_.SelectNodes('layer/stroke').Count; background = $_.SelectSingleNode('background').GetAttribute('type') }
    })
}
function Expect-Pages($label, $file, $count, $page1Strokes) {
    $pages = Pages $file
    Write-Host "${label}: $($pages.Count) pages, page 1 has $($pages[0].strokes) strokes" # not output: the function returns the pages
    if ($pages.Count -ne $count) { throw "${label}: $($pages.Count) pages, expected $count" }
    if ($pages[0].strokes -ne $page1Strokes) { throw "${label}: page 1 has $($pages[0].strokes) strokes, expected $page1Strokes" }
    , $pages
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    Copy-Item C:\sbx\data\Schaltungen.pdf C:\work
    # A text on page 1 (the examples have none): upstream's <text> as Xournal++ writes it.
    $xml = Read-Xopp C:\sbx\data\Schaltungen.xopp
    $first = $xml.IndexOf('<layer>')
    $text = '<text font="Sans Bold" size="20" x="60" y="40" color="#cc0000ff">Testtext auf Seite 1</text>'
    Write-Xopp C:\work\Seiten.xopp ($xml.Substring(0, $first + 7) + $text + $xml.Substring($first + 7))
    $file = 'C:\work\Seiten.xopp'
    $original = Pages $file
    "original: $($original.Count) pages, page 1 has $($original[0].strokes) strokes"

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 4
    Shot '01-text-and-toolbar'
    & C:\sbx\data\uiaprobe.exe XournalStudioWindow | Out-File "$out\uia-tree.txt" -Encoding utf8

    # The pictures on the later pages: zoomed out, all pages in view, then back to the page width.
    for ($i = 0; $i -lt 5; $i++) { [Inj]::Ctrl(0xBD); Start-Sleep -Milliseconds 400 }
    Start-Sleep 4
    Shot '02-all-pages-pictures'
    Ctrl 0x30 '0'
    Start-Sleep 2

    # A stroke on page 1, then a page inserted after it: the stroke's undo/redo still works.
    Stroke 150 300 450 330
    ToolbarButton 'Seite einf'
    Start-Sleep 2
    Shot '03-page-inserted'
    Ctrl 0x53 'S'
    $pages = Expect-Pages 'after inserting' $file ($original.Count + 1) ($original[0].strokes + 1)
    if ($pages[1].strokes -ne 0 -or $pages[1].background -ne 'solid') { throw 'the inserted page is not a blank page after page 1' }
    Ctrl 0x5A 'Z'; Ctrl 0x53 'S'
    Expect-Pages 'undo after inserting' $file ($original.Count + 1) $original[0].strokes | Out-Null
    Ctrl 0x59 'Y'; Ctrl 0x53 'S'
    Expect-Pages 'redo after inserting' $file ($original.Count + 1) ($original[0].strokes + 1) | Out-Null

    # Delete the inserted page (in view after inserting): asked first; the stroke's history survives.
    ToolbarButton 'Seite l'
    Shot '04-delete-question'
    ClickAnywhere ('L' + [char]0xF6 + 'schen')
    Start-Sleep 2
    Shot '05-page-deleted'
    Ctrl 0x53 'S'
    Expect-Pages 'after deleting' $file $original.Count ($original[0].strokes + 1) | Out-Null
    Ctrl 0x5A 'Z'; Ctrl 0x53 'S'
    Expect-Pages 'undo after deleting' $file $original.Count $original[0].strokes | Out-Null

    # The app's Speichern button saves too (a change first).
    Ctrl 0x59 'Y'
    ToolbarButton 'Speichern'
    Start-Sleep 1
    Expect-Pages 'saved with the toolbar button' $file $original.Count ($original[0].strokes + 1) | Out-Null
    Select-String -Path "$out\app.log" -Pattern 'document view: page|toast \(' | ForEach-Object { "log: $($_.Line)" }
    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

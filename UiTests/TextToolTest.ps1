# The Text tool in the app: open examples\Schaltungen.xopp, write a new text with the pen (a tap, the keyboard types,
# umlauts and a second line), end it with a tap beside, save; edit it again, save; undo the change, save; select it with
# a mouse lasso and move it, save - and check each saved file: the new <text> as upstream writes it (font "Sans", size,
# x, y, color, content), the other texts and strokes unchanged.
#   UiTests\Run-AppTest.ps1 -Test UiTests\TextToolTest.ps1 -Extra <uiaprobe.exe>
# (Umlauts as [char] codes: Windows PowerShell 5.1 reads a script without BOM in the ANSI code page.)
. C:\sbx\TestKit.ps1

function ToolbarButton($name) {
    $at = & C:\sbx\data\uiaprobe.exe XournalStudioWindow $name
    if ($LASTEXITCODE -ne 0 -or -not $at) { throw "toolbar button '$name' not found by uiaprobe" }
    $x, $y = "$at".Trim() -split ' '
    [Inj]::ClickPixel([int]$x, [int]$y); Start-Sleep -Milliseconds 800; [Inj]::MoveMouse(980, 500)
    "toolbar: $name at $x,$y"
}

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function PenTap($label, $x, $y) {
    Hover $x $y
    [Inj]::PenFrame($x, $y, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    [Inj]::PenFrame($x, $y, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    Hover $x $y
    [Inj]::PenFrame($x, $y, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 700
    "pen tap $label"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 600; "key 0x{0:X}" -f $k }
function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1500; "ctrl+$label" }
function Write-Typed($text) { [Inj]::TypeText($text); Start-Sleep -Milliseconds 500; "typed $($text.Length) characters" }

function Read-Doc($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        $doc = New-Object Xml.XmlDocument
        $doc.LoadXml((New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)).ReadToEnd())
        $doc
    } finally { $stream.Dispose() }
}
# The texts of page 1 and the number of strokes of the document.
function Texts($file) { @((Read-Doc $file).SelectNodes('/xournal/page[1]/layer/text')) }
function StrokeCount($file) { (Read-Doc $file).SelectNodes('//stroke').Count }

$ae = [char]0xE4; $ue = [char]0xFC

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    Copy-Item C:\sbx\data\Schaltungen.xopp, C:\sbx\data\Schaltungen.pdf C:\work
    $file = 'C:\work\Schaltungen.xopp'
    $textsBefore = @(Texts $file).Count
    $strokesBefore = StrokeCount $file
    "original: $textsBefore texts on page 1, $strokesBefore strokes"

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 3

    # 1. a new text: the Text tool, a pen tap, typing, a tap beside; Ctrl+S
    # The Text button: uiaprobe finds a name by a part of it ("Text" is in "Textmarker"), so it is found between its
    # neighbors (the icon left of the middle, the options arrow right of it).
    $shapes = & C:\sbx\data\uiaprobe.exe XournalStudioWindow 'Formen'
    $ruler = & C:\sbx\data\uiaprobe.exe XournalStudioWindow 'Lineal'
    $sx, $sy = "$shapes".Trim() -split ' '; $rx, $ry = "$ruler".Trim() -split ' '
    [Inj]::ClickPixel([int](([int]$sx + [int]$rx) / 2) - 8, [int]$sy); Start-Sleep -Milliseconds 800; [Inj]::MoveMouse(980, 500)
    "toolbar: Text between $sx and $rx"
    PenTap 'new text' 150 290
    Write-Typed "Gr$($ue)n und K$($ae)se`nZeile 2"
    Shot '01-typing'
    PenTap 'beside' 700 420
    Shot '02-ended'
    Ctrl 0x53 'S'
    $texts = @(Texts $file)
    if ($texts.Count -ne $textsBefore + 1) { throw "after the new text: $($texts.Count) texts on page 1, expected $($textsBefore + 1)" }
    $new = $texts[$texts.Count - 1]
    "new text: font=$($new.GetAttribute('font')) size=$($new.GetAttribute('size')) x=$($new.GetAttribute('x')) y=$($new.GetAttribute('y')) color=$($new.GetAttribute('color'))"
    $content = $new.InnerText
    "content: " + (($content.ToCharArray() | ForEach-Object { '{0:X}' -f [int]$_ }) -join ' ')
    if ($content -ne ("Gr" + $ue + "n und K" + $ae + "se`nZeile 2")) { throw "content: '$content'" }
    if ($new.GetAttribute('font') -ne 'Sans') { throw "font: $($new.GetAttribute('font'))" }
    $x = [double]::Parse($new.GetAttribute('x'), [Globalization.CultureInfo]::InvariantCulture)
    if ((StrokeCount $file) -ne $strokesBefore) { throw 'the strokes changed' }

    # 2. edit it: a tap on it, End, typing, a tap beside; Ctrl+S - then undo the change, Ctrl+S
    PenTap 'on the text' 165 296
    Key 0x23  # End
    Write-Typed ' neu'
    Shot '03-editing-again'
    PenTap 'beside again' 700 420
    Ctrl 0x53 'S'
    $edited = @(Texts $file)[$textsBefore]
    "edited: '$($edited.InnerText)'"
    if ($edited.InnerText -notlike '*neu') { throw "the edit is not in the file: '$($edited.InnerText)'" }
    Ctrl 0x5A 'Z'
    Ctrl 0x53 'S'
    $undone = @(Texts $file)[$textsBefore]
    if ($undone.InnerText -like '*neu') { throw "undo left the edit: '$($undone.InnerText)'" }
    Shot '04-undone'

    # 3. the selection: a mouse lasso around the text selects it, a drag moves it 60 DIPs right
    [Inj]::MouseDrag(@(130, 270, 420, 270, 420, 345, 130, 345, 130, 270)); Start-Sleep -Milliseconds 800
    Shot '05-selected'
    [Inj]::MouseDrag(@(170, 300, 230, 300)); Start-Sleep -Milliseconds 800
    Shot '06-moved'
    Ctrl 0x53 'S'
    $moved = @(Texts $file)[$textsBefore]
    $x2 = [double]::Parse($moved.GetAttribute('x'), [Globalization.CultureInfo]::InvariantCulture)
    "moved: x $x -> $x2"
    if ($x2 -le $x + 10) { throw "the text did not move: x $x -> $x2" }
    if (@(Texts $file).Count -ne $textsBefore + 1) { throw 'texts were added or lost by the move' }
    if ((StrokeCount $file) -ne $strokesBefore) { throw 'the strokes changed' }
    'text tool: passed'
}

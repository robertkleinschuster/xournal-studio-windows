# The app in Windows Sandbox with synthetic pen input: open examples\Schaltungen.xopp, write a stroke, move the page's
# content (select all + pen drag), undo the move, erase across the page, saving after each step - and check the saved
# file each time: strokes the user did not touch are byte-identical elements of the original, moved ones are the original
# elements with all coordinates shifted by one offset (attributes and widths unchanged), new ink is added. Then reload.
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\InkRoundTripTest.ps1 -Extra <uiaprobe.exe>
# Toolbar buttons are found by name with InkPages' uiaprobe (PS 5.1's managed UI Automation sees comctl32 toolbars only
# as an empty pane).
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
# A pen stroke between client DIPs; hovers ~120 ms before it touches, as a real pen.
function Stroke($label, $x0, $y0, $x1, $y1) {
    Hover $x0 $y0
    [Inj]::PenFrame($x0, $y0, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    $n = [Math]::Max(1, [int]([Math]::Sqrt(($x1 - $x0) * ($x1 - $x0) + ($y1 - $y0) * ($y1 - $y0)) / 4))
    for ($s = 1; $s -le $n; $s++) {
        [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, 0, 600)
    }
    [Inj]::PenFrame($x1, $y1, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x1, $y1, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 700
    "stroke ${label}: $x0,$y0 -> $x1,$y1"
}
function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1500; "ctrl+$label" }

# The strokes of a saved .xopp: per page a list of their outer XML (attribute order as written).
function Read-Strokes($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        $reader = New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)
        $doc = New-Object Xml.XmlDocument
        $doc.PreserveWhitespace = $true
        $doc.LoadXml($reader.ReadToEnd())
    } finally { $stream.Dispose() }
    $pages = @()
    foreach ($page in $doc.SelectNodes('/xournal/page')) {
        $pages += , @($page.SelectNodes('layer/stroke') | ForEach-Object { $_.OuterXml })
    }
    , $pages
}

# Saved strokes against the original: how many are identical, missing (erased) and new.
function Compare-Strokes($label, $original, $saved) {
    $identical = 0; $missing = 0; $new = 0
    for ($i = 0; $i -lt $original.Count; $i++) {
        $before = $original[$i]; $after = $saved[$i]
        $set = New-Object 'System.Collections.Generic.HashSet[string]'
        foreach ($s in $after) { [void]$set.Add($s) }
        foreach ($s in $before) { if ($set.Contains($s)) { $identical++ } else { $missing++ } }
        $origSet = New-Object 'System.Collections.Generic.HashSet[string]'
        foreach ($s in $before) { [void]$origSet.Add($s) }
        foreach ($s in $after) { if (-not $origSet.Contains($s)) { $new++ } }
    }
    "${label}: identical $identical, missing $missing, new $new"
    @{ identical = $identical; missing = $missing; new = $new }
}

# Page 1 moved: every original stroke there that is ink has its attributes and widths unchanged and every coordinate
# shifted by one offset (dx, dy), up to the file's 4 decimals; strokes drawn from the file (styled) are not ink and stay.
function Check-Moved($original, $saved) {
    $savedSet = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($s in $saved) { [void]$savedSet.Add($s) }
    $unmoved = @($original | Where-Object { $savedSet.Contains($_) }).Count
    $before = @($original | Where-Object { -not $savedSet.Contains($_) } | ForEach-Object { [xml]$_ })
    $after = @($saved | ForEach-Object { [xml]$_ })
    $dx = $null; $dy = $null; $matched = 0
    foreach ($b in $before) {
        $bn = $b.DocumentElement
        $bc = $bn.InnerText.Trim() -split '\s+'
        foreach ($a in $after) {
            $an = $a.DocumentElement
            $ac = $an.InnerText.Trim() -split '\s+'
            if ($ac.Count -ne $bc.Count) { continue }
            $sameAttributes = $true
            foreach ($attr in $bn.Attributes) { if ($an.GetAttribute($attr.Name) -ne $attr.Value) { $sameAttributes = $false } }
            if (-not $sameAttributes -or $an.Attributes.Count -ne $bn.Attributes.Count) { continue }
            $ox = [double]$ac[0] - [double]$bc[0]; $oy = [double]$ac[1] - [double]$bc[1]
            if ($dx -ne $null -and ([Math]::Abs($ox - $dx) -gt 0.0002 -or [Math]::Abs($oy - $dy) -gt 0.0002)) { continue }
            $all = $true
            for ($k = 0; $k + 1 -lt $bc.Count; $k += 2) {
                if ([Math]::Abs([double]$ac[$k] - [double]$bc[$k] - $ox) -gt 0.0002 -or [Math]::Abs([double]$ac[$k + 1] - [double]$bc[$k + 1] - $oy) -gt 0.0002) { $all = $false; break }
            }
            if ($all) { $dx = $ox; $dy = $oy; $matched++; break }
        }
    }
    "moved: $matched of $($before.Count) changed original strokes on page 1 shifted by ($dx, $dy) with their attributes; $unmoved unchanged"
    if ($matched -ne $before.Count -or $dy -eq $null -or [Math]::Abs($dy) -lt 1) { throw 'the moved strokes are not the original elements shifted by one offset' }
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    Copy-Item C:\sbx\data\* C:\work
    $file = 'C:\work\Schaltungen.xopp'
    $original = Read-Strokes $file
    "original: $($original.Count) pages, $(($original | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum) strokes, page 1: $($original[0].Count)"

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 3
    Shot '01-opened'

    # 1. write: one new stroke, everything else identical
    Stroke 'new' 150 300 450 330
    Ctrl 0x53 'S'
    Shot '02-written-saved'
    $r = Compare-Strokes 'after writing' $original (Read-Strokes $file)
    if ($r.missing -ne 0 -or $r.new -ne 1) { throw 'writing one stroke must add exactly one and keep every other element' }
    $afterWrite = Read-Strokes $file

    # 2. move page 1's content: select all, drag with the pen inside the selection
    Ctrl 0x41 'A'
    Shot '03-selected'
    Hover 300 280
    Stroke 'move' 300 280 300 330
    Shot '04-moved'
    Ctrl 0x53 'S'
    $moved = Read-Strokes $file
    Check-Moved $original[0] $moved[0]
    $rest = Compare-Strokes 'other pages after the move' ($original | Select-Object -Skip 1) ($moved | Select-Object -Skip 1)
    if ($rest.missing -ne 0 -or $rest.new -ne 0) { throw 'the move changed other pages' }

    # 3. undo the move: the original elements again
    Ctrl 0x5A 'Z'
    Ctrl 0x53 'S'
    Shot '05-undone-saved'
    $r = Compare-Strokes 'after undoing the move' $afterWrite (Read-Strokes $file)
    if ($r.missing -ne 0 -or $r.new -ne 0) { throw 'undoing the move must give back the elements as written before' }

    # 4. erase (the eraser tool, exact) across the page: touched strokes are cut, all others stay identical
    ToolbarButton 'Radierer'
    Stroke 'erase' 300 120 300 560
    Ctrl 0x53 'S'
    Shot '06-erased-saved'
    $r = Compare-Strokes 'after erasing' $afterWrite (Read-Strokes $file)
    if ($r.missing -lt 1) { throw 'the eraser line touched nothing (check the screenshot)' }
    $afterErase = Read-Strokes $file

    # 5. close (saved: no question) and reload: the saved strokes come back
    $script:p.CloseMainWindow() | Out-Null
    if (-not $script:p.WaitForExit(8000)) { throw 'the app did not end after its window was closed (unexpected question?)' }
    "exit code: $($script:p.ExitCode)"
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 3
    Shot '07-reloaded'
    $restored = Select-String -Path "$out\app.log.inkpages.log" -Pattern 'ink: page 0 restored \((\d+) strokes\)' | Select-Object -Last 1
    "reload: $restored (saved page 1: $($afterErase[0].Count) strokes)"
    if (-not $restored -or [int]$restored.Matches[0].Groups[1].Value -ne $afterErase[0].Count) { throw 'the reloaded page 1 has not the saved strokes' }
    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

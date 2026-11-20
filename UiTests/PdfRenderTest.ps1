# PDF pages and thumbnails on screen: a generated PDF of 80 pages (page 1: text from 4 to 16 pt, hairlines, a fine grid,
# diagonals; page 2 turned by /Rotate 90; page 3 a crop box inside a larger media box, red marks outside it that must not
# show; page 4 a /Square annotation with an appearance; page 5 A5 landscape; then copies of page 1) opened as a document.
# Screenshots at fit width, zoomed in by keys (settled) and by a pinch (right after it and settled), at the largest zoom,
# zoomed out, the Seiten tab and the thumbnails after writing; the log's render lines (pdf render, bitmap upload,
# underlay ... drawn ... in ms) and the process' memory after scrolling through the document at a high zoom.
#   UiTests\Run-AppTest.ps1 -Test UiTests\PdfRenderTest.ps1 -Extra <uiaprobe.exe>
. C:\sbx\TestKit.ps1

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Wheel
{
    [DllImport("user32")] static extern void mouse_event(uint flags, int dx, int dy, int data, IntPtr extra);
    [DllImport("user32")] static extern bool SetCursorPos(int x, int y);
    public static void Turn(int x, int y, int notches)
    {
        SetCursorPos(x, y);
        for (int i = 0; i < Math.Abs(notches); i++) { mouse_event(0x0800, 0, 0, notches < 0 ? -120 : 120, IntPtr.Zero); System.Threading.Thread.Sleep(15); }
    }
}
'@

function Probe($name) {
    $at = & C:\sbx\data\uiaprobe.exe XournalStudioWindow $name
    if ($LASTEXITCODE -ne 0 -or -not $at) { throw "'$name' not found by uiaprobe" }
    $x, $y = "$at".Trim() -split ' '
    , @([int]$x, [int]$y)
}
function Click($name) { $p = Probe $name; [Inj]::ClickPixel($p[0], $p[1]); Start-Sleep -Milliseconds 900; "click $name" }
# The window in the foreground first: after the touch input of the pinch it was not (keys went elsewhere).
function Ctrl($k, $label) { [Inj]::SetForegroundWindow($script:h) | Out-Null; [Inj]::Ctrl($k); Start-Sleep -Milliseconds 300; "ctrl+$label" }
function Memory($what) {
    $script:p.Refresh()
    'memory {0}: private {1:N0} MB, working set {2:N0} MB' -f $what, ($script:p.PrivateMemorySize64 / 1MB), ($script:p.WorkingSet64 / 1MB)
}
function Mark($what) { Add-Content "$out\app.log" "---- $what" ; "---- $what" }

# A PDF written by hand: objects, then the cross-reference table with their byte offsets.
function Stream($content) { "<< /Length $($content.Length) >>`nstream`n$content`nendstream" }
function New-RenderPdf($file, $copies) {
    $text = ''
    $y = 800
    foreach ($size in 4, 5, 6, 8, 10, 12, 16) {
        $text += "BT /F1 $size Tf 40 $y Td ($size pt: Induktivitaet L = 4,7 mH; The quick brown fox jumps over the lazy dog 0123456789) Tj ET`n"
        $y -= $size + 6
    }
    $lines = "0 0 0 RG`n"
    $y = 680
    foreach ($w in 0, 0.1, 0.25, 0.5, 1) { $lines += "$w w 40 $y m 555 $y l S`n"; $y -= 8 }
    $grid = "0.2 w 0 0 1 RG`n"
    for ($i = 0; $i -le 50; $i++) { $x = 40 + 4 * $i; $grid += "$x 400 m $x 600 l S 40 $(400 + 4 * $i) m 240 $(400 + 4 * $i) l S`n" }
    $diag = "0.3 w 0 0 0 RG`n"
    for ($i = 0; $i -lt 20; $i++) { $diag += "300 400 m $(320 + 12 * $i) 600 l S`n" }
    $circle = "0.5 w 1 0 0 RG 400 200 m 400 255.2 355.2 300 300 300 c 244.8 300 200 255.2 200 200 c 200 144.8 244.8 100 300 100 c 355.2 100 400 144.8 400 200 c S`n"
    $page1 = $text + $lines + $grid + $diag + $circle
    $rotated = "BT /F1 28 Tf 60 700 Td (Gedrehte Seite: /Rotate 90) Tj ET 2 w 60 680 m 400 680 l S 1 w 20 20 555 802 re S 0 0 m 595 842 l S 595 0 m 0 842 l S BT /F1 20 Tf 150 430 Td (Mitte der gedrehten Seite) Tj ET BT /F1 8 Tf 150 405 Td (8 pt: The quick brown fox jumps over the lazy dog) Tj ET"
    $crop = "1 0 0 rg 0 0 700 40 re f 0 910 700 40 re f 1 0 0 RG 6 w 0 0 m 700 950 l S 0 0.6 0 RG 3 w 52 52 591 838 re S BT /F1 24 Tf 80 800 Td (Crop box: kein Rot sichtbar) Tj ET BT /F1 20 Tf 200 480 Td (Mitte der Crop-Box-Seite) Tj ET BT /F1 8 Tf 200 455 Td (8 pt: The quick brown fox jumps over the lazy dog) Tj ET 0.5 w 0 0 1 RG 340 52 m 340 890 l S 50 470 m 645 470 l S"
    $annot = "BT /F1 24 Tf 60 780 Td (Seite mit Annotation \(blaues Quadrat\)) Tj ET"
    $a5 = "BT /F1 24 Tf 40 360 Td (A5 quer) Tj ET 1 w 20 20 555 380 re S"
    $copyContent = $page1
    $kids = @()
    $objects = @(
        '<< /Type /Catalog /Pages 2 0 R >>',
        'PAGES',
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
        (Stream $page1), (Stream $rotated), (Stream $crop), (Stream $annot), (Stream $a5),
        (Stream "0 0 1 rg 0 0 200 150 re f 1 1 1 rg BT /F1 18 Tf 20 70 Td (Annotation) Tj ET")
    )
    # 1 catalog, 2 pages, 3 font, 4-8 contents, 9 annotation appearance
    $res = '/Resources << /Font << /F1 3 0 R >> >>'
    $objects += "<< /Type /Annot /Subtype /Square /Rect [200 350 400 500] /C [0 0 1] /AP << /N 9 0 R >> /F 4 >>"   # 10
    $objects[8] = "<< /Type /XObject /Subtype /Form /BBox [0 0 200 150] /Resources << /Font << /F1 3 0 R >> >> /Length " + ("0 0 1 rg 0 0 200 150 re f 1 1 1 rg BT /F1 18 Tf 20 70 Td (Annotation) Tj ET".Length) + " >>`nstream`n0 0 1 rg 0 0 200 150 re f 1 1 1 rg BT /F1 18 Tf 20 70 Td (Annotation) Tj ET`nendstream"
    $pages = @(
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] $res /Contents 4 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Rotate 90 $res /Contents 5 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 700 950] /CropBox [50 50 645 892] $res /Contents 6 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] $res /Contents 7 0 R /Annots [10 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 420] $res /Contents 8 0 R >>"
    )
    for ($i = 0; $i -lt $copies; $i++) { $pages += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] $res /Contents 4 0 R >>" }
    for ($i = 0; $i -lt $pages.Count; $i++) { $kids += "$($objects.Count + 1 + $i) 0 R" }
    $objects += $pages
    $objects[1] = "<< /Type /Pages /Kids [$($kids -join ' ')] /Count $($pages.Count) >>"
    $body = "%PDF-1.4`n"
    $offsets = @()
    for ($i = 0; $i -lt $objects.Count; $i++) {
        $offsets += [Text.Encoding]::ASCII.GetByteCount($body)
        $body += "$($i + 1) 0 obj`n$($objects[$i])`nendobj`n"
    }
    $xref = [Text.Encoding]::ASCII.GetByteCount($body)
    $body += "xref`n0 $($objects.Count + 1)`n0000000000 65535 f `n"
    foreach ($o in $offsets) { $body += ('{0:D10} 00000 n ' -f $o) + "`n" }
    $body += "trailer`n<< /Size $($objects.Count + 1) /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
    [IO.File]::WriteAllBytes($file, [Text.Encoding]::ASCII.GetBytes($body))
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    New-RenderPdf 'C:\work\Render.pdf' 75
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    New-Item -ItemType Directory -Force $env:XOURNAL_STUDIO_CONFIG_DIR | Out-Null
    Set-Content "$out\config\settings-native.json" '{"toolChoices":"select=1"}' -Encoding Ascii
    Start-TestApp 'XournalStudio.exe' '"C:\work\Render.pdf"'
    Shot '01-open-fit-width'
    Memory 'opened'
    $cx = [int]([Inj]::W / 2); $cy = [int]([Inj]::H / 2)

    Mark 'thumbnails'
    Ctrl 0x42 'B'
    Start-Sleep 1
    Click 'Seiten'
    Start-Sleep 4
    Shot '02-thumbnails'
    [Inj]::PenStroke((L 500 150 600 200 700 150 800 200), $false, 600)
    Mark 'ink written'
    Start-Sleep 3
    Shot '03-thumbnails-after-ink'
    Memory 'thumbnails'
    Ctrl 0x42 'B'
    Start-Sleep 1


    Mark 'zoom in by keys'
    1..4 | ForEach-Object { Ctrl 0xBB '+' }
    Start-Sleep 2
    Shot '04-zoom-keys-x2.4'
    1..6 | ForEach-Object { Ctrl 0xBB '+' }
    Start-Sleep 3
    Shot '05-zoom-max'
    Memory 'zoom max'

    Mark 'zoom out by keys'
    Ctrl 0x32 '2'
    1..6 | ForEach-Object { Ctrl 0xBD '-' }
    Start-Sleep 3
    Shot '06-zoom-out'
    Memory 'zoomed out'

    Mark 'pinch'
    Ctrl 0x32 '2'
    Start-Sleep 2
    [Inj]::Pinch($cx, 250, 100, 400, 30)
    [Inj]::Shot("$out\07a-pinch-at-once.png")
    Start-Sleep -Milliseconds 300
    [Inj]::Shot("$out\07b-pinch-300ms.png")
    Start-Sleep 2
    Shot '07c-pinch-settled'

    Mark 'scroll at zoom 3'
    Ctrl 0x32 '2'
    1..5 | ForEach-Object { Ctrl 0xBB '+' }
    Start-Sleep 2
    for ($i = 0; $i -lt 40; $i++) { [Wheel]::Turn(700, 600, -10); Start-Sleep -Milliseconds 100 }
    Start-Sleep 1
    Shot '08-scrolled-zoom3'
    Memory 'scrolled 40x10 notches at zoom 3'
    [Inj]::TouchDrag($cx, 600, $cx, 100, 8)
    [Inj]::Shot("$out\08b-flick-at-once.png")
    Start-Sleep 2
    Shot '08c-flick-settled'

    Mark 'pages 2-5'
    Ctrl 0x32 '2'
    1..6 | ForEach-Object { Ctrl 0xBD '-' }
    for ($i = 0; $i -lt 60; $i++) { [Wheel]::Turn(700, 600, 10); Start-Sleep -Milliseconds 30 }
    Start-Sleep 3
    Shot '09-pages-overview'
    # Pages 2 (/Rotate 90), 3 (crop box) and 4 (annotation) at the page width and zoomed in (tiles beyond 8 Mpx).
    foreach ($n in 2, 3, 4) {
        Mark "page $n"
        Ctrl 0x47 'G'
        Start-Sleep 1
        [Inj]::TypeText("$n"); Start-Sleep -Milliseconds 300
        [Inj]::Key(0x0D); Start-Sleep 1
        if (-not (Select-String -Path "$out\app.log" -Pattern "go to page $n of" -Quiet)) {
            throw "Navigation to page $n was not recorded in the log"
        }
        Ctrl 0x32 '2'
        Start-Sleep 2
        Shot "10-page$n-width"
        1..4 | ForEach-Object { Ctrl 0xBB '+' }
        Start-Sleep 3
        Shot "11-page$n-zoomed"
    }
    Memory 'end'

    Mark 'copy a PDF rectangle as a picture'
    Ctrl 0x32 '2'
    Click ("Ausw" + [char]0x00E4 + "hlen")
    "draw rectangle at $cx,$cy DIPs"
    [Inj]::MouseDrag(@(($cx - 55), ($cy - 45), ($cx + 55), ($cy + 45)))
    for ($i = 0; $i -lt 20 -and -not (Select-String "$out\app.log" -Pattern 'clipboard: PDF area menu shown' -Quiet); $i++) {
        Start-Sleep -Milliseconds 200
    }
    if (-not (Select-String "$out\app.log" -Pattern 'clipboard: PDF area menu shown' -Quiet)) { throw 'no menu for the PDF rectangle' }
    [Inj]::Key(0x28)
    [Inj]::Key(0x0D)
    for ($i = 0; $i -lt 30 -and -not (Select-String "$out\app.log" -Pattern 'clipboard: PDF area copied' -Quiet); $i++) {
        Start-Sleep -Milliseconds 300
    }
    if (-not (Select-String "$out\app.log" -Pattern 'clipboard: PDF area copied' -Quiet)) { throw 'PDF rectangle was not copied' }
    Ctrl 0x56 'V'
    Ctrl 0x53 'S'
    Start-Sleep 1
    $saved = 'C:\work\Snapshot.xopp'
    [Inj]::Ctrl(0x41)
    [Inj]::TypeText($saved)
    [Inj]::Key(0x0D)
    Start-Sleep 2
    $stream = [IO.File]::OpenRead($saved)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        $reader = New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)
        try { [xml]$savedDoc = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $images = $savedDoc.SelectNodes('/xournal/page/layer/image')
        if ($images.Count -ne 1 -or $images[0].InnerText.Trim() -notlike 'iVBORw0KGgo*') {
            throw 'the PDF rectangle did not paste as an upstream PNG image'
        }
    } finally { $stream.Dispose() }

    Mark 'one-finger pan away from the selection'
    $pan = 'touch: no tap \((the view moves|DirectManipulation took the contact)\)'
    $inklog = "$out\app.log.inkpages.log"
    $before = @(Select-String $inklog -Pattern $pan).Count
    [Inj]::TouchDrag(([Inj]::W * 0.19), ($cy + 105), ([Inj]::W * 0.19), ($cy - 100), 30)
    Start-Sleep -Milliseconds 800
    if (@(Select-String $inklog -Pattern $pan).Count -le $before) {
        throw 'a finger drag beside the selection did not go to DirectManipulation'
    }

    'pdf render: done'
}

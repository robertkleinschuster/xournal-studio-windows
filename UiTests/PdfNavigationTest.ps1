# PDF outline, links and search in the app: a generated three-page PDF with an outline ("Kapitel 1-3"), on page 1 a
# web link along the top and a link to page 3 below it, and a word on each page. Opened as a document: the Gliederung tab
# appears, an entry goes to its page; a mouse click on the internal link goes to page 3; a click on the web link asks
# (Abbrechen); Strg+F searches the PDF's text, F3 steps to the next hit. Checked in the app's log.
#   UiTests\Run-AppTest.ps1 -Test UiTests\PdfNavigationTest.ps1 -Extra <uiaprobe.exe>
. C:\sbx\TestKit.ps1

function Probe($name) {
    $at = & C:\sbx\data\uiaprobe.exe XournalStudioWindow $name
    if ($LASTEXITCODE -ne 0 -or -not $at) { throw "'$name' not found by uiaprobe" }
    $x, $y = "$at".Trim() -split ' '
    , @([int]$x, [int]$y)
}
function Click($name) { $p = Probe $name; [Inj]::ClickPixel($p[0], $p[1]); Start-Sleep -Milliseconds 900; "click $name" }
function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1200; "ctrl+$label" }
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 800; "key 0x{0:X}" -f $k }
function LogCount($pattern) { @(Get-Content "$out\app.log" | Where-Object { $_ -match $pattern }).Count }
function LogHas($pattern) { (LogCount $pattern) -gt 0 }
# Waits until the log has the line `$times` times.
function Expect($pattern, $what, $times = 1) {
    for ($i = 0; $i -lt 20 -and (LogCount $pattern) -lt $times; $i++) { Start-Sleep -Milliseconds 500 }
    if ((LogCount $pattern) -lt $times) { throw "not in the log ($times x): $what ($pattern)" }
    "ok: $what"
}

# A PDF written by hand: objects, then the cross-reference table with their byte offsets.
function New-TestPdf($file) {
    $objects = @(
        '<< /Type /Catalog /Pages 2 0 R /Outlines 9 0 R /PageMode /UseOutlines >>',
        '<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 8 0 R >> >> /Contents 6 0 R /Annots [13 0 R 14 0 R] >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 8 0 R >> >> /Contents 7 0 R >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 8 0 R >> >> /Contents 15 0 R >>',
        (Stream 'BT /F1 24 Tf 72 600 Td (Anfang Induktivitaet) Tj ET'),
        (Stream 'BT /F1 24 Tf 72 600 Td (Mitte Induktivitaet) Tj ET'),
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
        '<< /Type /Outlines /First 10 0 R /Last 12 0 R /Count 3 >>',
        '<< /Title (Kapitel 1) /Parent 9 0 R /Next 11 0 R /Dest [3 0 R /XYZ 0 792 0] >>',
        '<< /Title (Kapitel 2) /Parent 9 0 R /Prev 10 0 R /Next 12 0 R /Dest [4 0 R /XYZ 0 792 0] >>',
        '<< /Title (Kapitel 3) /Parent 9 0 R /Prev 11 0 R /Dest [5 0 R /XYZ 0 400 0] >>',
        '<< /Type /Annot /Subtype /Link /Rect [0 742 612 792] /Border [0 0 0] /A << /S /URI /URI (https://example.org/) >> >>',
        '<< /Type /Annot /Subtype /Link /Rect [0 380 612 740] /Border [0 0 0] /Dest [5 0 R /XYZ 0 792 0] >>',
        (Stream 'BT /F1 24 Tf 72 600 Td (Ende Induktivitaet) Tj ET')
    )
    $text = "%PDF-1.4`n"
    $offsets = @()
    for ($i = 0; $i -lt $objects.Count; $i++) {
        $offsets += [Text.Encoding]::ASCII.GetByteCount($text)
        $text += "$($i + 1) 0 obj`n$($objects[$i])`nendobj`n"
    }
    $xref = [Text.Encoding]::ASCII.GetByteCount($text)
    $text += "xref`n0 $($objects.Count + 1)`n0000000000 65535 f `n"
    foreach ($o in $offsets) { $text += ('{0:D10} 00000 n ' -f $o) + "`n" }
    $text += "trailer`n<< /Size $($objects.Count + 1) /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
    [IO.File]::WriteAllBytes($file, [Text.Encoding]::ASCII.GetBytes($text))
}
function Stream($content) { "<< /Length $($content.Length) >>`nstream`n$content`nendstream" }

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    New-TestPdf 'C:\work\Navigation.pdf'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' '"C:\work\Navigation.pdf"'
    Start-Sleep 3
    Expect 'outline: 3 entries' 'the outline is read'

    # Gliederung: the tab is there; Kapitel 3 goes to page 3.
    Ctrl 0x42 'B'
    Click 'Gliederung'
    Shot '01-outline'
    Click 'Kapitel 3'
    Expect 'pdf links: to page 3' 'the outline entry goes to page 3'
    Shot '02-outline-chosen'

    # Back to page 1; a mouse click on the internal link goes to page 3, one on the web link asks.
    Click 'Kapitel 1'
    Expect 'pdf links: to page 1' 'back to page 1'
    Ctrl 0x42 'B'   # the pane closed: the page fills the window
    Start-Sleep 1
    [Inj]::Click(500, 300); Start-Sleep 1
    Expect 'pdf links: to page 3 ' 'the link goes to page 3' 2
    Ctrl 0x42 'B'
    Click 'Kapitel 1'
    Ctrl 0x42 'B'
    Start-Sleep 1
    [Inj]::Click(500, 55); Start-Sleep 1.5
    Shot '03-web-link-question'
    ClickAnywhere 'Abbrechen'
    if (LogHas 'pdf links: opening') { throw 'the web link was opened without asking' }

    # Suche: Strg+F, a word on every page; F3 steps on.
    Ctrl 0x46 'F'
    [Inj]::TypeText('Induktivitaet'); Start-Sleep -Milliseconds 400
    Key 0x0D
    Expect 'search: 3 hits' 'three hits'
    Shot '04-search'
    Key 0x72  # F3
    Shot '05-next-hit'
    'pdf navigation: passed'
}

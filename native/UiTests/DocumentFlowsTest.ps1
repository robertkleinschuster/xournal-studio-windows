# The app's document flows in Windows Sandbox: a PDF opened as a new document (its pages visible: screenshots), the file's
# pictures and PDF background on screen, the leave question with unsaved changes (Abbrechen keeps the document, Nicht
# speichern leaves it), Save As of a document with an attached PDF (the attachment goes along, the file keeps "attach"),
# and Datei → Als PDF exportieren.
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\DocumentFlowsTest.ps1
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
function CtrlShift($k, $label) {
    [System.Windows.Forms.SendKeys]::SendWait("^+$label"); Start-Sleep -Milliseconds 1500; "ctrl+shift+$label"
}
# Types a path into the open file dialog (its name box has the focus) and confirms it.
function TypePath($path) {
    Start-Sleep -Milliseconds 1500
    [System.Windows.Forms.SendKeys]::SendWait($path); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}'); Start-Sleep -Milliseconds 2500
    "typed $path"
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
function Log-Has($pattern) { [bool](Select-String -Path "$out\app.log" -Pattern $pattern -Quiet) }

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work, C:\work\sub | Out-Null
    Copy-Item C:\sbx\data\* C:\work
    # A document with an attached PDF: "<name>.xopp.bg.pdf", domain "attach" (upstream's attachment).
    Copy-Item C:\work\Schaltungen.pdf C:\work\Angehaengt.xopp.bg.pdf
    Write-Xopp C:\work\Angehaengt.xopp ((Read-Xopp C:\work\Schaltungen.xopp) -replace 'domain="absolute" filename="Schaltungen.pdf"', 'domain="attach" filename="bg.pdf"')

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"

    # 1. a PDF as a new document: its pages show (rendered by Windows.Data.Pdf on the render thread)
    Start-TestApp 'XournalStudio.exe' '"C:\work\Schaltungen.pdf"'
    Start-Sleep 4
    Shot '01-pdf-as-document'
    if (-not (Log-Has 'pdf background: .*Schaltungen.pdf loaded')) { throw 'the PDF was not loaded for the background' }
    if (Log-Has 'ERROR') { Select-String -Path "$out\app.log" -Pattern 'ERROR' | ForEach-Object { "log: $_" } }

    # 2. the leave question: a change, then Neu - Abbrechen keeps the document, Nicht speichern leaves it
    Stroke 150 300 450 330
    Ctrl 0x4E 'N'
    Shot '02-leave-question'
    ClickAnywhere 'Abbrechen'
    Start-Sleep 1
    if ($script:p.MainWindowTitle -notlike '• *') { throw "after Abbrechen the document must still be the changed one (title: $($script:p.MainWindowTitle))" }
    "title after Abbrechen: $($script:p.MainWindowTitle)"
    Ctrl 0x4E 'N'
    ClickAnywhere 'Nicht speichern'
    Start-Sleep 2
    $script:p.Refresh()
    "title after Nicht speichern: $($script:p.MainWindowTitle)"
    if ($script:p.MainWindowTitle -notlike '*-Notiz-*') { throw 'Nicht speichern must leave for the new document' }
    Shot '03-new-document'

    # 3. the file's pictures and PDF background (Schaltungen.xopp: a PDF page, pictures, a PDF picture)
    $script:p.CloseMainWindow() | Out-Null
    if (-not $script:p.WaitForExit(8000)) { throw 'the new, unchanged document asked on close' }
    Start-TestApp 'XournalStudio.exe' '"C:\work\Angehaengt.xopp"'
    Start-Sleep 4
    Shot '04-attached-pdf-and-pictures'

    # 4. Save As into a subfolder: the attached PDF goes along, the file keeps "attach"
    Stroke 150 300 450 330
    CtrlShift 's' 's'
    TypePath 'C:\work\sub\Kopie.xopp'
    Shot '05-saved-as'
    if (-not (Test-Path 'C:\work\sub\Kopie.xopp.bg.pdf')) { throw 'Save As did not take the attached PDF along' }
    $copy = Read-Xopp 'C:\work\sub\Kopie.xopp'
    if ($copy -notmatch 'type="pdf" domain="attach" filename="bg.pdf"') { throw 'the copy does not keep "attach"' }
    "save as: Kopie.xopp + Kopie.xopp.bg.pdf, attach kept"
    if ($script:p.MainWindowTitle -notlike 'Kopie *') { throw "the document is not the copy now (title: $($script:p.MainWindowTitle))" }

    # 5. export as PDF
    Ctrl 0x45 'E'
    TypePath 'C:\work\Export.pdf'
    for ($i = 0; $i -lt 30 -and -not (Log-Has 'toast \((success|warning)\): Als PDF exportiert'); $i++) { Start-Sleep -Milliseconds 500 }
    Shot '06-exported'
    if (-not (Test-Path 'C:\work\Export.pdf')) { throw 'no exported PDF' }
    $head = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes('C:\work\Export.pdf')[0..4])
    if ($head -ne '%PDF-') { throw "the export is not a PDF ($head)" }
    Select-String -Path "$out\app.log" -Pattern 'toast \(' | ForEach-Object { "log: $($_.Line)" }
    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

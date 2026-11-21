# The clipboard in the app: open examples\Schaltungen.xopp, select page 1 (Strg+A), copy (Strg+C) - the clipboard holds
# the app's format "Xournal Studio Clip" (a <clip> of upstream elements) and, a moment later, PNG and a bitmap for other
# apps; paste (Strg+V) and save: page 1's strokes are there twice. Then a picture of another app (a bitmap set by this
# script) is pasted as a new <image>; cut (Strg+X) of that picture takes it away and a paste brings it back.
#   UiTests\Run-AppTest.ps1 -Test UiTests\ClipboardTest.ps1
. C:\sbx\TestKit.ps1
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ClipboardThemeProbe {
    [DllImport("user32")] public static extern uint GetClipboardSequenceNumber();
    [DllImport("user32")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
}
"@

function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1500; "ctrl+$label" }
function Read-Doc($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        $doc = New-Object Xml.XmlDocument
        $doc.LoadXml((New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)).ReadToEnd())
        $doc
    } finally { $stream.Dispose() }
}
function Count($file, $xpath) { (Read-Doc $file).SelectNodes($xpath).Count }
function Formats { [Windows.Forms.Clipboard]::GetDataObject().GetFormats() -join ', ' }
function PngBytes($data) {
    if ($data -is [IO.MemoryStream]) { return [Convert]::ToBase64String($data.ToArray()) }
    if ($data -is [byte[]]) { return [Convert]::ToBase64String($data) }
    throw 'The clipboard does not contain PNG bytes.'
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    Copy-Item C:\sbx\data\Schaltungen.xopp, C:\sbx\data\Schaltungen.pdf C:\work
    $file = 'C:\work\Schaltungen.xopp'
    $strokes = Count $file '//stroke'
    $page1 = Count $file '/xournal/page[1]//stroke'
    $images = Count $file '//image'
    "original: $strokes strokes ($page1 on page 1), $images images"

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 3
    [Inj]::MoveMouse(980, 500)

    # 1. copy page 1, paste it, save
    Ctrl 0x41 'A'
    Shot '01-selected'
    Ctrl 0x43 'C'
    Start-Sleep 3 # the picture for other apps is rendered in the background
    $formats = Formats
    "clipboard after copy: $formats"
    if ($formats -notmatch 'Xournal Studio Clip') { throw "the app's format is not on the clipboard: $formats" }
    if ($formats -notmatch 'PNG') { throw "no PNG for other apps: $formats" }
    $png = [Windows.Forms.Clipboard]::GetData('PNG')
    "PNG: $($png.Length) bytes"
    $bitmap = [Windows.Forms.Clipboard]::GetImage()
    if (-not $bitmap) { throw 'no bitmap for other apps' }
    "bitmap: $($bitmap.Width) x $($bitmap.Height)"
    $lightPng = PngBytes $png
    [ClipboardThemeProbe]::SendMessage($script:h, 0x111, [IntPtr]409, [IntPtr]::Zero) | Out-Null
    Shot '01-dark-selection'
    if (!(Get-Content "$out\config\settings-native.json" -Raw | ConvertFrom-Json).darkMode) { throw 'Dark mode did not enable.' }
    $sequence = [ClipboardThemeProbe]::GetClipboardSequenceNumber()
    Ctrl 0x43 'C'
    Start-Sleep -Seconds 3
    if ([ClipboardThemeProbe]::GetClipboardSequenceNumber() -eq $sequence) { throw 'Dark mode did not copy the selection.' }
    if ((PngBytes ([Windows.Forms.Clipboard]::GetData('PNG'))) -cne $lightPng) { throw 'Clipboard PNG changed with the theme.' }
    Ctrl 0x56 'V'
    Shot '02-pasted'
    Ctrl 0x53 'S'
    $after = Count $file '//stroke'
    "after paste: $after strokes"
    if ($after -lt $strokes + $page1) { throw "the pasted strokes are missing: $after, expected at least $($strokes + $page1)" }

    # 2. a picture of another app
    $picture = New-Object Drawing.Bitmap 120, 80
    $g = [Drawing.Graphics]::FromImage($picture); $g.Clear([Drawing.Color]::CornflowerBlue); $g.Dispose()
    [Windows.Forms.Clipboard]::SetImage($picture)
    "clipboard: a picture of another app ($(Formats))"
    Ctrl 0x56 'V' # replaces the selection (paste clears it first)
    Shot '03-picture-pasted'
    Ctrl 0x53 'S'
    $imagesNow = Count $file '//image'
    "after the picture: $imagesNow images"
    if ($imagesNow -ne $images + 1) { throw "the picture was not pasted as <image>: $imagesNow, expected $($images + 1)" }

    # 3. cut the pasted picture (still selected after the paste), save, paste it again
    Ctrl 0x58 'X'
    Ctrl 0x53 'S'
    if ((Count $file '//image') -ne $images) { throw 'cut did not take the picture away' }
    if ((Formats) -notmatch 'Xournal Studio Clip') { throw "cut did not copy: $(Formats)" }
    Ctrl 0x56 'V'
    Ctrl 0x53 'S'
    Shot '04-cut-and-pasted'
    if ((Count $file '//image') -ne $images + 1) { throw 'the cut picture did not come back with paste' }
    $errors = @(Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue)
    if ($errors.Count -gt 0) { $errors | ForEach-Object { "errors.log: $_" }; throw 'errors in errors.log' }
    'clipboard: passed'
}

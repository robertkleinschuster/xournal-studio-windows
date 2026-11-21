# Verifies the display palette of a PDF background and an embedded PDF, plus ordinary PNG preservation.
. C:\sbx\TestKit.ps1

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PdfThemeProbe {
    [DllImport("user32")] public static extern IntPtr SendMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
}
"@

function Write-Pdf($file, $drawing) {
    $objects = @(
        '<< /Type /Catalog /Pages 2 0 R >>',
        '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << >> /Contents 4 0 R >>',
        "<< /Length $($drawing.Length) >>`nstream`n$drawing`nendstream"
    )
    $content = "%PDF-1.4`n"
    $offsets = @()
    foreach ($object in $objects) {
        $offsets += $content.Length
        $content += "$($offsets.Count) 0 obj`n$object`nendobj`n"
    }
    $xref = $content.Length
    $content += "xref`n0 5`n0000000000 65535 f `n"
    foreach ($offset in $offsets) { $content += ('{0:D10} 00000 n ' -f $offset) + "`n" }
    $content += "trailer`n<< /Size 5 /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
    [IO.File]::WriteAllBytes($file, [Text.Encoding]::ASCII.GetBytes($content))
}

function Read-Palette($name) {
    $bitmap = [Drawing.Bitmap]::new("$out\$name.png")
    try {
        $result = @{ Paper = 0; White = 0; Red = 0; Pink = 0; Blue = 0; PaleBlue = 0; Magenta = 0 }
        for ($x = 0; $x -lt $bitmap.Width; $x += 3) {
            for ($y = [int]($bitmap.Height * 0.16); $y -lt $bitmap.Height * 0.86; $y += 3) {
                $p = $bitmap.GetPixel($x, $y)
                if ($p.R -eq 21 -and $p.G -eq 22 -and $p.B -eq 25) { $result.Paper++ }
                if ($p.R -eq 255 -and $p.G -eq 255 -and $p.B -eq 255) { $result.White++ }
                if ($p.R -eq 255 -and $p.G -lt 10 -and $p.B -lt 10) { $result.Red++ }
                if ($p.R -eq 255 -and $p.G -ge 115 -and $p.G -le 145 -and $p.B -ge 115 -and $p.B -le 145) { $result.Pink++ }
                if ($p.B -eq 255 -and $p.G -lt 10 -and $p.R -lt 10) { $result.Blue++ }
                if ($p.B -eq 255 -and $p.R -ge 170 -and $p.R -le 205 -and $p.G -ge 170 -and $p.G -le 205) { $result.PaleBlue++ }
                if ($p.B -eq 255 -and $p.R -eq 255 -and $p.G -eq 0) { $result.Magenta++ }
            }
        }
        $result
    } finally { $bitmap.Dispose() }
}

Invoke-Test {
    New-Item -ItemType Directory -Force "$out\config", "$out\project" | Out-Null
    Write-Pdf "$out\project\Background.pdf" '1 0 0 rg 100 610 130 90 re f 0 0 0 rg 100 570 130 15 re f'
    Write-Pdf "$out\project\Embedded.pdf" '0 0 1 rg 30 30 535 782 re f'
    $pdf = [Convert]::ToBase64String([IO.File]::ReadAllBytes("$out\project\Embedded.pdf"))
    $sample = [Drawing.Bitmap]::new(30, 30)
    $graphics = [Drawing.Graphics]::FromImage($sample)
    try {
        $graphics.Clear([Drawing.Color]::White)
        $graphics.FillRectangle([Drawing.Brushes]::Magenta, 10, 10, 10, 10)
        $sample.Save("$out\project\Unchanged.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $sample.Dispose() }
    $png = [Convert]::ToBase64String([IO.File]::ReadAllBytes("$out\project\Unchanged.png"))
    $file = "$out\project\DarkPdf.xopp"
    $xml = "<xournal creator=`"xournalpp 1.2.3`" fileversion=`"4`"><title>PDF palette</title><page width=`"595`" height=`"842`"><background type=`"pdf`" domain=`"absolute`" filename=`"Background.pdf`" pageno=`"1`"/><layer><teximage left=`"290`" top=`"135`" right=`"405`" bottom=`"295`" natural_size=`"595 842`">$pdf</teximage><image left=`"410`" top=`"155`" right=`"495`" bottom=`"240`" natural_size=`"30 30`">$png</image></layer></page></xournal>"
    $stream = [IO.File]::Create($file)
    $compressed = [IO.Compression.GZipStream]::new($stream, [IO.Compression.CompressionMode]::Compress)
    $writer = [IO.StreamWriter]::new($compressed, [Text.UTF8Encoding]::new($false))
    try { $writer.Write($xml) } finally { $writer.Dispose() }

    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    @{ paneOpen = $false; darkMode = $true; projectFolder = "$out\project"; mouseHintShown = $true } |
        ConvertTo-Json -Compress | Set-Content "$out\config\settings-native.json" -Encoding ascii
    Start-TestApp 'XournalStudio.exe' ('"' + $file + '"')
    Start-Sleep -Seconds 4
    Shot '01-dark-pdf-and-embedded-pdf'
    $dark = Read-Palette '01-dark-pdf-and-embedded-pdf'
    "Dark: white=$($dark.White), paper=$($dark.Paper), red=$($dark.Red), pink=$($dark.Pink), blue=$($dark.Blue), pale blue=$($dark.PaleBlue)"
    if ($dark.Paper -lt 5000 -or $dark.Pink -lt 200 -or $dark.PaleBlue -lt 200) { throw 'Dark PDF background or embedded PDF is missing its recolored content.' }
    [PdfThemeProbe]::SendMessage($script:h, 0x111, [IntPtr]409, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 2
    Shot '02-original-pdf-and-embedded-pdf'
    $light = Read-Palette '02-original-pdf-and-embedded-pdf'
    "Light: white=$($light.White), paper=$($light.Paper), red=$($light.Red), blue=$($light.Blue)"
    if ($light.White -lt 5000 -or $light.Red -lt 200 -or $light.Blue -lt 200) { throw 'Original PDF background or embedded PDF colors did not restore.' }
    if ($dark.White -lt 300 -or $dark.Magenta -lt 40 -or $dark.Magenta -ne $light.Magenta) { throw 'The ordinary PNG changed with the theme.' }
}

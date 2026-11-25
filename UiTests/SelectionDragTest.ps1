# Moving a big selection stays fast: a generated document with 800 strokes on page 1; Strg+A selects them, a mouse drag
# inside the frame moves them. The drag's ink is composited whatever the size (InkPages: SelectionController, "drag ink
# preview ready after N ms", "moving composited ink preview"); the live-ink path ("previewed N strokes, slowest update")
# must not run, the snapshot must be ready quickly, and the strokes must really have moved (saved file).
#   UiTests\Run-AppTest.ps1 -Test UiTests\SelectionDragTest.ps1
. C:\sbx\TestKit.ps1

function New-DragDocument($file, $count) {
    $random = New-Object Random 7
    $strokes = for ($i = 0; $i -lt $count; $i++) {
        $x = 40 + $random.NextDouble() * 500; $y = 40 + $random.NextDouble() * 740
        $points = for ($j = 0; $j -lt 12; $j++) {
            $x += $random.NextDouble() * 12 - 6; $y += $random.NextDouble() * 12 - 6
            '{0:0.0} {1:0.0}' -f $x, $y
        }
        '<stroke tool="pen" color="#000000ff" width="1.41">{0}</stroke>' -f ($points -join ' ')
    }
    $xml = '<xournal creator="test" fileversion="4"><title>Drag</title><page width="595" height="842">' +
        '<background type="solid" color="#ffffffff" style="plain"/><layer>' + ($strokes -join '') + '</layer></page></xournal>'
    $stream = [IO.File]::Create($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Compress)
        try { $bytes = [Text.Encoding]::UTF8.GetBytes($xml); $gzip.Write($bytes, 0, $bytes.Length) } finally { $gzip.Dispose() }
    } finally { $stream.Dispose() }
}

function FirstPoint($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        $doc = New-Object Xml.XmlDocument
        $doc.LoadXml((New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)).ReadToEnd())
        $first = ($doc.SelectNodes('//stroke')[0].InnerText.Trim() -split '\s+')
        [double[]]@([double]$first[0], [double]$first[1], $doc.SelectNodes('//stroke').Count)
    } finally { $stream.Dispose() }
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    $file = 'C:\work\Drag.xopp'
    New-DragDocument $file 800
    $before = FirstPoint $file
    "document: $($before[2]) strokes, first point $($before[0]) $($before[1])"
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    New-Item -ItemType Directory -Force $env:XOURNAL_STUDIO_CONFIG_DIR | Out-Null
    Start-TestApp 'XournalStudio.exe' "`"$file`""
    Start-Sleep 3
    $cx = [int]([Inj]::W / 2); $cy = [int]([Inj]::H / 2)
    [Inj]::MoveMouse($cx, $cy)

    Ctrl 0x41 'A' | Out-Null
    Shot '01-selected'
    $steps = 60
    $path = @(); 0..$steps | ForEach-Object { $path += ($cx + 120 * $_ / $steps); $path += ($cy + 40 * $_ / $steps) }
    [Inj]::MouseDrag([double[]]$path)
    Start-Sleep 2
    Shot '02-moved'

    $log = "$out\app.log.inkpages.log"
    $ready = Select-String $log -Pattern 'drag ink preview ready after ([\d.]+) ms' | Select-Object -Last 1
    if (-not $ready) { throw "the composited drag preview was never ready (see $log)" }
    $milliseconds = [double]$ready.Matches[0].Groups[1].Value
    "composited preview ready after $milliseconds ms"
    if ($milliseconds -gt 1500) { throw "the composited preview took $milliseconds ms to prepare" }
    if (-not (Select-String $log -Pattern 'moving composited ink preview' -Quiet)) { throw 'the composited ink never moved' }
    if (Select-String $log -Pattern 'previewed \d+ strokes, slowest update' -Quiet) { throw 'the live-ink preview ran for a Move drag' }

    Ctrl 0x53 'S' | Out-Null
    Start-Sleep 2
    $after = FirstPoint $file
    "first point after the drag: $($after[0]) $($after[1])"
    if ($after[2] -ne $before[2]) { throw "the stroke count changed: $($before[2]) -> $($after[2])" }
    if ([Math]::Abs($after[0] - $before[0]) -lt 20 -and [Math]::Abs($after[1] - $before[1]) -lt 5) { throw 'the strokes did not move' }
    'selection drag: done'
}

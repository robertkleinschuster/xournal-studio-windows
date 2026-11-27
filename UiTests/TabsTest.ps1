# Tabs in the app (Windows Sandbox / GitHub runner): two documents open in tabs (restored from the settings: the way a
# session continues), switching with Strg+Tab keeps a stroke drawn in the first one (saved afterwards, checked in the
# file), Strg+W closes a tab without a question when it has no changes, and closing the window remembers the tabs.
#   UiTests\Run-AppTest.ps1 -Test UiTests\TabsTest.ps1 -Extra <uiaprobe.exe>
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
function Read-Xopp($file) {
    $stream = [IO.File]::OpenRead($file)
    try {
        $gzip = New-Object IO.Compression.GZipStream($stream, [IO.Compression.CompressionMode]::Decompress)
        (New-Object IO.StreamReader($gzip, [Text.Encoding]::UTF8)).ReadToEnd()
    } finally { $stream.Dispose() }
}
function Strokes($file) {
    $doc = New-Object Xml.XmlDocument
    $doc.LoadXml((Read-Xopp $file))
    $doc.SelectNodes('/xournal/page/layer/stroke').Count
}
function Title { $script:p.Refresh(); $script:p.MainWindowTitle }
function Expect-Title($label, $name) {
    $title = Title
    Write-Host "${label}: window title '$title'"
    if ($title -notlike "*$name*") { throw "${label}: the shown document is not $name (title '$title')" }
}

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    Copy-Item C:\sbx\data\Schaltungen.xopp C:\work\Erste.xopp
    Copy-Item C:\sbx\data\Schaltungen.xopp C:\work\Zweite.xopp
    Copy-Item C:\sbx\data\Schaltungen.pdf C:\work
    $first = 'C:\work\Erste.xopp'; $second = 'C:\work\Zweite.xopp'
    $before = Strokes $first

    # A session that ended with two tabs: Zweite on the left, Erste shown.
    $config = "$out\config"
    New-Item -ItemType Directory -Force $config | Out-Null
    $settings = @{ openFiles = @($second); lastFile = $first } | ConvertTo-Json
    [IO.File]::WriteAllText("$config\settings-native.json", $settings, (New-Object Text.UTF8Encoding($false)))
    $env:XOURNAL_STUDIO_CONFIG_DIR = $config
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    Start-TestApp 'XournalStudio.exe'
    Start-Sleep 5
    Shot '01-two-tabs'
    & C:\sbx\data\uiaprobe.exe XournalStudioWindow | Out-File "$out\uia-tree.txt" -Encoding utf8
    Expect-Title 'restored' 'Erste'

    # A stroke in Erste, to Zweite and back: the stroke is still there and is saved.
    Stroke 150 300 450 330
    Expect-Title 'before switching' 'Erste'
    Ctrl 0x09 'Tab'
    Expect-Title 'after Strg+Tab' 'Zweite'
    Shot '02-second-tab'
    Ctrl 0x09 'Tab'
    Expect-Title 'back' 'Erste'
    if ((Title) -notlike '*' + [char]0x2022 + '*') { throw 'Erste lost its unsaved mark while it was in the background' }
    Ctrl 0x53 'S'
    $after = Strokes $first
    Write-Host "strokes before $before, after $after"
    if ($after -ne $before + 1) { throw "the stroke did not survive the tab switch: $after strokes, expected $($before + 1)" }

    # Strg+W closes the shown tab (no changes now: no question); Zweite is shown.
    Ctrl 0x57 'W'
    Expect-Title 'after closing' 'Zweite'
    Shot '03-one-tab-left'

    # Closing the window remembers the tab that is open.
    $script:p.CloseMainWindow() | Out-Null
    $script:p.WaitForExit(15000) | Out-Null
    if (-not $script:p.HasExited) { throw 'the window did not close' }
    $saved = Get-Content "$config\settings-native.json" -Raw | ConvertFrom-Json
    Write-Host "settings after closing: lastFile $($saved.lastFile), openFiles $($saved.openFiles -join ', ')"
    if ($saved.lastFile -ne $second) { throw "lastFile is $($saved.lastFile), expected $second" }
    Get-Content "$config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

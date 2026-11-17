# A barrel drag of a selection down to the view's edge and held (auto-scroll, hand-over to the next page), three times
# in a row, after the shape tool's options were open - to find when such a drag ends right after its press.
. C:\sbx\TestKit.ps1
$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $SECOND = 0x20; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
$BARREL = 0x1
$held = $INRANGE -bor $INCONTACT -bor $FIRST -bor $SECOND -bor $PRIMARY
function Frame($x, $y, $flags) { [Inj]::PenFrame($x, $y, $flags, $BARREL, 512); Start-Sleep -Milliseconds 8 }
function Hold($x, $y, $ms) { for ($t = 0; $t -lt $ms; $t += 16) { [Inj]::PenFrame($x, $y, $held -bor $UPDATE, $BARREL, 512); Start-Sleep -Milliseconds 16 } }

function Drag($n) {
    [Inj]::PenStroke((L 400 250 600 250), $false, 512)
    [Inj]::PenStrokeFlags((L 390 235 610 235 610 265 390 265 390 235), $BARREL, 512)
    $x = 500; $y0 = 250; $edge = [Inj]::H - 12
    for ($t = 0; $t -lt 300; $t += 16) { [Inj]::PenFrame($x, $y0 - 1, $INRANGE -bor $UPDATE -bor $PRIMARY, $BARREL, 0); Start-Sleep -Milliseconds 16 }
    Frame $x $y0 ($held -bor $DOWN)
    for ($y = $y0; $y -le $edge; $y += 4) { Frame $x $y ($held -bor $UPDATE) }
    Hold $x $edge 1500
    for ($y = $edge; $y -ge $edge - 160; $y -= 4) { Frame $x $y ($held -bor $UPDATE) }
    [Inj]::PenFrame($x, $edge - 160, $INRANGE -bor $UP -bor $PRIMARY, $BARREL, 0)
    [Inj]::PenFrame($x, $edge - 160, $UPDATE -bor $PRIMARY, 0, 0)
    Shot "drag-$n"
}

Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    Drag 1
    Button 'ShapeButton'; Button 'ShapeButton'; Escape
    Button 'PenButton1'
    Drag 2
    Drag 3
}

# The library's tools in the demo app, with synthetic pen input (Run-SandboxTest.ps1 -Test this; the demo published as
# -App). Screenshots per step in out\; check them visually. Page 1 of the demo spans about x 205..790, y 90.. (client
# DIPs; pixel = 34 + 1.5 * DIP vertically, 1.5 * DIP horizontally).
. C:\sbx\TestKit.ps1

# POINTER_FLAGs / PEN_FLAGs (see Inj.cs)
$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $SECOND = 0x20; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
$BARREL = 0x1
$held = $INRANGE -bor $INCONTACT -bor $FIRST -bor $SECOND -bor $PRIMARY
function Frame($x, $y, $flags) { [Inj]::PenFrame($x, $y, $flags, $BARREL, 512); Start-Sleep -Milliseconds 8 }
function Hold($x, $y, $ms) { for ($t = 0; $t -lt $ms; $t += 16) { [Inj]::PenFrame($x, $y, $held -bor $UPDATE, $BARREL, 512); Start-Sleep -Milliseconds 16 } }
# A real pen hovers before it touches; over the selection the library takes it there (moving is XAML pointer input).
function HoverAt($x, $y, $ms = 100) { for ($t = 0; $t -lt $ms; $t += 16) { [Inj]::PenFrame($x, $y, 0x22002, 1, 0); Start-Sleep -Milliseconds 16 } }

Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    Shot '01-start'

    # Stroke eraser (Windows ink): the touched stroke goes. The pen's eraser end too.
    [Inj]::PenStroke((L 300 130 550 130), $false, 512)
    Button 'InkToolbarEraserButton'
    [Inj]::PenStroke((L 420 115 420 145), $false, 512)
    Shot '02-stroke-eraser'
    Button 'PenButton1'
    [Inj]::PenStroke((L 300 170 550 170), $false, 512)
    [Inj]::PenStroke((L 420 155 420 185), $true, 512)
    Shot '03-eraser-end'

    # Barrel button with the pen active: lasso, then a barrel drag inside the selection moves it; the pen still inks;
    # the delete button removes the selection.
    [Inj]::PenStroke((L 580 220 720 220), $false, 512)
    [Inj]::PenStrokeFlags((L 570 205 735 205 735 235 570 235 570 205), 1, 512)
    Shot '04-barrel-lasso'
    HoverAt 650 219
    [Inj]::PenStrokeFlags((L 650 220 650 270), 1, 512)
    Shot '05-barrel-moved'
    [Inj]::PenStroke((L 300 300 450 300), $false, 512)
    Shot '06-pen-still-inks'
    Button 'DeleteSelectionButton'; Shot '07-selection-deleted'

    # Selection tool: lasso; the corner handle scales; undo puts it back.
    Button 'SelectButton'
    [Inj]::PenStroke((L 290 285 465 285 465 315 290 315 290 285), $false, 512)
    Shot '08-selected'
    # Duplicate: a copy one grid step right and down, selected; its corner scales it; undo takes the scale back.
    Button 'DuplicateSelectionButton'
    Shot '09-duplicated'
    HoverAt 472 322
    [Inj]::PenStroke((L 472 322 570 345), $false, 512)
    Shot '10-scaled'
    Button 'UndoButton'; Shot '11-scale-undone'

    # Point eraser: exactly the circle at the pen; the circle shows while hovering; undo.
    Button 'PenButton1'
    [Inj]::PenStroke((L 300 380 550 380), $false, 512)
    Button 'PointEraserButton'
    [Inj]::PenStroke((L 420 365 420 395), $false, 512)
    for ($k = 0; $k -lt 6; $k++) { [Inj]::PenFrame(470 + 3 * $k, 380, 0x22002, 0, 0) }
    [Inj]::Shot("$out\12-point-erased-circle.png"); "shot 12-point-erased-circle"
    [Inj]::PenFrame(490, 380, 0x22000, 0, 0)
    Button 'UndoButton'; Shot '13-point-erase-undone'

    # The eraser end follows the eraser chosen last (the point eraser): both touches cut; the tip hovering gives the pen
    # back to the ink.
    Button 'PenButton1'
    [Inj]::PenStroke((L 300 420 600 420), $false, 512)
    [Inj]::PenStroke((L 380 405 380 435), $true, 512)
    [Inj]::PenStroke((L 500 405 500 435), $true, 512)
    for ($k = 0; $k -lt 4; $k++) { [Inj]::PenFrame(300 + 3 * $k, 450, 0x22002, 0, 0) }
    [Inj]::PenStroke((L 300 450 600 450), $false, 512)
    Shot '14-eraser-end-partial-then-ink'
    Button 'InkToolbarEraserButton'   # back to whole strokes for the eraser end
    Button 'PenButton1'

    # Shapes: an arrow with the shape tool; with recognition on, drawn shapes become clean ones.
    Button 'ShapeButton'
    ClickInOptions 'ShapeButton' 'Arrow'; ClickAnywhere 'Recognize drawn shapes'; Escape
    [Inj]::PenStroke((L 250 480 400 500), $false, 512)
    Button 'PenButton1'
    [Inj]::PenStroke((L 450 470 560 472 559 520 451 518 450 471), $false, 512)
    $c = @(); for ($k = 0; $k -le 36; $k++) { $t = 2 * [Math]::PI * $k / 36; $c += 660 + 45 * [Math]::Cos($t) + 2 * [Math]::Sin(5 * $t); $c += 495 + 30 * [Math]::Sin($t) }
    [Inj]::PenStroke([double[]]$c, $false, 512); Start-Sleep 2
    Shot '15-shapes-and-recognized'
    Button 'ShapeButton'; ClickInOptions 'ShapeButton' 'Recognize drawn shapes'; Escape
    Button 'PenButton1'

    # A barrel drag down to the view's edge, held: the pages scroll, the content goes to the next page, and moved back up
    # it is right under the pen.
    [Inj]::PenStroke((L 400 250 600 250), $false, 512)
    [Inj]::PenStrokeFlags((L 390 235 610 235 610 265 390 265 390 235), $BARREL, 512)
    $x = 500; $y0 = 250; $edge = [Inj]::H - 12
    for ($t = 0; $t -lt 300; $t += 16) { [Inj]::PenFrame($x, $y0 - 1, $INRANGE -bor $UPDATE -bor $PRIMARY, $BARREL, 0); Start-Sleep -Milliseconds 16 }
    Frame $x $y0 ($held -bor $DOWN)
    for ($y = $y0; $y -le $edge; $y += 4) { Frame $x $y ($held -bor $UPDATE) }
    Hold $x $edge 2500
    [Inj]::Shot("$out\16-drag-held.png"); "shot 16-drag-held"
    for ($y = $edge; $y -ge $edge - 160; $y -= 4) { Frame $x $y ($held -bor $UPDATE) }
    Hold $x ($edge - 160) 600
    [Inj]::Shot("$out\17-drag-moved-up.png"); "shot 17-drag-moved-up (pen at y=$($edge - 160))"
    [Inj]::PenFrame($x, $edge - 160, $INRANGE -bor $UP -bor $PRIMARY, $BARREL, 0)
    [Inj]::PenFrame($x, $edge - 160, $UPDATE -bor $PRIMARY, 0, 0)
    Shot '18-drag-released'
    Button 'UndoButton'; Shot '19-drag-undone'
}

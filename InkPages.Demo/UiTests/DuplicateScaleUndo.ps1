# Reproduces: duplicate a selected stroke, scale the copy at its corner, undo once - the copy must stay (unscaled).
. C:\sbx\TestKit.ps1
Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    [Inj]::PenStroke((L 300 300 450 300), $false, 512)
    Button 'SelectButton'
    [Inj]::PenStroke((L 290 285 465 285 465 315 290 315 290 285), $false, 512)
    Button 'DuplicateSelectionButton'
    Shot '01-duplicated'
    for ($t = 0; $t -lt 100; $t += 16) { [Inj]::PenFrame(472, 322, 0x22002, 0, 0); Start-Sleep -Milliseconds 16 }  # a real pen hovers first
    [Inj]::PenStroke((L 472 322 570 345), $false, 512)
    Shot '02-scaled'
    Button 'UndoButton'; Shot '03-undone'
    Button 'UndoButton'; Shot '04-undone-twice'
    Copy-Item "$out\demo.log" "$out\demo-copy.log" -ErrorAction SilentlyContinue
}

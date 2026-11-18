# Highlighter transparency: a pen stroke, then a highlighter stroke across it; the pen stroke must stay visible
# under the highlighter (Run-SandboxTest.ps1 -Test this, the demo published as -App).
. C:\sbx\TestKit.ps1

Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    [Inj]::PenStroke((L 300 150 550 150), $false, 512)
    [Inj]::PenStroke((L 300 190 550 190), $false, 512)
    Button 'HighlighterButton1'
    [Inj]::PenStroke((L 280 170 570 170 570 160 280 160), $false, 512)
    [Inj]::PenStroke((L 420 120 420 220), $false, 512)
    Shot '01-highlighter-over-pen'
    Button 'PenButton1'
    [Inj]::PenStroke((L 300 260 550 260), $false, 512)
    Button 'HighlighterButton2'
    [Inj]::PenStroke((L 420 240 420 280), $false, 512)
    Shot '02-pen-then-highlighter'
}

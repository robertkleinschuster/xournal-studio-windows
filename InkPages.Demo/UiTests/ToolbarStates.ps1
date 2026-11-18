# Screenshots of the toolbar in every state, light and dark: each tool active, each tool's options open, a selection
# (its commands enabled), the ruler shown. For reviewing the design (Run-SandboxTest.ps1 -Test this -Exe InkPages.Demo.exe).
. C:\sbx\TestKit.ps1

function States([string]$theme) {
    Start-TestApp 'InkPages.Demo.exe' "--$theme"
    Shot "$theme-01-start"
    Button 'PenButton1'; Shot "$theme-02-pen-options"; Escape
    Button 'PenButton2'; Shot "$theme-03-pen2-active"
    Button 'HighlighterButton1'; Shot "$theme-04-highlighter-active"
    Button 'HighlighterButton1'; Shot "$theme-05-highlighter-options"; Escape
    Button 'InkToolbarEraserButton'; Shot "$theme-06-eraser-active"
    Button 'InkToolbarEraserButton'; Shot "$theme-07-eraser-options"; Escape
    Button 'WholeStrokeEraseButton'; Shot "$theme-08-erase-whole-strokes-on"
    RightClick 'WholeStrokeEraseButton'; Shot "$theme-09-eraser-size"; Escape
    Button 'SelectButton'; Shot "$theme-10-select-active"
    Button 'SelectButton'; Shot "$theme-11-select-options"; Escape
    Button 'ShapeButton'; Shot "$theme-12-shapes-active"
    Button 'ShapeButton'; Shot "$theme-13-shapes-options"; Escape
    Button 'StencilButton'; Shot "$theme-14-ruler"
    Button 'StencilButton'; Shot "$theme-15-ruler-options"; Escape
    Button 'StencilButton'
    # A stroke, selected with the lasso: the selection's commands are enabled.
    Button 'PenButton1'
    [Inj]::PenStroke((L 300 300 500 320), $false, 512)
    Button 'SelectButton'
    [Inj]::PenStroke((L 280 280 520 280 520 340 280 340 280 280), $false, 512)
    Shot "$theme-16-selection"
    Stop-TestApp
}

Invoke-Test {
    States 'light'
    States 'dark'
}

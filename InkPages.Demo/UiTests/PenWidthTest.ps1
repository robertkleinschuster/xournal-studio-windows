# Pen widths: each step of the pen flyout's width slider draws one of PenWidths' widths (step 1 = 0.42, thinner than
# the slider's whole numbers) - at full pressure, as Xournal's widths mean it. One line per step, top to bottom (Run-SandboxTest.ps1 -Test this, the demo as -App).
. C:\sbx\TestKit.ps1

Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    for ($step = 1; $step -le 6; $step++) {
        Button 'PenButton1'   # the active pen: opens its flyout
        Start-Sleep -Milliseconds 600
        $slider = Find 'PenStrokeWidthSlider'
        if ($slider -eq $null) { throw 'no width slider in the pen flyout' }
        $slider.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue($step)
        if ($step -eq 1) { Shot '01-flyout-step-1' }
        Escape
        $y = 140 + 40 * $step
        [Inj]::PenStroke((L 300 $y 600 $y), $false, 1024)   # full pressure: exactly the step's width
    }
    Shot '02-steps-1-to-6'
}

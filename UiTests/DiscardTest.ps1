# Explorer: deleting the open document's PDF, and discarding the local changes of the open document (git).
# Run-SandboxTest.ps1 -Test this -Extra <a git repository with a committed test.xopp + its PDF>.
# Screenshots per step; the folder's files and the document's content are written to the transcript.
. C:\sbx\TestKit.ps1

function Alive($step) { if ($script:p.HasExited) { throw "app exited after $step (exit code $($script:p.ExitCode))" }; "alive after $step" }
function Files($step) { "files after ${step}: " + ((Get-ChildItem 'C:\work' -File | ForEach-Object Name) -join ', ') }
function Hash($file) { (Get-FileHash $file -Algorithm SHA256).Hash.Substring(0, 12) }

# Opens a tree row's context menu and clicks one of its items (searched as a menu item: the toolbar has buttons with the
# same names).
function MenuOnRow($row, $item) {
    RightClick $row
    $cond = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $item)),
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::MenuItem)))
    $e = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if ($e -eq $null) { Shot "menu-without-$item"; throw "menu item '$item' not found" }
    $r = $e.Current.BoundingRectangle
    [Inj]::ClickPixel([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)); Start-Sleep -Milliseconds 900
    "menu $row -> $item"
}

Invoke-Test {
    $work = 'C:\work'
    robocopy 'C:\sbx\extra' $work /E /NFL /NDL /NJH /NJS | Out-Null
    attrib +h "$work\.git"
    New-Item -ItemType Directory -Force 'C:\cfg' | Out-Null
    Set-Content 'C:\cfg\settings.json' '{"ExplorerOpen":true,"ProjectFolder":"C:\\work"}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = 'C:\cfg'; $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    $committed = Hash "$work\test.xopp"
    "committed test.xopp: $committed"

    # 1. Discard: write on the document, save it (modified in git), discard -> the committed content, opened again.
    Start-TestApp 'XournalStudio.exe' "`"$work\test.xopp`""
    Start-Sleep 4
    [Inj]::PenStroke((L 700 300 900 340), $false, 600); Start-Sleep 1
    [Inj]::Ctrl(0x53); Start-Sleep 2   # Ctrl+S
    "saved test.xopp: $(Hash "$work\test.xopp")"
    Start-Sleep 2
    Shot '01-modified'
    MenuOnRow 'test.xopp' 'Änderungen verwerfen'
    Shot '02-discard-question'
    Button 'PrimaryButton'; Start-Sleep 3
    Shot '03-discarded'
    "after discard test.xopp: $(Hash "$work\test.xopp") (committed $committed)"
    Alive 'discard'

    # 2. A new stroke (unsaved), then discard: the leave question comes first ("Nicht speichern").
    [Inj]::PenStroke((L 700 400 900 440), $false, 600); Start-Sleep 1
    "unsaved stroke, file: $(Hash "$work\test.xopp")"

    # 3. Deleting the PDF of the open document.
    $pdf = (Get-ChildItem $work -Filter *.pdf | Select-Object -First 1).Name
    MenuOnRow $pdf 'Löschen'
    Shot '04-delete-question'
    Button 'PrimaryButton'; Start-Sleep 1
    Shot '05-leave-question'
    Button 'SecondaryButton'; Start-Sleep 3
    Shot '06-pdf-deleted'
    Files 'deleting the open PDF'
    Alive 'delete pdf'
    Copy-Item 'C:\cfg\errors.log' $out -ErrorAction SilentlyContinue
}

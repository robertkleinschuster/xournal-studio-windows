# The explorer with the open document in Windows Sandbox: discard the changes of the open document (git: committed
# content back, opened again), rename the open document with F2 (the leave question, then opened from its new place),
# delete the open document's PDF (to the recycle bin; the document is left), a new folder named in place, Als Pfad
# kopieren. The repository comes from the host (New-TestRepository.ps1: the sandbox has no git).
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\ExplorerTest.ps1 -Extra (native\UiTests\New-TestRepository.ps1)
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
function Hash($file) { (Get-FileHash $file -Algorithm SHA256).Hash.Substring(0, 12) }
function Title { $script:p.Refresh(); $script:p.MainWindowTitle }
function Files { (Get-ChildItem 'C:\work' -Force | Where-Object Name -ne '.git' | ForEach-Object Name) -join ', ' }
# A tree row's context menu, then one of its items (a Win32 popup menu: searched from the desktop).
function MenuOnRow($row, $item) {
    RightClick $row | Out-Null
    ClickAnywhere $item | Out-Null
    Start-Sleep -Milliseconds 900
    "menu $row -> $item"
}
function WaitFor($what, [scriptblock]$condition) {
    for ($i = 0; $i -lt 20 -and -not (& $condition); $i++) { Start-Sleep -Milliseconds 500 }
    if (-not (& $condition)) { Shot "failed-$what"; throw "timed out: $what" }
}

Invoke-Test {
    $work = 'C:\work'
    Expand-Archive 'C:\sbx\data\work.zip' $work
    attrib +h "$work\.git"
    New-Item -ItemType Directory -Force "$out\config" | Out-Null
    Set-Content "$out\config\settings-native.json" '{"paneOpen":true,"paneTab":0,"projectFolder":"C:\\work"}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    $committed = Hash "$work\test.xopp"
    "committed test.xopp: $committed; files: $(Files)"

    Start-TestApp 'XournalStudio.exe' "`"$work\test.xopp`""
    Start-Sleep 3
    if ((Find 'test.xopp') -eq $null) { Shot 'no-tree'; throw 'the explorer does not show test.xopp' }
    Shot '01-explorer'

    # 1. Discard: a stroke, saved (modified in git: blue), then Änderungen verwerfen -> the committed content, reopened.
    Stroke 600 300 850 330
    [Inj]::Ctrl(0x53); Start-Sleep 3   # Strg+S
    "saved: $(Hash "$work\test.xopp")"
    Shot '02-modified-blue'
    MenuOnRow 'test.xopp' 'Änderungen verwerfen'
    Shot '03-discard-question'
    ClickAnywhere 'Verwerfen'
    WaitFor 'discard' { (Hash "$work\test.xopp") -eq $committed }
    Start-Sleep 2
    "after discard: $(Hash "$work\test.xopp"), title: $(Title)"
    if ((Title) -like '•*') { throw 'the reopened document must be unchanged' }
    Shot '04-discarded'

    # 2. Rename the open document (changed) with F2: the leave question, then it opens from its new place.
    Stroke 600 400 850 430
    Button 'test.xopp' | Out-Null
    [Inj]::Key(0x71); Start-Sleep -Milliseconds 800   # F2
    [System.Windows.Forms.SendKeys]::SendWait('Umbenannt'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}'); Start-Sleep 1
    Shot '05-leave-question'
    ClickAnywhere 'Nicht speichern'
    WaitFor 'rename' { Test-Path "$work\Umbenannt.xopp" }
    Start-Sleep 2
    "after rename: files $(Files), title: $(Title)"
    if ((Title) -notlike '*Umbenannt*') { throw "the renamed document is not open (title: $(Title))" }
    if (Test-Path "$work\test.xopp") { throw 'the old name is still there' }
    Shot '06-renamed'

    # 3. Delete the open document's PDF: asked, the document is left (it uses the PDF), the PDF goes to the recycle bin.
    MenuOnRow 'Schaltungen.pdf' 'Löschen'
    Shot '07-delete-question'
    ClickAnywhere 'Löschen'
    WaitFor 'delete' { -not (Test-Path "$work\Schaltungen.pdf") }
    Start-Sleep 1
    "after delete: files $(Files), title: $(Title)"
    if ((Title) -notlike '*Notiz*') { throw "the document using the PDF must be left (title: $(Title))" }
    Shot '08-pdf-deleted'

    # 4. A new folder, named in place (label edit).
    MenuOnRow 'todo.txt' 'Neuer Ordner'
    [System.Windows.Forms.SendKeys]::SendWait('Übung 1'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'new folder' { Test-Path "$work\Übung 1" }
    "after new folder: files $(Files)"
    Shot '09-new-folder'

    # 5. Als Pfad kopieren: the path in quotes, as File Explorer gives it.
    MenuOnRow 'todo.txt' 'Als Pfad kopieren'
    $clip = Get-Clipboard
    "clipboard: $clip"
    if ($clip -ne '"C:\work\todo.txt"') { throw "the clipboard holds $clip" }

    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

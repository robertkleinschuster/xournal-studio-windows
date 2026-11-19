# The explorer with the open document in Windows Sandbox: discard the changes of the open document (git: committed
# content back, opened again), rename the open document with F2 (the leave question, then opened from its new place),
# delete the open document's PDF (to the recycle bin; the document is left), a new folder named in place, Als Pfad
# kopieren. The repository comes from the host (New-TestRepository.ps1: the sandbox has no git).
#   UiTests\Run-AppTest.ps1 -Test UiTests\ExplorerTest.ps1 -Extra (UiTests\New-TestRepository.ps1),
#       D:\src\InkPages\.local-build\tools\uiaprobe.exe
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
# comctl32 controls and Win32 menus through the native UI Automation client (InkPages' uiaprobe, in C:\sbx\data): the
# managed client of Windows PowerShell does not see their items. ASCII parts of names (the probe matches substrings).
function At($class, $text) {
    for ($i = 0; $i -lt 10; $i++) {
        $r = & C:\sbx\data\uiaprobe.exe $class $text
        if ($LASTEXITCODE -eq 0) { $p = "$r".Trim() -split ' '; return @([int]$p[0], [int]$p[1]) }
        Start-Sleep -Milliseconds 300
    }
    Shot ("missing-" + ($text -replace '[^A-Za-z0-9]', '_')) | Out-Null
    throw "'$text' not found in a $class window"
}
function ClickAt($class, $text) { $a = At $class $text; [Inj]::ClickPixel($a[0], $a[1]); Start-Sleep -Milliseconds 700; "click $text" }
function RightClickAt($class, $text) { $a = At $class $text; [Inj]::RightClickPixel($a[0], $a[1]); Start-Sleep -Milliseconds 700; "right-click $text" }
# A tree row's context menu, then one of its items.
function MenuOnRow($row, $item) {
    RightClickAt 'XournalStudioWindow' $row | Out-Null
    ClickAt '#32768' $item | Out-Null
    Start-Sleep -Milliseconds 600
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
    At 'XournalStudioWindow' 'test.xopp' | Out-Null   # the tree shows the folder
    Shot '01-explorer'

    # 1. Discard: a stroke, saved (modified in git: blue), then Änderungen verwerfen -> the committed content, reopened.
    Stroke 600 300 850 330
    [Inj]::Ctrl(0x53); Start-Sleep 3   # Strg+S
    "saved: $(Hash "$work\test.xopp")"
    Shot '02-modified-blue'
    MenuOnRow 'test.xopp' 'verwerfen'
    Shot '03-discard-question'
    ClickAnywhere 'Verwerfen'
    WaitFor 'discard' { (Hash "$work\test.xopp") -eq $committed }
    Start-Sleep 2
    "after discard: $(Hash "$work\test.xopp"), title: $(Title)"
    if ((Title) -like '•*') { throw 'the reopened document must be unchanged' }
    Shot '04-discarded'

    # 2. Rename the open document (changed) with F2: the leave question, then it opens from its new place.
    Stroke 600 400 850 430
    ClickAt 'XournalStudioWindow' 'test.xopp' | Out-Null
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

    # 3. Rename the open document's PDF (F2): the document refers to the new name and counts as changed (saved by choice).
    ClickAt 'XournalStudioWindow' 'Schaltungen.pdf' | Out-Null
    [Inj]::Key(0x71); Start-Sleep -Milliseconds 800   # F2
    [System.Windows.Forms.SendKeys]::SendWait('Skript'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'pdf rename' { Test-Path "$work\Skript.pdf" }
    Start-Sleep 1
    "after pdf rename: files $(Files), title: $(Title)"
    if ((Title) -notlike '•*') { throw "renaming the document's PDF must mark it changed (title: $(Title))" }
    Shot '07-pdf-renamed'

    # 4. Delete the open document's PDF: asked, the document is left (it uses the PDF; changed: the leave question,
    # Nicht speichern), the PDF goes to the recycle bin.
    MenuOnRow 'Skript.pdf' 'schen'
    Shot '08-delete-question'
    ClickAnywhere 'Löschen'
    Start-Sleep 1
    ClickAnywhere 'Nicht speichern'
    WaitFor 'delete' { -not (Test-Path "$work\Skript.pdf") }
    Start-Sleep 1
    "after delete: files $(Files), title: $(Title)"
    if ((Title) -notlike '*Notiz*') { throw "the document using the PDF must be left (title: $(Title))" }
    Shot '09-pdf-deleted'

    # 5. A new folder, named in place (label edit).
    MenuOnRow 'todo.txt' 'Neuer Ordner'
    [System.Windows.Forms.SendKeys]::SendWait('Übung 1'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'new folder' { Test-Path "$work\Übung 1" }
    "after new folder: files $(Files)"
    Shot '10-new-folder'

    # 6. Als Pfad kopieren: the path in quotes, as File Explorer gives it.
    MenuOnRow 'todo.txt' 'Als Pfad'
    $clip = Get-Clipboard
    "clipboard: $clip"
    if ($clip -ne '"C:\work\todo.txt"') { throw "the clipboard holds $clip" }

    # 7. New document and folder inside a subfolder: from the folder's menu (right-clicked beside its name - the whole
    # row is the folder) the document goes into the folder; from a file's menu the folder goes into the file's folder.
    $at = At 'XournalStudioWindow' 'UE'
    [Inj]::RightClickPixel($at[0] + 150, $at[1]); Start-Sleep -Milliseconds 700
    ClickAt '#32768' 'Neues Dokument' | Out-Null
    Start-Sleep 1
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')   # keep the suggested name
    WaitFor 'new document in UE' { @(Get-ChildItem "$work\UE" -Filter *.xopp).Count -eq 1 }
    if (@(Get-ChildItem $work -Filter '*-Notiz-*.xopp').Count -ne 0) { throw 'the new document landed in the project folder' }
    "after new document in UE: $((Get-ChildItem "$work\UE" | ForEach-Object Name) -join ', ')"
    Shot '11-document-in-subfolder'
    MenuOnRow 'blatt.txt' 'Neuer Ordner'
    [System.Windows.Forms.SendKeys]::SendWait('Teil 2'); Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    WaitFor 'new folder in UE' { Test-Path "$work\UE\Teil 2" }
    if (Test-Path "$work\Teil 2") { throw 'the new folder landed in the project folder' }
    "after new folder in UE: $((Get-ChildItem "$work\UE" | ForEach-Object Name) -join ', ')"
    Shot '12-folder-in-subfolder'

    Get-Content "$out\config\errors.log" -ErrorAction SilentlyContinue | ForEach-Object { "errors.log: $_" }
}

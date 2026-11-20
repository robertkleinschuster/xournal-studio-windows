# The app as a PDF viewer with mouse and keyboard: a generated five-page PDF ("Seite 1".."Seite 5"; on page 1 a link to
# page 4). The status bar shows "Seite n von m" and the zoom; Ende/Pos1/Bild ab scroll (logged: whether DirectManipulation
# took the key); Strg+0/1/2 zoom (Ganze Seite, 100 %, Seitenbreite); Strg+G goes to a page, Alt+Links/Alt+Rechts go back
# and forward; the mouse over the link shows the hand and a tooltip "Seite 4", a click follows it, the mouse's back
# button returns; a middle-button drag pans; a right click opens the pages' menu; Datei -> Zuletzt geöffnet lists the
# PDF; closed and opened again, the PDF comes back at its last position. Checked in the app's log (status, navigation).
#   UiTests\Run-AppTest.ps1 -Test UiTests\PdfViewerTest.ps1 -Extra <uiaprobe.exe>
. C:\sbx\TestKit.ps1

# Mouse buttons and keys TestKit has no helper for: the middle and side buttons, Alt+key (SendInput, at the cursor).
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Threading;
public static class ViewerInput
{
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; public uint pad1, pad2; }
    [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public int type; public UNION u; }
    [DllImport("user32", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    static void Send(INPUT i)
    {
        if (SendInput(1, new[] { i }, Marshal.SizeOf(typeof(INPUT))) != 1) throw new Exception("SendInput failed: " + Marshal.GetLastWin32Error());
        Thread.Sleep(30);
    }
    public static void Button(uint flags, uint data)
    {
        var i = new INPUT(); i.type = 0; i.u.mi.dwFlags = flags; i.u.mi.mouseData = data; Send(i);
    }
    static void KeyEvent(ushort vk, bool up, bool extended)
    {
        var i = new INPUT(); i.type = 1; i.u.ki.vk = vk; i.u.ki.flags = (up ? 2u : 0u) | (extended ? 1u : 0u); Send(i);
    }
    public static void Alt(ushort vk)
    {
        KeyEvent(0x12, false, false); KeyEvent(vk, false, true); KeyEvent(vk, true, true); KeyEvent(0x12, true, false);
    }
}
"@

function Ctrl($k, $label) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 1200; "ctrl+$label" }
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 1000; "key 0x{0:X}" -f $k }
function Alt($k, $label) { [ViewerInput]::Alt($k); Start-Sleep -Milliseconds 1200; "alt+$label" }
$script:log = "$out\app.log"
function LogCount($pattern) { if (-not (Test-Path $script:log)) { return 0 }; @(Get-Content $script:log | Where-Object { $_ -match $pattern }).Count }
# Waits until the log has the line `$times` times.
function Expect($pattern, $what, $times = 1) {
    for ($i = 0; $i -lt 20 -and (LogCount $pattern) -lt $times; $i++) { Start-Sleep -Milliseconds 500 }
    if ((LogCount $pattern) -lt $times) { throw "not in the log ($times x): $what ($pattern)" }
    "ok: $what"
}
# The page the status bar shows now (its last "Seite n von m" line).
function StatusPage {
    $line = @(Get-Content $script:log | Where-Object { $_ -match 'status: Seite (\d+) von' }) | Select-Object -Last 1
    if ($line -match 'status: Seite (\d+) von') { [int]$Matches[1] } else { 0 }
}
function ExpectPage($page, $what) {
    for ($i = 0; $i -lt 20 -and (StatusPage) -ne $page; $i++) { Start-Sleep -Milliseconds 500 }
    if ((StatusPage) -ne $page) { throw "status bar on page $(StatusPage), expected $page`: $what" }
    "ok: $what (Seite $page)"
}
function KeyLines { Get-Content $script:log | Where-Object { $_ -match 'viewport: key|viewer: key' } | Select-Object -Last 4 }

# A PDF written by hand: objects, then the cross-reference table with their byte offsets.
function New-TestPdf($file) {
    $page = '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents {0} 0 R{1} >>'
    $objects = @(
        '<< /Type /Catalog /Pages 2 0 R >>',
        '<< /Type /Pages /Kids [4 0 R 5 0 R 6 0 R 7 0 R 8 0 R] /Count 5 >>',
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
        ($page -f 9, ' /Annots [14 0 R]'), ($page -f 10, ''), ($page -f 11, ''), ($page -f 12, ''), ($page -f 13, ''),
        (Stream 'BT /F1 36 Tf 72 700 Td (Seite 1) Tj ET'),
        (Stream 'BT /F1 36 Tf 72 700 Td (Seite 2) Tj ET'),
        (Stream 'BT /F1 36 Tf 72 700 Td (Seite 3) Tj ET'),
        (Stream 'BT /F1 36 Tf 72 700 Td (Seite 4) Tj ET'),
        (Stream 'BT /F1 36 Tf 72 700 Td (Seite 5) Tj ET'),
        '<< /Type /Annot /Subtype /Link /Rect [0 380 612 680] /Border [0 0 1] /Dest [7 0 R /XYZ 0 792 0] >>'
    )
    $text = "%PDF-1.4`n"
    $offsets = @()
    for ($i = 0; $i -lt $objects.Count; $i++) {
        $offsets += [Text.Encoding]::ASCII.GetByteCount($text)
        $text += "$($i + 1) 0 obj`n$($objects[$i])`nendobj`n"
    }
    $xref = [Text.Encoding]::ASCII.GetByteCount($text)
    $text += "xref`n0 $($objects.Count + 1)`n0000000000 65535 f `n"
    foreach ($o in $offsets) { $text += ('{0:D10} 00000 n ' -f $o) + "`n" }
    $text += "trailer`n<< /Size $($objects.Count + 1) /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
    [IO.File]::WriteAllBytes($file, [Text.Encoding]::ASCII.GetBytes($text))
}
function Stream($content) { "<< /Length $($content.Length) >>`nstream`n$content`nendstream" }

Invoke-Test {
    New-Item -ItemType Directory -Force C:\work | Out-Null
    New-TestPdf 'C:\work\Viewer.pdf'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = $script:log
    Start-TestApp 'XournalStudio.exe' '"C:\work\Viewer.pdf"'
    Start-Sleep 3
    ExpectPage 1 'the status bar shows the first page'
    Expect 'status: \d+ %' 'the status bar shows the zoom'
    Shot '01-status-bar'

    # Keys on the pages (the pages have the keyboard after a click beside the link).
    [Inj]::Click(500, 60); Start-Sleep 1
    Key 0x23  # Ende
    ExpectPage 5 'Ende goes to the last page'
    Key 0x24  # Pos1
    ExpectPage 1 'Pos1 goes to the first page'
    Key 0x22; Key 0x22; Key 0x22  # Bild ab
    if ((StatusPage) -lt 2) { throw "three times Bild ab stayed on page $(StatusPage)" }
    "ok: Bild ab scrolls (Seite $(StatusPage))"
    Key 0x20  # Leertaste
    Key 0x28  # Pfeil ab
    KeyLines
    Shot '02-keys'

    # Zoom: Ganze Seite, Originalgröße (100 %), Seitenbreite.
    Ctrl 0x30 '0'
    Shot '03-whole-page'
    Ctrl 0x31 '1'
    Expect 'status: 100 %' 'Strg+1 shows the real size'
    Ctrl 0x32 '2'
    Shot '04-page-width'

    # Gehe zu Seite 4, then back and forward.
    $before = StatusPage
    Ctrl 0x47 'G'
    Shot '05-go-to-page'
    [Inj]::TypeText('4'); Start-Sleep -Milliseconds 300
    Key 0x0D
    Expect 'go to page 4 of 5' 'Gehe zu Seite 4'
    ExpectPage 4 'on page 4'
    Alt 0x25 'Links'
    Expect 'navigation: back to page' 'Alt+Links goes back'
    for ($i = 0; $i -lt 10 -and (StatusPage) -eq 4; $i++) { Start-Sleep -Milliseconds 500 }
    if ((StatusPage) -eq 4) { throw "Alt+Links stayed on page 4 (before the jump: page $before)" }
    "ok: back where the view was (Seite $(StatusPage), before the jump $before)"
    Alt 0x27 'Rechts'
    Expect 'navigation: forward to page 4' 'Alt+Rechts goes forward'
    ExpectPage 4 'forward on page 4 again'

    # The link: hand and tooltip, a click follows it, the mouse's back button returns.
    Key 0x24
    ExpectPage 1 'Pos1'
    Start-Sleep 1
    [Inj]::MoveMouse(500, 280); Start-Sleep -Milliseconds 300
    [Inj]::MoveMouse(504, 284); Start-Sleep -Milliseconds 700
    Expect 'link tooltip: Seite 4' 'the tooltip names the link''s page'
    Shot '06-link-tooltip'
    [Inj]::Click(504, 284); Start-Sleep 1
    Expect 'pdf links: to page 4' 'the click follows the link'
    ExpectPage 4 'the link went to page 4'
    [ViewerInput]::Button(0x80, 1); [ViewerInput]::Button(0x100, 1); Start-Sleep 1  # XBUTTON1 down/up: back
    Expect 'navigation: back to page 1' 'the mouse''s back button returns'
    ExpectPage 1 'back on page 1'

    # Middle-button drag pans the pages.
    [Inj]::MoveMouse(500, 500)
    [ViewerInput]::Button(0x20, 0)  # middle down
    for ($y = 500; $y -ge 150; $y -= 10) { [Inj]::MoveMouse(500, $y) }
    [ViewerInput]::Button(0x40, 0)  # middle up
    Start-Sleep 1
    Expect 'viewer: middle-button pan' 'the middle button pans'
    Expect 'viewer: pan ended' 'the pan ended'
    Shot '07-panned'

    # The pages' context menu.
    [Inj]::MoveMouse(400, 300)
    [ViewerInput]::Button(0x8, 0); [ViewerInput]::Button(0x10, 0); Start-Sleep 1
    Expect 'page menu: shown' 'a right click opens the pages'' menu'
    Shot '08-context-menu'
    Escape

    # Datei -> Zuletzt geöffnet lists the PDF.
    [ViewerInput]::Alt(0x44); Start-Sleep -Milliseconds 800  # Alt+D: Datei
    [Inj]::Key(0x47); Start-Sleep -Milliseconds 800           # g: Zuletzt geöffnet
    Shot '09-recent-files'
    Escape; Escape; Escape

    # Closed (the window's own close: settings saved) and opened again: the PDF at its last position.
    Key 0x23
    ExpectPage 5 'Ende before closing'
    [Inj]::PostMessage($script:h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # WM_CLOSE
    if (-not $script:p.WaitForExit(15000)) { throw 'the app did not close' }
    $settings = Get-Content "$out\config\settings-native.json" -Raw
    if ($settings -notmatch 'recentFiles' -or $settings -notmatch 'Viewer\.pdf') { throw "no recent file in the settings: $settings" }
    if ($settings -notmatch '"page":4') { throw "the last position (page 5) is not in the settings: $settings" }
    "ok: recent file and position saved"
    $script:log = "$out\app2.log"
    $env:XOURNAL_STUDIO_LOG = $script:log
    Start-TestApp 'XournalStudio.exe' '"C:\work\Viewer.pdf"'
    Start-Sleep 3
    Expect 'surface: to page 5' 'the last position is restored'
    ExpectPage 5 'opened again on page 5'
    Shot '10-reopened'
    'pdf viewer: passed'
}

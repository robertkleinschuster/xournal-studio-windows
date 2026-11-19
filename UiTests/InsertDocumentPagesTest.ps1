# Pages of another document (Seite -> Seiten aus PDF oder Dokument einfügen…), through the real file picker (typed
# path), and the message floating over the top bar (closing by itself).
# Run-SandboxTest.ps1 -Test this -Document examples\Schaltungen.xopp -Extra <folder with Grenzwerte.xopp>
# (its PDF Grenzwerte.pdf lies next to Schaltungen.xopp in examples, so the runner copies it).
. C:\sbx\TestKit.ps1
Add-Type -AssemblyName System.Windows.Forms

function Alive($step) { if ($script:p.HasExited) { throw "app exited after $step (exit code $($script:p.ExitCode))" }; "alive after $step" }

# Pages, their PDF page numbers and the PDF's page count of a saved .xopp.
function Describe($xopp, $pdf) {
    $fs = [IO.File]::OpenRead($xopp); $gz = New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionMode]::Decompress)
    $r = New-Object IO.StreamReader($gz); $x = $r.ReadToEnd(); $r.Close()
    $pagenos = ([regex]::Matches($x, 'type="pdf"[^>]*pageno="(\d+)"') | ForEach-Object { $_.Groups[1].Value }) -join ','
    $files = ([regex]::Matches($x, 'filename="([^"]+)"') | ForEach-Object { $_.Groups[1].Value }) -join ','
    $pdfPages = ([regex]::Matches([IO.File]::ReadAllText($pdf, [Text.Encoding]::GetEncoding(28591)), '/Type\s*/Page[^s]')).Count
    "pages=$(([regex]::Matches($x, '<page ')).Count) pdf pagenos=$pagenos filenames=$files pdf pages=$pdfPages"
}

Invoke-Test {
    $work = 'C:\work'; New-Item -ItemType Directory -Force "$work\cfg" | Out-Null
    Copy-Item "$root\data\*" $work -Force
    Copy-Item 'C:\sbx\extra\*' $work -Force
    Set-Content "$work\cfg\settings.json" '{"ExplorerOpen":false,"ProjectFolder":"C:\\work"}'
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$work\cfg"; $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    "before: $(Describe "$work\test.xopp" "$work\Schaltungen.pdf")"
    "source: $(Describe "$work\Grenzwerte.xopp" "$work\Grenzwerte.pdf")"
    Start-TestApp 'XournalStudio.exe' "`"$work\test.xopp`""
    Start-Sleep 4
    Shot '01-open'

    Button 'Seite'
    ClickAnywhere 'Seiten aus PDF oder Dokument einfügen…'
    Start-Sleep 3
    Shot '02-picker'
    [System.Windows.Forms.SendKeys]::SendWait('C:\work\Grenzwerte.xopp')
    Start-Sleep -Milliseconds 500
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    Start-Sleep 6
    Shot '03-dialog-with-thumbnails'
    Button 'Alle auswählen'
    Start-Sleep 1
    Button 'PrimaryButton'
    Start-Sleep 3
    Shot '04-inserted-message'
    Alive 'insert'
    [Inj]::Ctrl(0x53); Start-Sleep 3   # Ctrl+S
    "after: $(Describe "$work\test.xopp" "$work\Schaltungen.pdf")"
    Start-Sleep 4
    Shot '05-message-closed-by-itself'

    # A message that stays: deleting the only page is refused (informational) - then an error-free close.
    Alive 'end'
    Copy-Item "$work\cfg\errors.log" $out -ErrorAction SilentlyContinue
}

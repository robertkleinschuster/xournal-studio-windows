# The data files a UI test needs in C:\sbx\data: examples\Schaltungen.xopp and .pdf for every test, plus what the test
# declares in a comment line of its own, paths relative to the repository, comma-separated:
#   # Data: examples\Grenzwerte.pdf
# Dot-sourced by Run-AppTest.ps1 (Windows Sandbox) and Run-CiTest.ps1 (GitHub runner), so both copy the same files.
function Get-UiTestData([string]$Repo, [string]$Test) {
    $files = @((Join-Path $Repo 'examples\Schaltungen.xopp'), (Join-Path $Repo 'examples\Schaltungen.pdf'))
    foreach ($line in Get-Content $Test) {
        if ($line -match '^#\s*Data:\s*(.+)$') {
            foreach ($part in $Matches[1] -split ',') {
                $file = Join-Path $Repo $part.Trim()
                if (-not (Test-Path $file)) { throw "$Test declares $($part.Trim()), which does not exist" }
                $files += $file
            }
        }
    }
    $files
}

# Fetches the WinUI Release runtime that XournalStudio.csproj copies over the official package's runtime:
# upstream microsoft-ui-xaml main + PR #12292 (readable InkPresenter.UnprocessedInput pointer args) + the InkToolbar custom
# tool states fix + pointer capture kept across PointerRoutedAway/To (branch fix/ink-runtime-capture), built in the fork
# robertkleinschuster/microsoft-ui-xaml and published there as a release asset. See CLAUDE.md, "Current WinUI ink state".
# Pinned by URL and SHA-256; extracted to .local-build\winui-fre. Does nothing when that runtime is already there.
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$url = 'https://github.com/robertkleinschuster/microsoft-ui-xaml/releases/download/winui-ink-fix-3d994d89/winui-runtime-x64fre-3d994d89.zip'
$sha256 = '6AA9D325D31DB6F7A7CEF1D654461435F62BE0000D5736BD8C743F939658D9BE'

$root = Split-Path $PSScriptRoot
$target = Join-Path $root '.local-build\winui-fre'
$marker = Join-Path $target 'runtimes\win10-x64\native\Microsoft.UI.Xaml.dll'
$stamp = Join-Path $target 'source.txt'

if (-not $Force -and (Test-Path $marker) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $sha256)) {
    "WinUI runtime present: $target"
    return
}

$zip = Join-Path ([IO.Path]::GetTempPath()) "winui-runtime-$sha256.zip"
Invoke-WebRequest $url -OutFile $zip -UseBasicParsing
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash
if ($actual -ne $sha256) {
    Remove-Item $zip
    throw "WinUI runtime download has SHA-256 $actual, expected $sha256 ($url)."
}

if (Test-Path $target) { Remove-Item -Recurse -Force $target }
New-Item -ItemType Directory $target | Out-Null
Expand-Archive $zip -DestinationPath $target
Remove-Item $zip
if (-not (Test-Path $marker)) { throw "The downloaded WinUI runtime has no $marker." }
Set-Content $stamp $sha256 -Encoding ascii
"WinUI runtime extracted: $target"

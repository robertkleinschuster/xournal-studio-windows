# Builds Xournal Studio for Windows and installs it for the current user:
#   %LOCALAPPDATA%\Programs\Xournal Studio for Windows  +  Start menu entry "Xournal Studio for Windows".
# No file associations are registered. An installation of the earlier name ("Xournal Studio WinUI") is removed.
param([switch]$SkipInstall, [switch]$SkipTests)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$publish = Join-Path $root 'XournalStudio\bin\publish'
$target = Join-Path $env:LOCALAPPDATA 'Programs\Xournal Studio for Windows'
$programs = [Environment]::GetFolderPath('Programs')

if (-not $SkipTests) {
    dotnet test (Join-Path $root 'XournalStudio.Tests') -nologo -v q
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}

if (Test-Path $publish) { Remove-Item -Recurse -Force $publish }
dotnet publish (Join-Path $root 'XournalStudio\XournalStudio.csproj') -c Release -o $publish -nologo -v q
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

if ($SkipInstall) { "Published to $publish"; return }

$running = Get-Process XournalStudio -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$target\*" }
if ($running) { throw 'Xournal Studio for Windows is running; close it first or use -SkipInstall.' }

if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse $publish $target

# Earlier name of this app.
$old = Join-Path $env:LOCALAPPDATA 'Programs\Xournal Studio WinUI'
if (Test-Path $old) { Remove-Item -Recurse -Force $old }
Remove-Item (Join-Path $programs 'Xournal Studio (WinUI).lnk') -ErrorAction SilentlyContinue

$shortcut = Join-Path $programs 'Xournal Studio for Windows.lnk'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = Join-Path $target 'XournalStudio.exe'
$link.WorkingDirectory = $target
$link.IconLocation = (Join-Path $target 'XournalStudio.exe') + ',0'
$link.Save()
"Installed to $target"

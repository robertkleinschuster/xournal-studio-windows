# Builds the git repository the explorer's sandbox test works in (the sandbox has no git): test.xopp + its PDF
# (examples\Schaltungen.*) and a todo.txt, committed, zipped. Returns the zip's path. Uses Git for Windows on the host.
#   UiTests\Run-AppTest.ps1 -Test UiTests\ExplorerTest.ps1 -Extra (UiTests\New-TestRepository.ps1)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$base = Join-Path $repo '.local-build\test-repository'
$work = Join-Path $base 'work'
$zip = Join-Path $base 'work.zip'
if (Test-Path $base) { Remove-Item -Recurse -Force $base }
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item (Join-Path $repo 'examples\Schaltungen.xopp') (Join-Path $work 'test.xopp')
Copy-Item (Join-Path $repo 'examples\Schaltungen.pdf') $work
New-Item -ItemType Directory -Force (Join-Path $work 'UE') | Out-Null
Set-Content (Join-Path $work 'UE\blatt.txt') 'Aufgabenblatt' -Encoding ascii
Set-Content (Join-Path $work 'todo.txt') "2026-11-15 Erste Aufgabe due:2026-11-20`n2026-11-15 Zweite Aufgabe" -NoNewline -Encoding ascii
# (Not named "Git": PowerShell would call the function itself for "git".)
function Invoke-Git { & git.exe -C $work @args; if ($LASTEXITCODE) { throw "git $args failed ($LASTEXITCODE)" } }
Invoke-Git init --quiet
Invoke-Git -c user.name=Test -c user.email=test@example.com add -A
Invoke-Git -c user.name=Test -c user.email=test@example.com commit --quiet -m start
Invoke-Git config user.name Test
Invoke-Git config user.email test@example.com
# The zip holds the folder's content (also .git); the test unpacks it to C:\work.
Compress-Archive -Path (Join-Path $work '*'), (Join-Path $work '.git') -DestinationPath $zip
$zip

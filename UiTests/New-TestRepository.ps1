# Builds the git repository the explorer's sandbox test works in (the sandbox has no git): test.xopp + its PDF
# (examples\Schaltungen.*) and a todo.txt, committed, zipped. Its upstream is a bare repository (in the zip as
# remote.git; the test moves it to C:\remote.git) with two commits the clone has not fetched yet, and the clone has one
# commit not pushed - the sync button shows "↑1 ↓2" after the app's fetch. Returns the zip's path. Uses Git for Windows
# on the host.
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
$branch = (& git.exe -C $work rev-parse --abbrev-ref HEAD).Trim()
# The bare upstream, pushed from the host's path; then two commits there that the clone does not know yet.
$remote = Join-Path $base 'remote.git'
& git.exe init --quiet --bare $remote; if ($LASTEXITCODE) { throw 'git init --bare failed' }
Invoke-Git push --quiet $remote "${branch}:${branch}"
$other = Join-Path $base 'other'
& git.exe clone --quiet $remote $other; if ($LASTEXITCODE) { throw 'git clone failed' }
foreach ($n in 1, 2) {
    Set-Content (Join-Path $other "remote$n.txt") "vom anderen Gerät $n" -Encoding ascii
    & git.exe -C $other add -A; & git.exe -C $other -c user.name=Other -c user.email=other@example.com commit --quiet -m "remote $n"
    if ($LASTEXITCODE) { throw 'git commit (other) failed' }
}
& git.exe -C $other push --quiet origin $branch; if ($LASTEXITCODE) { throw 'git push (other) failed' }
# The clone tracks it where the sandbox will have it, with the state before the two commits as its last fetch.
Invoke-Git remote add origin 'C:/remote.git'
Invoke-Git update-ref "refs/remotes/origin/$branch" HEAD
Invoke-Git config "branch.$branch.remote" origin
Invoke-Git config "branch.$branch.merge" "refs/heads/$branch"
# One local commit, not pushed (a file the explorer test does not use).
Set-Content (Join-Path $work 'lokal.txt') 'nur hier' -Encoding ascii
Invoke-Git add lokal.txt
Invoke-Git commit --quiet -m lokal
Move-Item $remote (Join-Path $work 'remote.git')
# The zip holds the folder's content (also .git and remote.git); the test unpacks it to C:\work.
Compress-Archive -Path (Join-Path $work '*'), (Join-Path $work '.git') -DestinationPath $zip
$zip

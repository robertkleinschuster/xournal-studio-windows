# The staged app of a green CI run (.github/workflows/build.yml uploads it on every push to main as the artifact
# app-<sha>: XournalStudio.exe, pdfium.dll, licenses\ - the layout install.ps1 installs), downloaded with the GitHub
# CLI (gh). Artifacts are kept 5 days.
#   . (Join-Path $repo 'tools\CiArtifact.ps1')
#   $run = Get-XournalCiApp -Repo $repo -Destination <folder> [-Commit <rev>]
# Without -Commit: the newest successful push run on main (CI builds only the newest commit of a burst, and docs-only
# pushes build nothing, so origin/main itself may have no run). With -Commit: a revision of the local repository
# (after git fetch); the newest successful run for exactly that commit. Fails with the reason when there is none.

function Invoke-XournalGh {
    $ErrorActionPreference = 'Continue' # gh writes progress to stderr; its exit code decides
    $output = & gh.exe @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "gh $args failed ($LASTEXITCODE): $($output -join "`n")" }
    $output
}

function Invoke-XournalGitText([string]$Repo) {
    $ErrorActionPreference = 'Continue'
    $output = & git.exe -C $Repo @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "git $args failed ($LASTEXITCODE): $($output -join "`n")" }
    $output
}

function Get-XournalCiApp([string]$Repo, [string]$Destination, [string]$Commit = '') {
    Invoke-XournalGitText $Repo fetch --quiet origin | Out-Null
    $fields = 'databaseId,headSha,displayTitle,createdAt,url'
    Push-Location $Repo # gh finds the GitHub repository from the checkout's origin
    try {
        if ($Commit) {
            $sha = "$(Invoke-XournalGitText $Repo rev-parse --verify "$Commit^{commit}")".Trim()
            $json = Invoke-XournalGh run list --workflow build.yml --commit $sha --event push --status success --json $fields --limit 1
            $what = "commit $sha"
        }
        else {
            $json = Invoke-XournalGh run list --workflow build.yml --branch main --event push --status success --json $fields --limit 1
            $what = 'main'
        }
        $runs = @(($json -join "`n") | ConvertFrom-Json)
        if ($runs.Count -eq 0) {
            $recent = Invoke-XournalGh run list --workflow build.yml --limit 5 --json 'headSha,status,conclusion,url'
            throw "no successful CI push run for $what (artifacts are kept 5 days); recent runs: $($recent -join ' ')"
        }
        $run = $runs[0]
        if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
        New-Item -ItemType Directory -Force $Destination | Out-Null
        Invoke-XournalGh run download $run.databaseId --name "app-$($run.headSha)" --dir $Destination | Out-Null
    }
    finally { Pop-Location }
    if (-not (Test-Path (Join-Path $Destination 'XournalStudio.exe'))) {
        throw "the artifact app-$($run.headSha) of run $($run.databaseId) has no XournalStudio.exe ($Destination)"
    }
    Write-Host "CI app: run $($run.databaseId) ($($run.url)), commit $($run.headSha.Substring(0, 7)) - $($run.displayTitle)"
    if (-not $Commit) {
        $behind = "$(Invoke-XournalGitText $Repo rev-list --count "$($run.headSha)..origin/main")".Trim()
        if ($behind -ne '0') {
            Write-Host "  origin/main is $behind commit(s) further (being built, cancelled by a newer push, or docs only)"
        }
    }
    $run
}

# The staged app of a green CI run (.github/workflows/build.yml uploads it on every push to main as the artifact
# app-<sha>: XournalStudio.exe, pdfium.dll, licenses\ - the layout install.ps1 installs; the upload step runs only after
# the build and the unit tests passed). Found and downloaded through GitHub's artifacts API (gh api for the listing,
# the artifact's zip by its id - each run attempt has its own artifacts). Artifacts are kept 5 days.
#   . (Join-Path $repo 'tools\CiArtifact.ps1')
#   $app = Get-XournalCiApp -Repo $repo -Destination <folder> [-Commit <rev>]
# Without -Commit: the newest app artifact of a run on main (CI builds only the newest commit of a burst, docs-only
# pushes build nothing, so origin/main itself may have none). With -Commit: a revision of the local repository (after
# git fetch); the newest artifact app-<that sha>. Fails with the reason when there is none.

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
    # One tab-separated line per artifact (the jq filter has no quotes: Windows PowerShell passes embedded quotes to
    # native programs unescaped); filtered and sorted here.
    $tsv = '.artifacts[] | [.id, .name, .expired, .workflow_run.id, .workflow_run.head_sha, .workflow_run.head_branch, .created_at] | @tsv'
    Push-Location $Repo # gh finds the GitHub repository from the checkout's origin
    try {
        if ($Commit) {
            $sha = "$(Invoke-XournalGitText $Repo rev-parse --verify "$Commit^{commit}")".Trim()
            $what = "commit $sha"
            $lines = Invoke-XournalGh api "repos/{owner}/{repo}/actions/artifacts?name=app-$sha&per_page=20" --jq $tsv
        }
        else {
            $what = 'main'
            $lines = Invoke-XournalGh api 'repos/{owner}/{repo}/actions/artifacts?per_page=100' --jq $tsv
        }
        $artifacts = @($lines | Where-Object { "$_".Trim() } | ForEach-Object {
                $f = "$_".Trim() -split "`t"
                [pscustomobject]@{ Id = $f[0]; Name = $f[1]; Expired = $f[2]; RunId = $f[3]; Sha = $f[4]; Branch = $f[5]; Created = $f[6] }
            } | Where-Object { $_.Name -like 'app-*' -and $_.Expired -eq 'false' -and ($Commit -or $_.Branch -eq 'main') } |
            Sort-Object Created -Descending)
        if ($artifacts.Count -eq 0) {
            throw ("no app artifact of a green CI run for $what (CI uploads app-<sha> after the build and the tests " +
                'passed; kept 5 days; a run that is being re-run has none until it finishes)')
        }
        $id = $artifacts[0].Id; $runId = $artifacts[0].RunId; $headSha = $artifacts[0].Sha; $created = $artifacts[0].Created
        $token = "$(Invoke-XournalGh auth token)".Trim()
        $remote = "$(Invoke-XournalGh repo view --json nameWithOwner --jq .nameWithOwner)".Trim()
    }
    finally { Pop-Location }

    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    $zip = Join-Path $Destination 'artifact.zip'
    # The artifact's zip by its id (GitHub redirects to its storage; the token is not sent there).
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -UseBasicParsing -Uri "https://api.github.com/repos/$remote/actions/artifacts/$id/zip" `
        -Headers @{ Authorization = "Bearer $token"; Accept = 'application/vnd.github+json' } -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $Destination
    Remove-Item $zip
    if (-not (Test-Path (Join-Path $Destination 'XournalStudio.exe'))) {
        throw "the artifact app-$headSha ($id) of run $runId has no XournalStudio.exe ($Destination)"
    }
    $url = "https://github.com/$remote/actions/runs/$runId"
    Write-Host "CI app: artifact app-$headSha ($id, $created) of run $runId ($url)"
    if (-not $Commit) {
        $behind = "$(Invoke-XournalGitText $Repo rev-list --count "$headSha..origin/main")".Trim()
        if ($behind -ne '0') {
            Write-Host "  origin/main is $behind commit(s) further (being built, cancelled by a newer push, failed, or docs only)"
        }
    }
    [pscustomobject]@{ ArtifactId = $id; RunId = $runId; HeadSha = $headSha; Url = $url }
}

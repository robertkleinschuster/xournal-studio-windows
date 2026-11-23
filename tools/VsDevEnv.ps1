# Visual Studio for the build scripts: the installation (vswhere), its CMake/CTest/Ninja, and the compiler environment
# of its x64 developer prompt (Microsoft's DevShell module) - the Ninja generator calls cl.exe directly, which needs
# that environment (INCLUDE, LIB, PATH).
#   . (Join-Path $PSScriptRoot 'tools\VsDevEnv.ps1')
#   $vs = Enter-XournalVsDevEnv      # once per process; the environment stays for the rest of the script

function Get-XournalVs {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    if (-not $vs) { throw 'Visual Studio not found (vswhere)' }
    $vs
}

function Get-XournalCMake([string]$Vs, [string]$Tool = 'cmake') {
    Join-Path $Vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$Tool.exe"
}

function Get-XournalNinja([string]$Vs) {
    Join-Path $Vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}

function Enter-XournalVsDevEnv {
    $vs = Get-XournalVs
    if (-not $env:VSCMD_VER) {
        Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
        Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
        if (-not $env:VSCMD_VER) { throw "Enter-VsDevShell did not set up the developer environment ($vs)" }
    }
    $vs
}

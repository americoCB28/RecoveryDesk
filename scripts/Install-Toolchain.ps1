#Requires -RunAsAdministrator
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$privateRoot = Join-Path $ProjectRoot '.tools'
$statusPath = Join-Path $privateRoot 'install-status.json'
New-Item -ItemType Directory -Path $privateRoot -Force | Out-Null
function Write-Status([string]$Stage, [string]$State, [string]$Detail = '') {
    @{stage=$Stage; state=$State; detail=$Detail; timestamp=[DateTime]::UtcNow.ToString('o')} |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
}
function Install-Verified([string]$Name, [string]$Arguments) {
    $file = Join-Path $privateRoot "installers\$Name"
    if (-not (Test-Path -LiteralPath $file)) {
        $sources = @{
            'vs_buildtools.exe'='https://aka.ms/vs/17/release/vs_buildtools.exe'
            'adksetup.exe'='https://go.microsoft.com/fwlink/?linkid=2289980'
            'adkwinpesetup.exe'='https://go.microsoft.com/fwlink/?linkid=2289981'
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
        Write-Status $Name 'downloading'
        Invoke-WebRequest -Uri $sources[$Name] -OutFile $file -UseBasicParsing
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $file
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw "Firma Microsoft no valida: $Name" }
    Write-Status $Name 'running'
    $process = Start-Process -FilePath $file -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -notin @(0,3010)) { throw "El instalador $Name termino con codigo $($process.ExitCode). Consulta el log del instalador en TEMP." }
    Write-Status $Name 'installed' "exit=$($process.ExitCode)"
}
Start-Transcript -LiteralPath (Join-Path $privateRoot 'install-toolchain.log') -Append | Out-Null
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = $null
    if (Test-Path -LiteralPath $vswhere) { $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
    if (-not $vsPath) {
        Install-Verified 'vs_buildtools.exe' '--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    }
    $adkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Assessment and Deployment Kit'
    if (-not (Test-Path -LiteralPath (Join-Path $adkRoot 'Deployment Tools\DandISetEnv.bat'))) {
        Install-Verified 'adksetup.exe' '/quiet /norestart /ceip off /features OptionId.DeploymentTools'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $adkRoot 'Windows Preinstallation Environment\amd64\en-us\winpe.wim'))) {
        Install-Verified 'adkwinpesetup.exe' '/quiet /norestart /ceip off /features OptionId.WindowsPreinstallationEnvironment'
    }
    Write-Status 'toolchain' 'complete'
} catch {
    Write-Status 'toolchain' 'failed' $_.Exception.Message
    throw
} finally { Stop-Transcript | Out-Null }

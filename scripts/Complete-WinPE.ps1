#Requires -RunAsAdministrator
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ProjectRoot, [switch]$WaitForInstaller)
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$privateRoot = Join-Path $ProjectRoot '.tools'
$statusPath = Join-Path $privateRoot 'winpe-status.json'
$logPath = Join-Path $privateRoot 'msvc-build.log'
function Write-Status([string]$Stage,[string]$State,[string]$Detail='') {
    @{stage=$Stage;state=$State;detail=$Detail;timestamp=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
}
function Invoke-Native([string]$Executable,[string[]]$Arguments) {
    & $Executable @Arguments 2>&1 | Out-File -LiteralPath $logPath -Encoding UTF8 -Append
    if ($LASTEXITCODE -ne 0) { throw "Fallo de $Executable (codigo $LASTEXITCODE). Consulta $logPath" }
}
try {
    if ($WaitForInstaller) {
        Write-Status 'installer' 'waiting'
        $deadline = [DateTime]::UtcNow.AddHours(1)
        do {
            $install = Get-Content -LiteralPath (Join-Path $privateRoot 'install-status.json') -Raw | ConvertFrom-Json
            if ($install.state -eq 'failed') { throw $install.detail }
            if ($install.stage -eq 'toolchain' -and $install.state -eq 'complete') { break }
            if ([DateTime]::UtcNow -gt $deadline) { throw 'Tiempo de espera del instalador agotado.' }
            Start-Sleep -Seconds 3
        } while ($true)
    }
    Set-Location -LiteralPath $ProjectRoot
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
    if (-not $cmake) { $cmake = Join-Path $privateRoot 'python\cmake\data\bin\cmake.exe' }
    if (-not (Test-Path -LiteralPath $cmake)) { throw 'Instala CMake 3.25 o posterior.' }
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    Write-Status 'msvc' 'building'
    Invoke-Native -Executable $cmake -Arguments @('--preset','msvc-release')
    Invoke-Native -Executable $cmake -Arguments @('--build','--preset','release','--parallel','2')
    Write-Status 'msvc' 'testing'
    Invoke-Native -Executable $ctest -Arguments @('--preset','release')
    Invoke-Native -Executable $cmake -Arguments @('--install','build/msvc','--config','Release','--prefix','dist/RecoveryDesk')
    $appDirectory = Join-Path $ProjectRoot 'dist\RecoveryDesk'
    $python = Get-Command python -ErrorAction Stop | Select-Object -ExpandProperty Source -First 1
    Invoke-Native -Executable $python -Arguments @('scripts/verify_pe.py',(Join-Path $appDirectory 'RecoveryDesk.exe'),'--output',(Join-Path $appDirectory 'pe-dependencies.json'))
    $adkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Assessment and Deployment Kit'
    $artifactRoot = Join-Path $ProjectRoot 'artifacts'
    New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
    $imageName = 'WinPE-validation-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $workDirectory = Join-Path $artifactRoot $imageName
    Write-Status 'winpe' 'building' $workDirectory
    & (Join-Path $ProjectRoot 'boot\Build-WinPE.ps1') -ApplicationDirectory $appDirectory -WorkDirectory $workDirectory -AdkRoot $adkRoot -ValidationImage
    $isoPath = "$workDirectory.iso"
    & (Join-Path $ProjectRoot 'boot\Export-ISO.ps1') -WorkDirectory $workDirectory -AdkRoot $adkRoot -OutputPath $isoPath
    Write-Status 'winpe' 'complete' $isoPath
} catch { Write-Status 'completion' 'failed' $_.Exception.Message; throw }

#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$WorkDirectory,
    [Parameter(Mandatory)][string]$AdkRoot,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
foreach ($pathValue in @($WorkDirectory,$AdkRoot,$OutputPath)) {
    if (-not [IO.Path]::IsPathRooted($pathValue) -or $pathValue -match '[&|<>^%!"\r\n]') { throw 'Ruta absoluta no compatible.' }
}
if (Test-Path -LiteralPath $OutputPath) { throw 'La ISO de destino ya existe; no se sobrescribe.' }
$ready = Get-Content -LiteralPath (Join-Path $WorkDirectory 'recoverydesk-ready.json') -Raw | ConvertFrom-Json
if ($ready.schema_version -ne 1 -or $ready.boot_ca -ne '2023') { throw 'Imagen de trabajo incompatible.' }
$wim = Join-Path $WorkDirectory 'media\sources\boot.wim'
if ((Get-FileHash -LiteralPath $wim -Algorithm SHA256).Hash -ne $ready.wim_sha256) { throw 'El WIM cambio desde su preparacion.' }
$environmentFile = Join-Path $AdkRoot 'Deployment Tools\DandISetEnv.bat'
$makeMedia = Join-Path $AdkRoot 'Windows Preinstallation Environment\MakeWinPEMedia.cmd'
$command = 'call "{0}" && call "{1}" /ISO "{2}" "{3}" /bootex' -f $environmentFile,$makeMedia,$WorkDirectory,$OutputPath
& "$env:SystemRoot\System32\cmd.exe" /d /s /c $command
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutputPath)) { throw 'No se pudo generar la ISO con UEFI CA 2023.' }
@{schema_version=1; boot_ca='2023'; iso_sha256=(Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash; physical_boot_validated=$false} |
    ConvertTo-Json | Set-Content -LiteralPath "$OutputPath.json" -Encoding UTF8

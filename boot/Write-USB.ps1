#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$WorkDirectory,
    [Parameter(Mandatory)][string]$AdkRoot,
    [Parameter(Mandatory)][uint32]$DiskNumber,
    [AllowEmptyString()][string]$ExpectedSerial,
    [Parameter(Mandatory)][string]$ExpectedIdentity,
    [Parameter(Mandatory)][uint64]$ExpectedBytes,
    [Parameter(Mandatory)][ValidatePattern('^[A-Z]:$')][string]$DriveLetter
)
$ErrorActionPreference = 'Stop'
foreach ($pathValue in @($WorkDirectory, $AdkRoot)) {
    if ($pathValue -match '[&|<>^%!"\r\n]' -or -not [IO.Path]::IsPathRooted($pathValue)) { throw 'Ruta de trabajo no admitida.' }
}
$ready = Get-Content -LiteralPath (Join-Path $WorkDirectory 'recoverydesk-ready.json') -Raw | ConvertFrom-Json
if ($ready.schema_version -ne 1 -or $ready.boot_ca -ne '2023') { throw 'Imagen no compatible.' }
$wimPath = Join-Path $WorkDirectory 'media\sources\boot.wim'
if ((Get-FileHash -LiteralPath $wimPath -Algorithm SHA256).Hash -ne $ready.wim_sha256) { throw 'La imagen cambio desde su preparacion.' }
if ((Get-Item -LiteralPath $wimPath).Length -ge 4GB) { throw 'El WIM supera el limite de FAT32. No se ha formateado el USB.' }
$disk = Get-Disk -Number $DiskNumber
if ($disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or $disk.IsOffline -or $disk.IsReadOnly -or $disk.Size -ne $ExpectedBytes) { throw 'Disco protegido o identidad no verificable.' }
if ($ExpectedSerial -and $disk.SerialNumber.Trim() -ne $ExpectedSerial.Trim()) { throw 'El numero de serie cambio.' }
function Assert-DiskIdentity($DiskValue) {
    if ($ExpectedIdentity.StartsWith('mbr-')) {
        if ("mbr-$($DiskValue.Signature)" -ne $ExpectedIdentity) { throw 'La firma MBR no coincide.' }
    } elseif ($ExpectedIdentity.StartsWith('serial-')) {
        if ("serial-$($DiskValue.SerialNumber.Trim())" -ne $ExpectedIdentity) { throw 'La identidad de serie no coincide.' }
    } elseif ($DiskValue.PartitionStyle -ne 'GPT' -or $DiskValue.Guid.ToString().Trim('{}') -ne $ExpectedIdentity.Trim('{}')) {
        throw 'El identificador GPT no coincide.'
    }
}
Assert-DiskIdentity $disk
$partitions = @(Get-Partition -DiskNumber $DiskNumber)
if ($partitions.Count -ne 1) { throw 'El MVP requiere un USB dedicado con una sola particion existente.' }
$partition = $partitions[0]
if ($partition.IsBoot -or $partition.IsSystem -or "$($partition.DriveLetter):" -ne $DriveLetter) { throw 'La particion no corresponde al USB confirmado.' }
if (-not (Test-Path -LiteralPath "$DriveLetter\")) { throw 'No se puede inspeccionar el contenido del USB.' }
if (Test-Path -LiteralPath "$DriveLetter\Windows\System32\config\SYSTEM") { throw 'El USB contiene una instalacion de Windows.' }
$targetVolume = Get-Volume -DriveLetter $partition.DriveLetter
if ($targetVolume.HealthStatus -ne 'Healthy' -or $targetVolume.Size -gt 32GB) { throw 'Se requiere una particion USB accesible y saludable de hasta 32 GB para FAT32.' }
$environmentFile = Join-Path $AdkRoot 'Deployment Tools\DandISetEnv.bat'
$makeMedia = Join-Path $AdkRoot 'Windows Preinstallation Environment\MakeWinPEMedia.cmd'
if (-not ((Get-Content -LiteralPath $makeMedia -Raw) -match '/bootex')) { throw 'ADK sin soporte UEFI CA 2023.' }
Start-Transcript -LiteralPath (Join-Path $WorkDirectory 'write-usb.log') -Append | Out-Null
try {
    # Final independent revalidation immediately before the Microsoft formatter.
    $current = Get-Disk -Number $DiskNumber
    Assert-DiskIdentity $current
    if ($current.UniqueId -ne $disk.UniqueId -or $current.Size -ne $ExpectedBytes -or $current.IsBoot -or $current.IsSystem -or $current.BusType -ne 'USB') { throw 'El dispositivo cambio antes de escribir.' }
    $mapped = Get-Partition -DriveLetter $partition.DriveLetter
    if ($mapped.DiskNumber -ne $DiskNumber) { throw 'La letra de unidad cambio.' }
    $command = 'call "{0}" && call "{1}" /UFD /F "{2}" {3} /bootex' -f $environmentFile, $makeMedia, $WorkDirectory, $DriveLetter
    & "$env:SystemRoot\System32\cmd.exe" /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'MakeWinPEMedia no pudo terminar.' }
    foreach ($fileName in @('sources\boot.wim', 'EFI\Boot\bootx64.efi')) {
        if (-not (Test-Path -LiteralPath (Join-Path "$DriveLetter\" $fileName))) { throw 'El USB no paso la comprobacion final.' }
    }
    if ((Get-FileHash -LiteralPath (Join-Path "$DriveLetter\" 'sources\boot.wim') -Algorithm SHA256).Hash -ne $ready.wim_sha256) { throw 'La copia del WIM no coincide.' }
} finally { Stop-Transcript | Out-Null }

#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ApplicationDirectory,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [Parameter(Mandatory)][string]$AdkRoot
)
$ErrorActionPreference = 'Stop'
function Assert-SafePath([string]$Value) {
    if ($Value -match '[&|<>^%!"\r\n]' -or -not [IO.Path]::IsPathRooted($Value)) { throw 'Usa una ruta absoluta sin caracteres especiales de consola.' }
}
foreach ($pathValue in @($ApplicationDirectory, $WorkDirectory, $AdkRoot)) { Assert-SafePath $pathValue }
if (Test-Path -LiteralPath $WorkDirectory) { throw 'La carpeta de trabajo ya existe. Elige una carpeta nueva.' }
if (-not (Test-Path -LiteralPath (Join-Path $ApplicationDirectory 'RecoveryDesk.exe'))) { throw 'No se encuentra RecoveryDesk.exe.' }
$deploymentEnvironment = Join-Path $AdkRoot 'Deployment Tools\DandISetEnv.bat'
$peRoot = Join-Path $AdkRoot 'Windows Preinstallation Environment'
$components = Join-Path $peRoot 'amd64\WinPE_OCs'
foreach ($component in @('WinPE-WMI', 'WinPE-SecureStartup', 'WinPE-StorageWMI')) {
    if (-not (Test-Path -LiteralPath (Join-Path $components "$component.cab"))) { throw "Falta $component del mismo ADK amd64." }
}
$setupCommand = 'call "{0}" && call "{1}" amd64 "{2}"' -f $deploymentEnvironment, (Join-Path $peRoot 'copype.cmd'), $WorkDirectory
& "$env:SystemRoot\System32\cmd.exe" /d /s /c $setupCommand
if ($LASTEXITCODE -ne 0) { throw 'CopyPE no pudo preparar la imagen.' }
$logPath = Join-Path $WorkDirectory 'build.log'
Start-Transcript -LiteralPath $logPath | Out-Null
$mountPath = Join-Path $WorkDirectory 'mount'
$wimPath = Join-Path $WorkDirectory 'media\sources\boot.wim'
$mounted = $false
try {
    & "$env:SystemRoot\System32\dism.exe" /Mount-Image "/ImageFile:$wimPath" /Index:1 "/MountDir:$mountPath"
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo montar WinPE.' }
    $mounted = $true
    foreach ($component in @('WinPE-WMI', 'WinPE-SecureStartup', 'WinPE-StorageWMI')) {
        foreach ($packagePath in @((Join-Path $components "$component.cab"), (Join-Path $components "en-us\${component}_en-us.cab"))) {
            if (-not (Test-Path -LiteralPath $packagePath)) { throw "Falta paquete: $packagePath" }
            & "$env:SystemRoot\System32\dism.exe" /Image:$mountPath /Add-Package "/PackagePath:$packagePath"
            if ($LASTEXITCODE -ne 0) { throw "No se pudo agregar $component." }
        }
    }
    $payloadPath = Join-Path $mountPath 'RecoveryDesk'
    New-Item -ItemType Directory -Path $payloadPath | Out-Null
    foreach ($name in @('RecoveryDesk.exe', 'data', 'LICENSE', 'NOTICE')) {
        Copy-Item -LiteralPath (Join-Path $ApplicationDirectory $name) -Destination $payloadPath -Recurse
    }
    # Omit the media builder from WinPE: only the native executable and local guides are needed.
    @'
[LaunchApps]
%SYSTEMROOT%\System32\wpeinit.exe
%SYSTEMDRIVE%\RecoveryDesk\RecoveryDesk.exe
'@ | Set-Content -LiteralPath (Join-Path $mountPath 'Windows\System32\winpeshl.ini') -Encoding ASCII
    & "$env:SystemRoot\System32\dism.exe" /Unmount-Image "/MountDir:$mountPath" /Commit
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo confirmar WinPE.' }
    $mounted = $false
    $makeMedia = Join-Path $peRoot 'MakeWinPEMedia.cmd'
    if (-not ((Get-Content -LiteralPath $makeMedia -Raw) -match '/bootex')) { throw 'Se requiere un ADK con soporte /bootex para medios firmados por UEFI CA 2023.' }
    # Store a digest to reject a changed WIM at write time. This is not a Microsoft license grant.
    @{ schema_version = 1; application_version = '0.1.0'; architecture = 'amd64'; boot_ca = '2023'; wim_sha256 = (Get-FileHash -LiteralPath $wimPath -Algorithm SHA256).Hash } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $WorkDirectory 'recoverydesk-ready.json') -Encoding UTF8
} finally {
    if ($mounted) { & "$env:SystemRoot\System32\dism.exe" /Unmount-Image "/MountDir:$mountPath" /Discard }
    Stop-Transcript | Out-Null
}

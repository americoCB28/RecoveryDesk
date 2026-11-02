[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$VmConfiguration,
    [Parameter(Mandatory)][string]$IsoPath,
    [ValidateRange(30,900)][int]$TimeoutSeconds=420
)
$ErrorActionPreference = 'Stop'
$configFile = (Resolve-Path -LiteralPath $VmConfiguration).Path
$directory = Split-Path -Parent $configFile
$config = Get-Content -LiteralPath $configFile -Raw | ConvertFrom-Json
$vbox = Join-Path $env:ProgramFiles 'Oracle\VirtualBox\VBoxManage.exe'
if ($config.name -notmatch '^RecoveryDesk-SecureBoot-[a-f0-9]{10}$' -or -not (Test-Path -LiteralPath $vbox)) { throw 'VM de laboratorio no valida.' }
$machine = & $vbox showvminfo $config.name --machinereadable
if ($LASTEXITCODE -ne 0) { throw 'No se encuentra la VM de laboratorio.' }
$expected = Join-Path $directory "$($config.name)\$($config.name).vbox"
$configLine = $machine | Where-Object { $_ -like 'CfgFile=*' } | Select-Object -First 1
$actual = $configLine.Substring(9).TrimEnd('"').Replace('\\','\')
if ([IO.Path]::GetFullPath($actual) -ne [IO.Path]::GetFullPath($expected)) { throw 'La VM no pertenece a esta carpeta de laboratorio.' }
if ($machine -notcontains 'nic1="none"' -or ($machine | Where-Object { $_ -like 'SharedFolderName*' }) -or
    ($machine | Where-Object { $_ -match '^"SATA-[0-9]+-[0-9]+"=' -and $_ -notmatch '="emptydrive"$' })) { throw 'Se requiere una VM nueva, aislada, sin discos, red ni carpetas de host.' }
$iso = (Resolve-Path -LiteralPath $IsoPath).Path
$metadata = Get-Content -LiteralPath "$iso.json" -Raw | ConvertFrom-Json
if ($metadata.boot_ca -ne '2023' -or (Get-FileHash -LiteralPath $iso -Algorithm SHA256).Hash -ne $metadata.iso_sha256) { throw 'La ISO no coincide con su registro de construccion.' }
$serialPath = Join-Path $directory 'boot-serial.log'
if (Test-Path -LiteralPath $serialPath) { throw 'Ya existe evidencia de otro arranque; utiliza una VM de laboratorio nueva.' }
$resultPath = Join-Path $directory 'validation.json'
$started = $false
try {
    & $vbox storageattach $config.name --storagectl SATA --port 0 --device 0 --type dvddrive --medium $iso
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo adjuntar la ISO.' }
    & $vbox startvm $config.name --type headless
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox no pudo iniciar la VM.' }
    $started = $true
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $proof = $null
    do {
        if (Test-Path -LiteralPath $serialPath) {
            foreach ($line in Get-Content -LiteralPath $serialPath -ErrorAction SilentlyContinue) {
                if ($line -match '(\{[^\r\n]*\})') {
                    try { $candidate = $Matches[1] | ConvertFrom-Json; if ($candidate.stage -eq 'native_gui_started') { $proof = $candidate } } catch { }
                }
            }
        }
        if ($proof) { break }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)
    & $vbox controlvm $config.name screenshotpng (Join-Path $directory 'screenshot.png')
    $passed = $proof -and $proof.winpe -and $proof.secure_boot -eq $true -and $proof.compiler -eq 'MSVC' -and $proof.discovery_complete -and $proof.bitlocker_provider_available
    @{schema_version=1;result=$(if($passed){'passed'}else{'failed'});environment='VirtualBox UEFI';native_evidence=$proof;
      iso_sha256=$metadata.iso_sha256;physical_hardware_tested=$false;dbx_revocation_matrix_tested=$false;captured_at=[DateTime]::UtcNow.ToString('o')} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    if ($passed) { $config.guest_boot_validated = $true; $config | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $configFile -Encoding UTF8 }
    if (-not $passed) { throw "El arranque no produjo todas las evidencias requeridas. Consulta $resultPath y screenshot.png." }
} finally {
    if ($started) { & $vbox controlvm $config.name poweroff }
}

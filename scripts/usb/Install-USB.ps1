# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
# Optional, explicit setup. Installs fixed official signed releases, never updates automatically.
$ErrorActionPreference = 'Stop'
try {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
        Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -ArgumentList $arguments
        exit
    }
    Write-Host 'Sunshine-Moonlight optional USB components (Windows x64)'
    Write-Host 'Installs usbipd-win 5.3.0 and usbip-win2 0.9.8.1 from their official releases.'
    Write-Host 'USB uses the paired TLS tunnel. Direct network access to plain USB/IP port 3240 will be blocked.'
    Write-Host 'This does not connect or force-bind any USB device. Close existing USB/IP sessions before continuing.'
    Write-Host 'Source and licenses: https://github.com/dorssel/usbipd-win and https://github.com/vadimgrn/usbip-win2'
    if ((Read-Host 'Install components and restrict plain USB/IP to loopback? [y/N]') -notmatch '^(y|yes)$') { exit }
    if (![Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { throw 'This package requires Windows x64.' }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $cache = Join-Path $env:TEMP ('SunshineMoonlightUSB-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $cache | Out-Null
    $assets = @(
        @{ Name='usbipd-win_5.3.0_x64.msi'; Url='https://github.com/dorssel/usbipd-win/releases/download/v5.3.0/usbipd-win_5.3.0_x64.msi'; Hash='1c984914aec944de19b64eff232421439629699f8138e3ddc29301175bc6d938' },
        @{ Name='USBip-0.9.8.1-x64.exe'; Url='https://github.com/vadimgrn/usbip-win2/releases/download/v.0.9.8.1/USBip-0.9.8.1-x64.exe'; Hash='38cad6d4432b52d5bb9409d9ad03b72fdffc4ada4cd3a48fbeca1a2752a8518a' }
    )
    foreach ($asset in $assets) {
        $destination = Join-Path $cache $asset.Name
        # An administrator may place an offline copy beside this script. It must
        # match the same fixed hash and valid Authenticode signature as a download.
        $offline = Join-Path $PSScriptRoot $asset.Name
        if (Test-Path -LiteralPath $offline) { Copy-Item -LiteralPath $offline -Destination $destination }
        else { Invoke-WebRequest -UseBasicParsing -Uri $asset.Url -OutFile $destination }
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $asset.Hash) { throw "Checksum mismatch: $($asset.Name)" }
        if ((Get-AuthenticodeSignature -LiteralPath $destination).Status -ne 'Valid') { throw "Signature validation failed: $($asset.Name)" }
    }
    # Preserve existing newer versions. Component installation is user-triggered,
    # never an application update checker or a background installation.
    $exporter = Join-Path $env:ProgramFiles 'usbipd-win\usbipd.exe'
    $importer = Join-Path $env:ProgramFiles 'USBip\usbip.exe'
    $exportVersion = if (Test-Path -LiteralPath $exporter) { (& $exporter --version 2>$null) -join '' } else { '' }
    $importVersion = if (Test-Path -LiteralPath $importer) { (& $importer --version 2>$null) -join '' } else { '' }
    $reboot = $false
    if ($exportVersion -notmatch '(\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'5.3.0') {
        $msi = Join-Path $cache $assets[0].Name
        $process = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList @('/i', ('"' + $msi + '"'), '/passive', '/norestart') -PassThru -Wait -WindowStyle Hidden
        if ($process.ExitCode -notin @(0,3010)) { throw "usbipd installer failed: $($process.ExitCode)" }
        $reboot = $process.ExitCode -eq 3010
    }
    if ($importVersion -notmatch '(\d+\.\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'0.9.8.1') {
        $process = Start-Process -FilePath (Join-Path $cache $assets[1].Name) -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-') -PassThru -Wait -WindowStyle Hidden
        if ($process.ExitCode -notin @(0,3010)) { throw "USBip installer failed: $($process.ExitCode)" }
        $reboot = $reboot -or $process.ExitCode -eq 3010
    }
    # The official exporter normally creates a LAN allow rule. This integration
    # uses only 127.0.0.1:3240 behind mTLS; keep raw USB/IP off the network.
    $ruleName = 'SunshineMoonlight-USB-LoopbackOnly'
    if (!(Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -Name $ruleName -DisplayName 'Sunshine-Moonlight: block direct network USB/IP' -Direction Inbound -Action Block -Protocol TCP -LocalPort 3240 -Profile Any -RemoteAddress @('0.0.0.0-126.255.255.255','128.0.0.0-255.255.255.255','::2-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff') | Out-Null
    }
    Start-Service -Name usbipd
    Write-Host 'USB components installed. Enable USB forwarding in Sunshine, restart Sunshine, then refresh Moonlight USB devices.'
    if ($reboot) { Write-Host 'Windows reported that a restart is required.' }
    Write-Host 'No Secure Boot or test-signing settings were changed. Downloads remain at:' $cache
} catch { Write-Host $_.Exception.Message -ForegroundColor Red }
Read-Host 'Press Enter to close'

# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
# Isolated mocks only; never attaches a real USB device or uses personal profiles.
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$config = Get-Content -LiteralPath (Join-Path $repo 'build/environment.json') -Raw | ConvertFrom-Json
function ConfigPath([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    [IO.Path]::GetFullPath((Join-Path (Join-Path $repo 'build') $value))
}
function CheckExit { if ($LASTEXITCODE -ne 0) { throw "USB test command failed: $LASTEXITCODE" } }
& (Join-Path $repo 'build/build.ps1') -Target environment
$qt = ConfigPath $config.qtRoot
$msys = ConfigPath $config.msysRoot
$env:PATH = "$qt/bin;$msys/ucrt64/bin;$env:PATH"
$clientBuild = Join-Path $repo 'out/tests/usb-client'
New-Item -ItemType Directory -Force $clientBuild | Out-Null
Push-Location $clientBuild
try {
    & "$qt/bin/qmake.exe" (Join-Path $repo 'tests/usb-client/test.pro'); CheckExit
    & (Join-Path $repo 'moonlight-qt/scripts/jom.exe') -j 4 release; CheckExit
} finally { Pop-Location }
Push-Location $repo
try {
    & "$msys/ucrt64/bin/g++.exe" '-std=c++23' '-O1' '-DBOOST_LOG_DYN_LINK' '-D_WIN32_WINNT=0x0A00' '-Ifoundation-sunshine' '-Ifoundation-sunshine/third-party' 'tests/test-usb-host.cpp' 'foundation-sunshine/src/remote_usb/reverse_tunnel_service.cpp' 'foundation-sunshine/src/remote_usb/remote_usb_host_controller.cpp' '-o' 'out/tests/test-usb-host.exe' '-lboost_log-mt' '-lboost_filesystem-mt' '-lboost_thread-mt' '-lssl' '-lcrypto' '-lws2_32' '-lmswsock' '-ladvapi32'
    CheckExit
    $env:PYTHONUTF8 = '1'
    python tests/test-usb-transport.py; CheckExit
} finally { Pop-Location }

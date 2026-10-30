# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
param([ValidateSet('environment','moonlight','sunshine','sunshine-web','sunshine-gui')][string]$Target = 'moonlight', [string]$EnvironmentFile = (Join-Path $PSScriptRoot 'environment.json'))
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $EnvironmentFile)) { throw 'Copy build/environment.example.json to build/environment.json and set your SDK/runtime paths first.' }
$config = Get-Content -LiteralPath $EnvironmentFile -Raw | ConvertFrom-Json
function ConfigPath([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $value))
}
function CheckExit { if ($LASTEXITCODE -ne 0) { throw "Build command failed: $LASTEXITCODE" } }
if ($Target -eq 'sunshine-web' -or $Target -eq 'sunshine-gui') {
    $node = ConfigPath $config.nodeRoot
    $env:PATH = "$node;$env:PATH"
    $env:npm_config_cache = ConfigPath '../.local/npm-cache'
    $source = if ($Target -eq 'sunshine-gui') { ConfigPath $config.controlPanelSource } else { ConfigPath $config.sunshineSource }
    Push-Location $source
    try {
        if (!(Test-Path 'node_modules')) { & "$node/npm.cmd" ci --no-audit --no-fund; CheckExit }
        if ($Target -eq 'sunshine-gui') {
            & "$node/npm.cmd" run build:renderer; CheckExit
            & "$PSScriptRoot/build.ps1" -Target environment -EnvironmentFile $EnvironmentFile
            $env:RUSTUP_HOME = ConfigPath $config.rustupRoot
            $env:CARGO_HOME = ConfigPath $config.cargoRoot
            $env:CARGO_TARGET_DIR = ConfigPath $config.guiBuild
            & "$env:CARGO_HOME/bin/cargo.exe" build --release --locked --manifest-path src-tauri/Cargo.toml; CheckExit
        } else { & "$node/npm.cmd" run build; CheckExit }
    } finally { Pop-Location }
    return
}
if ($Target -eq 'environment' -or $Target -eq 'moonlight') {
    $qt = ConfigPath $config.qtRoot
    $msvc = ConfigPath $config.msvcRoot
    $vc = Join-Path $msvc "VC/Tools/MSVC/$($config.msvcVersion)"
    $sdk = Join-Path $msvc 'Windows Kits/10'
    $sdkVersion = $config.windowsSdkVersion
    $env:PATH = "$qt/bin;$vc/bin/Hostx64/x64;$sdk/bin/$sdkVersion/x64;$env:PATH"
    $env:INCLUDE = "$vc/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
    $env:LIB = "$vc/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    $env:VCToolsInstallDir = "$vc/"
    $env:WindowsSdkDir = "$sdk/"
    $env:WindowsSDKVersion = "$sdkVersion\"
    $env:VSCMD_ARG_TGT_ARCH = 'x64'
    if (!(Test-Path "$vc/bin/Hostx64/x64/cl.exe") -or !(Test-Path "$sdk/Include/$sdkVersion/um/Windows.h")) {
        throw 'MSVC or Windows SDK is missing. Check environment.json.'
    }
    if ($Target -eq 'environment') { Write-Host "Qt $($config.qtVersion), MSVC $($config.msvcVersion), SDK $sdkVersion loaded for this shell."; return }
    $source = ConfigPath $config.moonlightSource
    $build = ConfigPath $config.moonlightBuild
    New-Item -ItemType Directory -Force $build | Out-Null
    Push-Location $build
    try {
        & "$qt/bin/qmake.exe" "$source/moonlight-qt.pro" '-after' 'QMAKE_CXXFLAGS+=/utf-8'
        CheckExit
        & "$source/scripts/jom.exe" -j 8 release
        CheckExit
    } finally { Pop-Location }
} else {
    $msys = ConfigPath $config.msysRoot
    $env:PATH = "$msys/ucrt64/bin;$msys/usr/bin;$env:PATH"
    $source = ConfigPath $config.sunshineSource
    $build = ConfigPath $config.sunshineBuild
    $base = ConfigPath $config.sunshineBase
    $env:BRANCH = 'master'
    $env:BUILD_VERSION = '2026.1020.23431'
    $env:COMMIT = '66d27405-focus-sync'
    & "$msys/ucrt64/bin/cmake.exe" -S $source -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DBUILD_TESTS=OFF' '-DBUILD_WEB_UI=OFF' '-DFETCH_GUI=OFF' '-DFETCH_DRIVER_DEPS=OFF' '-DDRIVER_DEPS_REQUIRED=OFF' "-DGUI_DIR=$base/assets/gui" '-DSUNSHINE_ENABLE_TRAY=ON' '-DSUNSHINE_REQUIRE_TRAY=ON' "-DSUNSHINE_ENHANCEMENT_PREBUILT_DIR=$base/tools/hdr_enhanced" '-DSUNSHINE_RTX_HDR=ON' '-DSUNSHINE_DLSSNR=ON'
    CheckExit
    & "$msys/ucrt64/bin/cmake.exe" --build $build --target sunshine -j 6
    CheckExit
}

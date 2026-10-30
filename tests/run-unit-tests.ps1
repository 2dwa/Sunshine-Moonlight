# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
param([string]$EnvironmentFile = (Join-Path $PSScriptRoot '../build/environment.json'))
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$config = Get-Content -LiteralPath $EnvironmentFile -Raw | ConvertFrom-Json
$configRoot = Join-Path $repo 'build'
$msys = if ([IO.Path]::IsPathRooted($config.msysRoot)) { $config.msysRoot } else { [IO.Path]::GetFullPath((Join-Path $configRoot $config.msysRoot)) }
$compiler = Join-Path $msys 'ucrt64/bin/g++.exe'
$env:PATH = "$(Join-Path $msys 'ucrt64/bin');$env:PATH"
New-Item -ItemType Directory -Path (Join-Path $repo 'out/tests') -Force | Out-Null
Push-Location $repo
try {
    foreach ($name in @('test-lock-state','test-focus-shortcut','test-clipboard','test-console-startup')) {
        $extra = @()
        if ($name -eq 'test-console-startup') { $extra = @('-mwindows') }
        & $compiler '-std=c++17' '-O2' '-I.' '-Ifoundation-sunshine' "tests/$name.cpp" '-o' "out/tests/$name.exe" @extra '-lshell32' '-lole32' '-luser32' '-ladvapi32' '-lwtsapi32'
        if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
        if ($name -ne 'test-console-startup') {
            & "./out/tests/$name.exe"
            if ($LASTEXITCODE -ne 0) { throw "$name failed" }
        }
    }
} finally { Pop-Location }

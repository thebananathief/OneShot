$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectRoot = Join-Path $root "oneshot_elixir"
$toolRoot = Join-Path $root ".tools\elixir"
$erlangHome = "C:\Program Files\Erlang OTP"
$outDir = Join-Path $root "artifacts\oneshot-elixir-release"

if (-not (Test-Path (Join-Path $toolRoot "bin\mix.bat"))) {
    throw "Expected Elixir toolchain under $toolRoot. Bootstrap the local toolchain first."
}

if (-not (Test-Path (Join-Path $erlangHome "bin\erl.exe"))) {
    throw "Expected Erlang OTP under $erlangHome."
}

$env:ERLANG_HOME = $erlangHome
$env:PATH = (Join-Path $toolRoot "bin") + ";" + (Join-Path $erlangHome "bin") + ";" + $env:PATH

Push-Location $projectRoot
try {
    if (Test-Path $outDir) {
        Remove-Item $outDir -Recurse -Force
    }

    mix deps.get
    mix test
    $env:MIX_ENV = "prod"
    mix release --overwrite

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    Copy-Item ".\_build\prod\rel\oneshot\*" $outDir -Recurse -Force
}
finally {
    Pop-Location
    Remove-Item Env:MIX_ENV -ErrorAction SilentlyContinue
}

Write-Host "Elixir release copied to: $outDir"

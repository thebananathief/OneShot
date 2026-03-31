$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $root "oneshot_native"
$buildDir = Join-Path $root "artifacts\native-build"

. (Join-Path $PSScriptRoot "BuildSupport.ps1")

Invoke-OneShotNativeBuild -SourceDir $sourceDir -BuildDir $buildDir -Configuration "Release"

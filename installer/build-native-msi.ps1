$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $root "oneshot_native"
$buildDir = Join-Path $root "artifacts\\native-build"
$publishDir = Join-Path $root "artifacts\\native-publish"
$msiOut = Join-Path $root "artifacts\\OneShot.Native.msi"
$versionFile = Join-Path $PSScriptRoot ".msi-build-counter-native"

if (Test-Path $versionFile) {
    $currentCounterText = (Get-Content $versionFile -Raw).Trim()
    if ($currentCounterText -match '^\d+$') {
        $counter = [int]$currentCounterText
    } else {
        $counter = 0
    }
} else {
    $counter = 0
}

$nextCounter = $counter + 1
if ($nextCounter -gt 65535) {
    throw "MSI build counter exceeded 65535. Reset installer/.msi-build-counter-native and choose a new major/minor version."
}

Set-Content -Path $versionFile -Value $nextCounter -NoNewline
$productVersion = "1.0.$nextCounter"

New-Item -ItemType Directory -Force $buildDir | Out-Null
New-Item -ItemType Directory -Force $publishDir | Out-Null

cmake -S $sourceDir -B $buildDir -G "Visual Studio 17 2022" -A x64
cmake --build $buildDir --config Release

Copy-Item (Join-Path $buildDir "Release\\oneshot.exe") $publishDir -Force

wix build (Join-Path $root "installer\\OneShot.Native.wxs") -arch x64 -d PublishDir=$publishDir -d ProductVersion=$productVersion -o $msiOut

Write-Host "Native MSI built at: $msiOut"
Write-Host "Native MSI ProductVersion: $productVersion"

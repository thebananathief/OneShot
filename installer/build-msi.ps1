$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $root "oneshot_native"
$buildDir = Join-Path $root "artifacts\native-build"
$publishDir = Join-Path $root "artifacts\publish"
$msiOut = Join-Path $root "artifacts\OneShot.msi"
$versionFile = Join-Path $PSScriptRoot ".msi-build-counter"

New-Item -ItemType Directory -Force $publishDir | Out-Null
New-Item -ItemType Directory -Force $buildDir | Out-Null

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
    throw "MSI build counter exceeded 65535. Reset installer/.msi-build-counter and choose a new major/minor version."
}

Set-Content -Path $versionFile -Value $nextCounter -NoNewline
$productVersion = "1.0.$nextCounter"

cmake -S $sourceDir -B $buildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Native build failed." }

Get-ChildItem -Path $publishDir -Force -ErrorAction SilentlyContinue | Remove-Item -Force -Recurse
Copy-Item (Join-Path $buildDir "Release\oneshot.exe") (Join-Path $publishDir "OneShot.exe") -Force

wix build (Join-Path $root "installer\OneShot.wxs") -arch x64 -d PublishDir=$publishDir -d ProductVersion=$productVersion -o $msiOut

Write-Host "MSI built at: $msiOut"
Write-Host "MSI ProductVersion: $productVersion"

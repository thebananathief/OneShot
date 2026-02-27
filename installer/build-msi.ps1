$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$publishDir = Join-Path $root "artifacts\publish"
$msiOut = Join-Path $root "artifacts\OneShot.msi"
$versionFile = Join-Path $PSScriptRoot ".msi-build-counter"

New-Item -ItemType Directory -Force $publishDir | Out-Null

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

dotnet publish (Join-Path $root "OneShot\OneShot.csproj") -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true /p:IncludeNativeLibrariesForSelfExtract=true /p:PublishTrimmed=false -o $publishDir

wix build (Join-Path $root "installer\OneShot.wxs") -arch x64 -d PublishDir=$publishDir -d ProductVersion=$productVersion -o $msiOut

Write-Host "MSI built at: $msiOut"
Write-Host "MSI ProductVersion: $productVersion"

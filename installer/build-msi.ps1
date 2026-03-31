$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $root "oneshot_native"
$buildDir = Join-Path $root "artifacts\native-build"
$publishDir = Join-Path $root "artifacts\publish"
$msiOut = Join-Path $root "artifacts\OneShot.msi"
$versionFile = Join-Path $PSScriptRoot ".msi-build-counter"

. (Join-Path $PSScriptRoot "BuildSupport.ps1")

function Get-InstalledOneShotPatchVersion {
    $uninstallRoots = @(
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
    )

    $maxPatch = 0
    foreach ($rootPath in $uninstallRoots) {
        if (-not (Test-Path $rootPath)) {
            continue
        }

        Get-ChildItem $rootPath | ForEach-Object {
            $item = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
            if ($null -eq $item -or $item.DisplayName -ne "OneShot" -or [string]::IsNullOrWhiteSpace($item.DisplayVersion)) {
                return
            }

            $parts = $item.DisplayVersion.Split('.')
            if ($parts.Length -ge 3 -and $parts[2] -match '^\d+$') {
                $patch = [int]$parts[2]
                if ($patch -gt $maxPatch) {
                    $maxPatch = $patch
                }
            }
        }
    }

    return $maxPatch
}

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

$installedCounter = Get-InstalledOneShotPatchVersion
$nextCounter = [Math]::Max($counter, $installedCounter) + 1
if ($nextCounter -gt 65535) {
    throw "MSI build counter exceeded 65535. Reset installer/.msi-build-counter and choose a new major/minor version."
}

Set-Content -Path $versionFile -Value $nextCounter -NoNewline
$productVersion = "1.0.$nextCounter"

Invoke-OneShotNativeBuild -SourceDir $sourceDir -BuildDir $buildDir -Configuration "Release"

Get-ChildItem -Path $publishDir -Force -ErrorAction SilentlyContinue | Remove-Item -Force -Recurse
Copy-Item (Join-Path $buildDir "Release\oneshot.exe") (Join-Path $publishDir "OneShot.exe") -Force

wix build (Join-Path $root "installer\OneShot.wxs") -arch x64 -d PublishDir=$publishDir -d ProductVersion=$productVersion -o $msiOut

Write-Host "MSI built at: $msiOut"
Write-Host "MSI ProductVersion: $productVersion"

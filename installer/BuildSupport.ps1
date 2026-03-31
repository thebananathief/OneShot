$ErrorActionPreference = "Stop"

function Get-OneShotVisualStudioInstances {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return @()
    }

    $instances = & $vswhere -all -products * -format json | ConvertFrom-Json
    if ($null -eq $instances) {
        return @()
    }

    return @(
        $instances |
            Where-Object { $_.productId -like "Microsoft.VisualStudio.Product.*" -and $_.isComplete } |
            Sort-Object { [version]$_.installationVersion } -Descending
    )
}

function Get-OneShotVisualStudioGeneratorName {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Instance
    )

    $majorVersion = ([version]$Instance.installationVersion).Major
    switch ($majorVersion) {
        18 { return "Visual Studio 18 2026" }
        17 { return "Visual Studio 17 2022" }
        16 { return "Visual Studio 16 2019" }
        default { return $null }
    }
}

function Resolve-OneShotCMakePath {
    if ($env:ONESHOT_CMAKE) {
        if (-not (Test-Path $env:ONESHOT_CMAKE)) {
            throw "ONESHOT_CMAKE points to '$($env:ONESHOT_CMAKE)', but that file does not exist."
        }

        return $env:ONESHOT_CMAKE
    }

    foreach ($instance in Get-OneShotVisualStudioInstances) {
        $bundledCmake = Join-Path $instance.installationPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $bundledCmake) {
            return $bundledCmake
        }
    }

    $pathCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $commonInstall = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $commonInstall) {
        return $commonInstall
    }

    throw "Unable to locate CMake. Install standalone CMake or a Visual Studio workload that includes the CMake component, or set ONESHOT_CMAKE to cmake.exe."
}

function Resolve-OneShotVisualStudioGenerator {
    param(
        [Parameter(Mandatory)]
        [string]$CMakePath
    )

    if ($env:ONESHOT_CMAKE_GENERATOR) {
        return $env:ONESHOT_CMAKE_GENERATOR
    }

    $helpText = & $CMakePath --help | Out-String
    foreach ($instance in Get-OneShotVisualStudioInstances) {
        $generator = Get-OneShotVisualStudioGeneratorName -Instance $instance
        if ($generator -and $helpText.Contains($generator)) {
            return $generator
        }
    }

    if ($helpText.Contains("Visual Studio 17 2022")) {
        return "Visual Studio 17 2022"
    }

    if ($helpText.Contains("Visual Studio 16 2019")) {
        return "Visual Studio 16 2019"
    }

    throw "Unable to find a supported Visual Studio generator in '$CMakePath --help'. Set ONESHOT_CMAKE_GENERATOR if you need to override generator selection."
}

function Invoke-OneShotNativeBuild {
    param(
        [Parameter(Mandatory)]
        [string]$SourceDir,

        [Parameter(Mandatory)]
        [string]$BuildDir,

        [string]$Configuration = "Release"
    )

    New-Item -ItemType Directory -Force $BuildDir | Out-Null

    $cmakePath = Resolve-OneShotCMakePath
    $generator = Resolve-OneShotVisualStudioGenerator -CMakePath $cmakePath

    Write-Host "Using CMake: $cmakePath"
    Write-Host "Using generator: $generator"

    & $cmakePath -S $SourceDir -B $BuildDir -G $generator -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    & $cmakePath --build $BuildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw "Native build failed." }
}

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectRoot = Join-Path $root "oneshot_elixir"
$toolRoot = Join-Path $root ".tools\elixir"
$erlangHome = "C:\Program Files\Erlang OTP"
$outDir = Join-Path $root "artifacts\oneshot-elixir-release"
$launcherPath = Join-Path $outDir "OneShot.cmd"

function Remove-DirectoryWithRetry {
    param(
        [string]$Path,
        [int]$Attempts = 5
    )

    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        if (-not (Test-Path $Path)) {
            return
        }

        try {
            Remove-Item $Path -Recurse -Force -ErrorAction Stop
            return
        }
        catch {
            if ($attempt -eq $Attempts) {
                throw
            }

            Start-Sleep -Milliseconds (150 * $attempt)
        }
    }
}

function Stop-ReleaseProcesses {
    param(
        [string]$Root
    )

    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -like "$Root*" -and $_.ProcessName -match '^(erl|epmd|beam)$' } |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

function Write-ReleaseLauncher {
    param(
        [string]$Path
    )

    $content = @'
@echo off
setlocal
set "ROOT=%~dp0"
set "ONESHOT_STARTUP_COMMAND=%~f0"

call "%ROOT%bin\oneshot.bat" pid >nul 2>nul
set "RUNNING=%ERRORLEVEL%"

if "%~1"=="" goto ensure_start
if /I "%~1"=="snapshot" goto snapshot
if /I "%~1"=="diagnostics" goto diagnostics
if /I "%~1"=="install-startup" goto install_startup
if /I "%~1"=="uninstall-startup" goto uninstall_startup
if /I "%~1"=="stop" goto stop_release

goto snapshot

:ensure_start
if "%RUNNING%"=="0" exit /b 0
start "" /min cmd /c ""%ROOT%bin\oneshot.bat" start"
exit /b 0

:snapshot
if not "%RUNNING%"=="0" (
  start "" /min cmd /c ""%ROOT%bin\oneshot.bat" start"
  timeout /t 3 /nobreak >nul
)
call "%ROOT%bin\oneshot.bat" rpc "OneshotCore.CaptureCoordinator.request_snapshot(:launcher)"
exit /b %ERRORLEVEL%

:diagnostics
if "%RUNNING%"=="0" (
  call "%ROOT%bin\oneshot.bat" rpc "OneshotShell.CLI.main([\"diagnostics\"])"
) else (
  call "%ROOT%bin\oneshot.bat" eval "OneshotShell.CLI.main([\"diagnostics\"])"
)
exit /b %ERRORLEVEL%

:install_startup
call "%ROOT%bin\oneshot.bat" eval "Application.ensure_all_started(:oneshot_core); IO.inspect(OneshotCore.StartupRegistration.install())"
exit /b %ERRORLEVEL%

:uninstall_startup
call "%ROOT%bin\oneshot.bat" eval "Application.ensure_all_started(:oneshot_core); IO.inspect(OneshotCore.StartupRegistration.uninstall())"
exit /b %ERRORLEVEL%

:stop_release
if "%RUNNING%"=="0" (
  call "%ROOT%bin\oneshot.bat" stop
)
exit /b 0
'@

    $normalized = ($content -split "`r?`n") -join "`r`n"
    [System.IO.File]::WriteAllText($Path, $normalized + "`r`n", [System.Text.Encoding]::ASCII)
}

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
        Stop-ReleaseProcesses -Root $outDir
        Remove-DirectoryWithRetry -Path $outDir
    }

    mix deps.get
    mix test
    $env:MIX_ENV = "prod"
    mix release --overwrite

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    Copy-Item ".\_build\prod\rel\oneshot\*" $outDir -Recurse -Force
    Write-ReleaseLauncher -Path $launcherPath
}
finally {
    Pop-Location
    Remove-Item Env:MIX_ENV -ErrorAction SilentlyContinue
}

Write-Host "Elixir release copied to: $outDir"
Write-Host "Elixir launcher generated at: $launcherPath"

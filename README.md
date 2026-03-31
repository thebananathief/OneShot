# OneShot

OneShot is a Windows screenshot tray app built as a native C++20 Win32 desktop application.

## Features

- Runs as a tray daemon until exit.
- Accepts command activation with `OneShot.exe snapshot`.
- Supports drag-to-select region capture across the desktop.
- Shows custom notification cards with draggable thumbnails.
- Opens a native markup editor with pen, shape, arrow, polygon, and text tools.
- Supports save to `Pictures\Screenshots` and clipboard copy.

## Build

```powershell
powershell .\installer\build-native.ps1
```

`build-native.ps1` auto-detects a compatible Visual Studio CMake install and generator, so it works whether `cmake.exe` is on `PATH` or bundled inside Visual Studio.

## Build The MSI

```powershell
powershell .\installer\build-msi.ps1
```

## Run

```powershell
.\artifacts\native-build\Release\oneshot.exe
```

Trigger a snapshot from another process:

```powershell
.\artifacts\native-build\Release\oneshot.exe snapshot
```

## Usage Notes

- If OneShot is already running, `snapshot` is forwarded through named pipe IPC.
- If OneShot is not running, launching with `snapshot` starts the daemon and triggers capture.

## AutoHotkey Example

```ahk
#s::Run "C:\Path\To\OneShot.exe snapshot"
!q::Run "C:\Path\To\OneShot.exe snapshot"
```

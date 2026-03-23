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
cmake -S .\oneshot_native -B .\artifacts\native-build -G "Visual Studio 17 2022" -A x64
cmake --build .\artifacts\native-build --config Release
```

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

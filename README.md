# OneShot

OneShot is a Windows screenshot daemon app.

## What it does

- Runs as a tray daemon until exit.
- Accepts command activation: `oneshot.exe snapshot`.
- Supports drag-to-select region capture.
- Shows custom notification with draggable thumbnail.
- Opens markup editor with shapes, pen, arrow, and text tools.
- Supports save to `Pictures\Screenshots` and clipboard copy.

## Build

```powershell
dotnet build OneShot.slnx
```

## Run

```powershell
dotnet run --project .\OneShot\OneShot.csproj
```

Trigger a snapshot from another process:

```powershell
dotnet run --project .\OneShot\OneShot.csproj -- snapshot
```

## AutoHotkey example

```ahk
#s::Run "C:\Path\To\OneShot.exe snapshot"
!q::Run "C:\Path\To\OneShot.exe snapshot"
```

## Notes

- If OneShot is already running, `snapshot` is forwarded through named pipe IPC.
- If OneShot is not running, launching with `snapshot` starts the daemon and triggers capture.

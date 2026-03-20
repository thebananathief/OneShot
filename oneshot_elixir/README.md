# OneShot Elixir Rewrite

Side-by-side OTP rewrite of the OneShot screenshot daemon for Windows.

Current implementation status:

- `oneshot_core` owns config, instance locking, JSON/TCP IPC, and daemon-side coordination.
- `oneshot_ui` owns the resident `:wx` shell and tray integration.
- The rewrite currently implements the runtime skeleton and daemon control path first; screen capture and markup parity are still in progress.

## Development

```powershell
cd .\oneshot_elixir
mix deps.get
mix test
mix release --overwrite
powershell -NoProfile -ExecutionPolicy Bypass -File .\bin\oneshot.ps1 diagnostics
powershell -NoProfile -ExecutionPolicy Bypass -File .\bin\oneshot.ps1 snapshot
```

Current working slice:

- Single-instance lock file under `%LOCALAPPDATA%\OneShot`
- Loopback TCP command server on `127.0.0.1:45731`
- JSON line protocol for `snapshot`, `install-startup`, `uninstall-startup`, `diagnostics`, and `ping`
- Resident daemon bootstrap through `OneshotShell.CLI`
- Early `:wx` tray host scaffolding in `oneshot_ui`

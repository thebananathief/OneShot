# AGENTS.md - Coding agent guidelines

## Project Structure & Module Organization
- `oneshot_native/` contains the primary Windows-native C++ application.
- `oneshot_native/src/` contains Win32 app code, rendering, capture, IPC, notifications, and editor logic.
- `oneshot_native/include/oneshot_native/` contains public headers shared across the native app.
- `installer/` contains WiX installer sources and packaging scripts for the production MSI.
- `artifacts/` is build output for native binaries and MSI artifacts; avoid committing generated files.
- `.tools/` contains local toolchain downloads and should not be treated as source.

## Build and Development Commands
- `powershell .\installer\build-native.ps1` configures and builds the native executable with auto-detected CMake and Visual Studio generator selection.
- `powershell .\installer\build-msi.ps1` builds the native app, publishes `artifacts\publish\OneShot.exe`, and produces `artifacts\OneShot.msi`.
- `.\artifacts\native-build\Release\oneshot.exe` runs the native tray daemon locally after a successful build.
- `.\artifacts\native-build\Release\oneshot.exe snapshot` runs and triggers capture flow against the native app.

## Commit Guidelines
- Use Conventional Commit messages: `<type>(optional-scope): <summary>`.
- Common types:
  - `feat`: new feature
  - `fix`: bug fix
  - `docs`: documentation only
  - `style`: formatting/style-only changes
  - `refactor`: code change with no feature/fix
  - `perf`: performance improvement
  - `test`: test changes
  - `build`: build/dependency changes
  - `chore`: maintenance tasks
  - `revert`: revert prior commit

## Workflow
- **Required for completion:** after any code change, always run a rebuild + reinstall cycle before reporting work complete.
- After the workflow steps complete, run `git commit` for the finished work using the Commit Guidelines above (Conventional Commits).
- Rebuild installer:
  - `powershell .\installer\build-msi.ps1`
- Before reinstalling, ensure no running instance remains:
  - `Get-Process oneshot -ErrorAction SilentlyContinue | Stop-Process -Force`
- Reinstall:
  - `msiexec /i "$PWD\artifacts\OneShot.msi"`
    - Use this as the primary install/upgrade path.
    - Use `msiexec /i "$PWD\artifacts\OneShot.msi" /l*v "$PWD\msi-install.log"` for install/repair tracing.
  - `msiexec /i "$PWD\artifacts\OneShot.msi" REINSTALL=ALL REINSTALLMODE=vomus` is repair mode only.
    - It does not install when the product is not already present, and may appear to skip replacements in some environments.
  - If a running `oneshot.exe` process exists, `msiexec.exe /i` is unreliable and may fail; the agent MUST end ALL running `oneshot.exe` processes before install.

## Coding Style & Naming Conventions
- Use 4-space indentation and standard C++ brace style.
- Prefer modern C++20 language features already enabled by the build.
- Naming: `PascalCase` for types, `camelCase` for locals and parameters, and `m_memberName` or a clearly consistent internal convention when extending existing native classes.
- Keep Win32/UI behavior in native UI modules such as `TrayController`, `OverlayManager`, `NotificationManager`, and `MarkupEditorWindow`.
- Keep reusable non-UI logic in services such as `CaptureService`, `CommandServer`, `OutputService`, `StartupService`, and `TempFileManager`.
- Prefer narrow, explicit Windows API wrappers instead of scattering raw Win32 calls across unrelated files.

## Native Windows Notes
- Use raw Win32, WIC, GDI, and OLE/COM directly; avoid introducing heavyweight UI frameworks.
- Keep the primary UI thread ownership model intact for windows, tray interactions, clipboard, and drag/drop.
- Preserve DPI-aware screen-pixel coordinate handling and centralize coordinate transforms.
- Prefer adding small service abstractions over hidden global state.
- When changing installer behavior, verify the MSI still installs the native `OneShot.exe`.

## Self Improvement Notes

Use this section to record brief notes after getting past a blocker; append new entries below with whatever header and description format fits the issue.

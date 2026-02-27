# AGENTS.md - Coding agent guidelines

## Project Structure & Module Organization
- `OneShot/` contains the WPF app targeting `net8.0-windows`.
- `OneShot/Windows/` holds XAML UI windows and code-behind files.
- `OneShot/Services/` contains app services (capture, startup, IPC, output).
- `OneShot/Models/` contains shared data models and command payloads.
- `OneShot/Interop/` contains Windows interop helpers.
- `OneShot/Assets/` stores static UI assets (for example `Assets/MarkupIcons/*.png`).
- `installer/` contains WiX installer sources and packaging script.
- `artifacts/` is build output (published binaries and MSI artifacts); avoid committing generated files.

## Build and Development Commands
- `dotnet restore OneShot.slnx` restores NuGet packages.
- `dotnet build OneShot.slnx` builds the solution.
- `dotnet test OneShot.Tests/OneShot.Tests.csproj --no-build` runs automated tests.
- `dotnet run --project .\OneShot\OneShot.csproj` runs the tray daemon locally.
- `dotnet run --project .\OneShot\OneShot.csproj -- snapshot` runs and triggers capture flow.

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
- As part of completion workflow, run tests: `dotnet test OneShot.Tests/OneShot.Tests.csproj --no-build`.
- After the workflow steps complete, run `git commit` for the finished work using the Commit Guidelines above (Conventional Commits).
- Rebuild installer:
  - `powershell .\installer\build-msi.ps1` publishes self-contained `win-x64` output and builds `artifacts\OneShot.msi` (requires WiX CLI: `wix`).
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
- Use 4-space indentation and standard C# brace style (K&R with braces on new lines for members/types).
- Keep nullable reference types enabled and avoid suppressing warnings without justification.
- Naming: `PascalCase` for public types/members, `camelCase` for locals/parameters, `_camelCase` for private readonly fields.
- Keep UI behavior in `Windows/*` and reusable logic in `Services/*` to maintain separation.

## Avalonia Library Notes
- Prefer MVVM and keep view logic thin:
  - Bind to view models; avoid code-behind except for UI-only concerns.
  - Use `ReactiveCommand`/`ICommand` and data binding instead of direct event wiring where possible.
- Keep styles centralized and predictable:
  - Put shared styles in `App.axaml` (or dedicated style files) and keep control-local overrides minimal.
  - Use `ThemeVariant` resources carefully; verify both light/dark variants if enabled.
- Use compiled bindings for safety/perf:
  - Enable compiled bindings where possible to catch binding errors at build time.
  - Always set `x:DataType` on views/templates that use compiled bindings.
- Common pitfalls and easy fixes:
  - Pitfall: Silent binding failures.
    - Fix: Turn on binding diagnostics/logging during development and verify `DataContext` assignment paths.
  - Pitfall: UI updates from background threads.
    - Fix: Marshal to UI thread with `Dispatcher.UIThread.Post(...)`/`InvokeAsync(...)`.
  - Pitfall: Large visual trees causing sluggish rendering.
    - Fix: Use virtualization (`ItemsRepeater`, virtualizing panels), reduce nested layouts, and avoid unnecessary effects.
  - Pitfall: Resource key collisions in merged dictionaries.
    - Fix: Prefix app-specific keys and keep dictionary ownership clear by feature.
  - Pitfall: Asset path issues after publish.
    - Fix: Use Avalonia asset URIs (`avares://...`) and verify `AvaloniaResource` build actions.
- Icons:
  - Prefer the Avalonia icon packages and documented usage patterns before adding custom bitmap assets.
  - Reference: `https://avaloniaui.github.io/icons.html`
- Reference docs:
  - Developer docs: `https://docs.avaloniaui.net/`
  - API docs: `https://api-docs.avaloniaui.net/`

## Self Improvement Notes

Use this section to record brief notes after getting past a blocker; append new entries below with whatever header and description format fits the issue.

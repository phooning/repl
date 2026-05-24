# REPL

A live REPL running the stack:

```
App shell:        C++ Qt6 Widgets
Editor:           Monaco in QWebEngineView
Console:          QPlainTextEdit
Execution:        tsx + Node 24
Typechecking:     tsgo where available, fallback to tsc
Language server:  typescript-language-server
IPC:              Qt WebChannel + JSON-RPC
Build:            CMake + vcpkg/conan optional
Packaging:        Qt Installer Framework or CPack
```

## File Structure

```
src/
  app/
    MainWindow.cpp
    EditorPane.cpp
    ConsolePane.cpp
    SplitView.cpp

  editor/
    WebEditorBridge.cpp       # Qt <-> Monaco/CodeMirror bridge
    EditorModel.cpp           # current document state
    DiagnosticModel.cpp
    ThemeModel.cpp

  runtime/
    TsRunner.cpp              # executes current code
    ProcessManager.cpp        # QProcess wrapper
    SandboxProfile.cpp
    TempWorkspace.cpp

  language/
    LspClient.cpp             # JSON-RPC
    TsServerProcess.cpp
    CompletionModel.cpp
    FormatProvider.cpp

  project/
    ProjectModel.cpp
    PackageManager.cpp
    TsConfigModel.cpp
    DependencyResolver.cpp

  ui/
    CommandPalette.cpp
    StatusBar.cpp
    ProblemsPanel.cpp
```

## Flow

User edits code
    ↓ debounce 250–500 ms
Save to temp workspace or in-memory virtual FS
    ↓
Run typecheck / transpile / execute
    ↓
Stream stdout/stderr into console pane
    ↓
Map diagnostics back to editor decorations

## Runtime Process

```
Qt main process
├─ editor webview process
├─ typescript-language-server process
├─ tsgo/tsc typecheck process
└─ node/tsx execution process
```

## V1 MVP

This repository currently contains a deliberately narrow Qt MVP:

- Left pane: Monaco editor hosted in `QWebEngineView`
- Right pane: read-only `QPlainTextEdit` output console
- Run: writes the editor buffer to a temp `main.tsx` and executes it with `tsx`
- Typecheck: writes the editor buffer to the same temp file, then runs `tsgo --noEmit` when available, falling back to `tsc --noEmit`
- Diagnostics: parses TypeScript compiler output and applies Monaco inline markers to `main.tsx`
- Safety controls: Stop button, process timeout, output cap, and bounded console history
- Autorun: debounced hot execution after editor changes settle
- Editor assets: Monaco and Vim mode assets are loaded from local packages and copied beside the executable at build time

## Vim Mode

The editor includes an optional Vim mode powered by `monaco-vim`.

- Toggle it from the top toolbar.
- Vim mode is off by default.
- The fallback textarea does not support Vim mode.
- Basic normal, insert, and visual operations are delegated to `monaco-vim`.
- App-level shortcuts such as `Ctrl+R` for Run remain active.

Build and run:

```sh
pnpm install
pnpm build:editor-assets
cmake -S . -B build
cmake --build build
./build/repl
```

Tool lookup checks `REPL_<TOOL>_PATH`, local `node_modules/.bin`, PATH and `/usr/bin`, then `pnpm exec` / `npm exec` fallbacks. `tsx` is required for Run; Typecheck prefers `tsgo` and falls back to `tsc`.

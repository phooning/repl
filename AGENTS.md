# AGENTS.md

## Project Overview

This is a native C++ project. JavaScript/TypeScript tooling may exist at the repository root for developer automation, code generation, formatting, linting, test orchestration, or experimental TypeScript execution, but the primary application/runtime is C++.

Agents should treat this repository as a C++ project first, with `pnpm` as an optional tooling layer.

## Repository Structure

Expected layout:

```txt
.
├── CMakeLists.txt
├── src/
├── include/
├── tests/
├── scripts/
├── package.json
|-- biome.json
├── pnpm-lock.yaml
├── AGENTS.md
└── .gitignore
````

Common directories:

* `src/` — C++ implementation files.
* `include/` — public or shared C++ headers.
* `tests/` — C++ tests.
* `scripts/` — helper scripts, possibly TypeScript, JavaScript, Python, or shell.
* `build/` — local CMake/Ninja build output. This should not be committed.
* `node_modules/` — pnpm-managed JavaScript dependencies. This should not be committed.

## Toolchain Expectations

### Native Toolchain

Prefer the local system C++ toolchain:

* CMake
* Ninja or Make
* Clang or GCC
* Optional: Qt 6, LLVM tooling, sanitizers, clangd, clang-format, clang-tidy

Common commands:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

For debug builds:

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

For release builds:

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
```

### pnpm Tooling Layer

The project may use `pnpm` at the root even though the main project is C++.

Use project-local tools first:

```bash
pnpm install
pnpm exec tsc --version
pnpm exec tsx ./scripts/example.ts
pnpm exec tsgo --version
```

Do not assume globally installed Node packages are available.

When resolving TypeScript-related binaries, prefer this lookup order:

1. `./node_modules/.bin/<tool>`
2. `pnpm exec <tool>`
3. System PATH
4. Known system fallback, such as `/usr/bin/tsc` on Arch/CachyOS

Important notes:

* The Arch/CachyOS `typescript` pacman package provides `tsc` and `tsserver`.
* It does not provide `tsx`.
* It does not provide `tsgo`.
* Install `tsx` with:

```bash
pnpm add -D tsx
```

* Install `tsgo` with:

```bash
pnpm add -D @typescript/native-preview
```

## Agent Behavior Guidelines

### General

* Preserve the project’s native C++ architecture.
* Do not introduce a JavaScript runtime dependency into the production application unless explicitly requested.
* Keep JS/TS tooling isolated to scripts, developer tooling, build helpers, or analysis utilities.
* Prefer small, reviewable changes.
* Avoid large rewrites unless the user explicitly asks for a refactor.
* Do not silently change build systems.
* Do not vendor generated dependency folders.

### C++ Guidelines

Prefer modern C++ practices:

* Clear ownership semantics.
* RAII.
* `std::unique_ptr` / `std::shared_ptr` only where appropriate.
* `std::span`, `std::string_view`, `std::optional`, and `std::expected` where supported and useful.
* Avoid raw owning pointers.
* Avoid global mutable state unless necessary.
* Keep platform-specific code isolated.

When adding files:

* Public headers go in `include/` when part of the public interface.
* Private headers may live near implementation files in `src/`.
* Tests should mirror the structure of the code they validate.

### Formatting

Use the project’s existing formatting with `biome` as specified in `package.json`:

```
"format": "biome format --write .",
"format:check": "biome format .",
"lint": "biome lint .",
"check": "biome check .",
"check:fix": "biome check --write .",
"typecheck": "tsc --noEmit"
```

### Build System

For CMake:

* Prefer target-based CMake.
* Use `target_link_libraries`, `target_include_directories`, and `target_compile_features`.
* Avoid global `include_directories`.
* Avoid global compiler flags unless necessary.
* Keep third-party dependency declarations centralized.

Good pattern:

```cmake
add_executable(app src/main.cpp)

target_compile_features(app PRIVATE cxx_std_20)

target_include_directories(app
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

### Tests

When modifying logic, add or update tests where practical.

Preferred commands:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

If tests are not configured yet, do not invent a full framework unless requested. Suggest one instead.

### TypeScript / pnpm Scripts

TypeScript scripts should be treated as dev tooling.

Use:

```bash
pnpm exec tsx ./scripts/name.ts
```

Avoid relying on globally installed `tsx`.

If a script requires TypeScript type-checking:

```bash
pnpm exec tsc --noEmit
```

If `tsx` is missing, show a clear error:

```txt
tsx is not installed in this project.
Install it with: pnpm add -D tsx
```

If `tsgo` is missing, show:

```txt
tsgo is not installed in this project.
Install it with: pnpm add -D @typescript/native-preview
```

## Dependency Policy

Do not commit:

* `node_modules/`
* CMake build directories
* compiler outputs
* binary artifacts
* local IDE state
* logs
* caches
* generated files unless explicitly intended for source control

Commit:

* `package.json`
* `pnpm-lock.yaml`
* `CMakeLists.txt`
* source files
* headers
* tests
* scripts
* stable configuration files

## Safety Rules for Agents

Before making changes:

1. Inspect the existing project structure.
2. Identify the active build system.
3. Preserve existing naming and style conventions.
4. Prefer minimal diffs.
5. Do not remove files unless clearly obsolete or explicitly requested.
6. Do not run destructive commands.
7. Do not overwrite user changes.

Avoid:

```bash
rm -rf
git reset --hard
git clean -fdx
pnpm install --force
sudo
```

unless the user explicitly asks and understands the consequence.

## Recommended Validation

After C++ changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

After TypeScript tooling changes:

```bash
pnpm install
pnpm exec tsc --noEmit
```

After script changes using `tsx`:

```bash
pnpm exec tsx ./scripts/<script>.ts
```

## Environment Notes

On Arch/CachyOS:

```bash
sudo pacman -S cmake ninja clang typescript nodejs pnpm
```

The pacman `typescript` package exposes:

```txt
/usr/bin/tsc
/usr/bin/tsserver
```

It does not expose:

```txt
tsx
tsgo
```

Use project-local pnpm dev dependencies for those tools.

## Code exploration — prefer `ast-outline` over full reads

For `.rs`, `.cs`, `.py`, `.pyi`, `.ts`, `.tsx`, `.js`, `.jsx`, `.java`, `.kt`, `.kts`,
`.scala`, `.sc`, `.go`, and `.md` files, read structure with `ast-outline`
before opening full contents.
Pull method bodies only once you know which ones you need.

Stop at the step that answers the question:

1. **Unfamiliar directory** — `ast-outline digest <dir>`: one-page map
   of every file's types and public methods.

2. **One file's shape** — `ast-outline <file>`: signatures with line
   ranges, no bodies (5–10× smaller than a full read).

3. **One method, class, or markdown section** — `ast-outline show <file>
   <Symbol>`. Suffix matching: `TakeDamage`, or `Player.TakeDamage` when
   ambiguous. Multiple at once: `ast-outline show Player.cs TakeDamage
   Heal Die`. For markdown, the symbol is the heading text.

4. **Who implements/extends a type** — `ast-outline implements <Type>
   <dir>`: AST-accurate (skip `grep`), transitive by default with
   `[via Parent]` tags on indirect matches. Add `--direct` for level-1 only.

Fall back to a full read only when you need context beyond the body
`show` returned.

If the outline header contains `# WARNING: N parse errors`, the outline
for that file is partial — read the source directly for the affected region.

`ast-outline help` for flags and rare options.

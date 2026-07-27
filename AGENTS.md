# Agent Instructions

These instructions apply to the whole repository.

## Project Overview

ImgVw is a small, fast Windows image viewer written in C++ with Win32 UI code. Keep changes conservative and preserve the
documented Windows XP compatibility target unless the user explicitly asks to change it.

## Repository Layout

- `src/app/`: application startup and top-level app types.
- `src/ui/win32/`: Win32 windows, message handling, rendering integration, and UI command dispatch.
- `src/browse/`: image list state, folder enumeration, and navigation behavior.
- `src/image/`: image item abstractions, format detection and decoding, image buffers, cache, and color helpers.
- `src/platform/win32/`: Win32 platform helpers and RAII wrappers.
- `resources/`: Windows resources, manifest, icons, and `resource.h`.
- `3rd-party/`: vendored dependencies. Do not reformat or refactor these files.
- `docs/plans/`: active, pending, and archived implementation plans. Use them for context, but prefer the current
  source tree when it disagrees.

## Build and Verification

- Visual Studio build entrypoint: `ImgVw.slnx` / `ImgVw.vcxproj`.
- MSYS/Makefile build entrypoint: `make`.
- Repository build scripts are under `scripts/`. Prefer `scripts/build-msys.ps1` for MSYS application builds; it locates
  MSYS2, selects the architecture-specific shell/toolchain, invokes the Makefile, and verifies the output executable.
  For example:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean`
- Test entrypoint: `scripts/test-msys.ps1`. Prefer this for unit tests; it uses the same MSYS2 discovery pattern as the
  build script, selects the requested architecture shell/toolchain, and runs `make -C tests test`.
  For example:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Clean`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x64 -Clean`
- Dependency build scripts are `scripts/build-libjpeg-turbo.ps1`, `scripts/build-libheif.ps1`, and
  `scripts/build-little-cms.ps1`. Use them when rebuilding vendored library artifacts instead of invoking dependency
  build systems manually.
- Useful Makefile variants:
  - `make`
  - `make config=release`
  - `make arch=x64`
  - `make arch=x64 config=release`
- The Makefile defines `WINVER=0x0501` and `_WIN32_WINNT=0x0501`; do not introduce newer Win32 APIs without guards or
  compatible fallbacks.
- Prefer incremental builds and tests for routine verification. Use clean builds only when necessary, such as after
  build-system, configuration, toolchain, or dependency changes; when investigating stale outputs; or for release
  validation. The `-Clean` commands above are examples, not the default for every change.
- When possible, verify changes with at least one local build path. If a toolchain is unavailable, state that clearly.
- Do not assume MSYS2 is installed at `C:\msys64`; use the repository scripts' discovery logic or call the scripts
  directly.

## Coding Style

- Follow `docs/CODING_STYLE.md`.
- Use the root `.clang-format` configuration for first-party C++ files.
- Use 4 spaces, no tabs, LF line endings except for `.rc`/`.rc2` files, and a final newline.
- Keep lines at or below 120 columns where practical.
- Classes, structs, enums, and public functions use `CamelCase`.
- Private data members use `lower_case_` with a trailing underscore.
- Constants use the existing `kName` style.
- Avoid Hungarian notation in new first-party code except where Win32 API conventions make it clearer.
- First-party UTF-8 source files must not have a BOM. Do not add a BOM when editing `.h`, `.cpp`, or other UTF-8 source
  files, and remove an existing BOM only when that encoding cleanup is intentional for the change.
- Preserve existing non-UTF-8 encodings. Do not rewrite an ANSI file as UTF-8 as a side effect of formatting or
  mechanical edits.
- `resources/*.rc` and `resources/*.rc2` are Windows resource files. Treat them as CRLF and Windows-1252/ANSI unless
  their encoding is intentionally changed. Do not run broad formatters over them.
- When editing `.rc` files, use byte/encoding-preserving replacements and verify with a build path that runs `windres`
  or the Visual Studio resource compiler.

## PowerShell and Windows Shell Practices

- Always invoke PowerShell with `-NoProfile`.
- Avoid nested `powershell -Command` calls for non-trivial scripts or string replacements. Prefer running PowerShell
  directly in the current shell, using `-File`, or using a short temporary script when quoting would be fragile.
- Quote Git refs and paths that contain special PowerShell characters. In particular, quote stash refs such as
  `'stash@{0}'`.
- Be careful with PowerShell strings containing `&`, tabs, quotes, backticks, or resource-script text. Prefer
  single-quoted literals or here-strings, and verify the file content after replacement.
- For multi-line PowerShell examples, prefer splatting or one command per line over fragile trailing-backtick command
  chains when editing scripts or docs.

## Architecture Guidelines

- Keep Win32 API details in `src/ui/win32/` and `src/platform/win32/` where practical.
- Keep image decoding, caching, and browser state independent from window message handling.
- Prefer RAII for Win32 handles, GDI objects, file mappings, critical sections, and heap allocations.
- Do not block paint or UI message paths on worker-thread completion. Use state objects and UI notifications instead.
- Capture `GetLastError()` close to the failing Win32 API call.
- Prefer explicit result/status objects at subsystem boundaries over silent failures or mixed error styles.

## Change Discipline

- Do not modify generated build outputs under `bin/` or `obj/`.
- Do not make broad formatting-only changes unless the user asks for them.
- Do not edit vendored code under `3rd-party/` unless the task specifically requires a dependency patch.
- Keep Visual Studio project files, filters, and the Makefile in sync when adding, moving, or removing source files.
- Treat `docs/plans/archive/imgvw_architecture_refactor_plan.md` as useful historical direction for larger refactors,
  especially around ownership, async loading, navigation safety, and tests. Check `docs/plans/README.md` for active
  plans first.
- Before staging or committing, inspect staged and unstaged changes separately. Preserve user-made staged changes unless
  the user explicitly asks to replace them.
- Use the repository's existing plain, imperative commit-message style; do not use Conventional Commit prefixes.
- For history rewrites, tag moves, force pushes, or replacing GitHub releases, create a backup ref first when practical,
  then verify the final tree/refs before pushing.
- When cleaning local branches, only delete branches that are merged or explicitly identified as disposable backup
  branches.
- When executing PowerShell commands, always use the `-NoProfile` flag (e.g., `powershell -NoProfile -ExecutionPolicy Bypass ...`) to prevent profile script loading and potential hangs.

## Release and Dependency Notes

- Release binaries currently statically link LGPL dependencies `libheif` and `libde265`; keep README/LICENSE/release
  notes consistent with the documented relinking path.
- Dependency rebuild scripts should be used instead of direct dependency build-system calls. When a script has both VS
  and MSYS legs, keep those environments isolated so MSYS CMake does not pick up MSVC tools.

# Agent Instructions

These instructions apply to the whole repository.

## Project Overview

ImgVw is a small, fast Windows image viewer written in C++ with Win32 UI code. Keep changes conservative and preserve the
documented Windows XP compatibility target unless the user explicitly asks to change it.

## Repository Layout

- `src/app/`: application startup and top-level app types.
- `src/ui/win32/`: Win32 windows, message handling, rendering integration, and UI command dispatch.
- `src/browse/`: image list state, folder enumeration, and navigation behavior.
- `src/image/`: image item abstractions, JPEG loading, image buffers, cache, and decode helpers.
- `src/platform/win32/`: Win32 platform helpers and RAII wrappers.
- `src/diagnostics/`: logging and debug helpers.
- `resources/`: Windows resources, manifest, icons, and `resource.h`.
- `3rd-party/`: vendored dependencies. Do not reformat or refactor these files.
- `artifacts/`: planning/reference material. Use it for context, but prefer the current source tree when it disagrees.

## Build and Verification

- Visual Studio build entrypoint: `ImgVw.slnx` / `ImgVw.vcxproj`.
- MSYS/Makefile build entrypoint: `make`.
- Repository build scripts are under `scripts/`. Prefer `scripts/build-msys.ps1` for MSYS application builds; it locates
  MSYS2, selects the architecture-specific shell/toolchain, invokes the Makefile, and verifies the output executable.
  For example:
  - `powershell -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean`
  - `powershell -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean`
- Dependency build scripts are `scripts/build-libjpeg-turbo.ps1` and `scripts/build-little-cms.ps1`. Use them when
  rebuilding vendored library artifacts instead of invoking dependency build systems manually.
- Useful Makefile variants:
  - `make`
  - `make config=release`
  - `make arch=x64`
  - `make arch=x64 config=release`
- The Makefile defines `WINVER=0x0501` and `_WIN32_WINNT=0x0501`; do not introduce newer Win32 APIs without guards or
  compatible fallbacks.
- When possible, verify changes with at least one local build path. If a toolchain is unavailable, state that clearly.

## Coding Style

- Follow `docs/CODING_STYLE.md`.
- Use the root `.clang-format` configuration for first-party C++ files.
- Use 4 spaces, no tabs, CRLF line endings, and a final newline.
- Keep lines at or below 120 columns where practical.
- Classes, structs, enums, and public functions use `CamelCase`.
- Private data members use `lower_case_` with a trailing underscore.
- Constants use the existing `kName` style.
- Avoid Hungarian notation in new first-party code except where Win32 API conventions make it clearer.

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
- Treat `artifacts/imgvw_architecture_refactor_plan.md` as useful direction for larger refactors, especially around
  ownership, async loading, navigation safety, and tests.

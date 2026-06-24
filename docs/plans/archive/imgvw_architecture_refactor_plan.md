# ImgVw Architecture, Style, and Safety Refactor Plan

## Summary

- Adopt **C++ Core Guidelines** as the project coding standard, using Microsoft's C++ Core Guidelines checker for MSVC and `clang-tidy` where available. Use `clang-format` with `BasedOnStyle: Microsoft`, which is supported by LLVM and Visual Studio.
- Preserve the current Windows XP compatibility promise, so refactoring must avoid newer Windows APIs unless guarded or replaced with compatible equivalents.
- Restructure the flat source layout into clear modules instead of keeping all first-party files at repo root.
- Exclude vendored `3rd-party/` code from formatting/lint ownership, but lint and format all first-party `.h`, `.cpp`, `.rc`-adjacent project code where tooling applies.

## Progress

- Completed:
  - First-party sources are under `src/`, `resources/`, and supporting module folders.
  - Project formatting and lint scripts are present and usable.
  - Visual Studio output is separated under `bin/vs/` and `obj/vs/`.
  - Simple clang-tidy quick wins have been applied where low-risk.
  - Paint/UI image paths no longer wait indefinitely for loader completion.
  - `ImgLoader` queues cache-owned `std::shared_ptr<ImgItem>` work instead of raw `ImgItem*`.
- Still open:
  - Broader RAII wrappers for Win32 handles, GDI selections, menus, events, and critical sections.
  - Deeper clang-tidy findings that need behavioral review, especially narrowing conversions and pointer casts.
  - Dedicated tests and CI-style build checks.
  - More explicit result/error objects at subsystem boundaries.
  - Shutdown waits still need bounded/error-aware handling.

References:

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Microsoft C++ Core Guidelines checker](https://learn.microsoft.com/en-us/cpp/code-quality/using-the-cpp-core-guidelines-checkers?view=msvc-170)
- [clang-format style options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [Visual Studio clang-format support](https://learn.microsoft.com/en-us/visualstudio/ide/reference/options-text-editor-c-cpp-formatting?view=visualstudio)

## Key Changes

- Add project standards:
  - Add `docs/CODING_STYLE.md` defining C++ Core Guidelines as the behavioral standard.
  - Add `.clang-format` with `BasedOnStyle: Microsoft`, 4-space indentation, no tabs, 120-column limit, sorted includes disabled initially to reduce churn.
  - Add `.clang-tidy` focused on `cppcoreguidelines-*`, `bugprone-*`, `modernize-*`, `performance-*`, and `readability-*`, with explicit suppressions for Win32-compatible raw API boundaries.
  - Update `.editorconfig` to match formatting defaults.

- Improve folder structure:
  - Move first-party sources into:
    - `src/app/` for `Program`, `ImgVw`, app startup.
    - `src/ui/win32/` for `Window`, `ImgVwWindow`, Win32 message/render integration.
    - `src/browse/` for folder enumeration, current item navigation, random order.
    - `src/image/` for `ImgItem`, decoders, image helpers, buffers, cache.
    - `src/platform/win32/` for Win32 RAII wrappers, file mapping, shell/file operations, temp paths.
    - `src/diagnostics/` for logging/debug helpers.
    - `resources/` for `.rc`, icons, manifest, and `resource.h`.
  - Keep `3rd-party/` isolated and not reformatted.
  - Update `ImgVw.vcxproj`, filters, and `Makefile` to reflect the new layout.

- Refactor architecture:
  - Keep `ImgVwWindow` responsible only for Win32 message handling, rendering requests, and UI command dispatch.
  - Introduce internal services:
    - `ImageBrowser` for file list state and navigation.
    - `ImageLoader` for async loading and worker ownership.
    - `ImageRenderer` for converting ready image data into displayable GDI resources.
    - `FileOperations` for recycle/delete behavior.
    - `IccProfileStore` for loading/selecting default ICC profile.
  - Replace raw ownership with RAII wrappers for `HANDLE`, `HBITMAP`, `HDC` selections, `HMENU`, critical sections, and events.
  - Replace queued raw `ImgItem*` with stable ownership, preferably `std::shared_ptr<ImgItem>` or item IDs backed by cache-owned objects.
  - Remove indefinite waits from paint paths. Loader completion should notify the UI via `PostMessage`, and painting should display ready/error/loading state without blocking.

- Fix known defects during refactor:
  - Fix `MoveToRandom()` deadlock by avoiding nested critical-section acquisition.
  - Add bounds-checked file path handling for delete and ICC profile selection.
  - Normalize error handling into explicit result/status objects at subsystem boundaries, with Win32 `GetLastError()` captured where relevant.
  - Ensure thread shutdown handles null/invalid handles safely and does not wait forever without cancellation.

## Public Interfaces / Types

- No external public API is required; this is an internal desktop app refactor.
- Add internal boundary types:
  - `ImageLoadState { Queued, Loading, Ready, Error }`.
  - `ImageLoadResult` carrying state, optional error message/code, dimensions, and display buffer metadata.
  - `NavigationCommand` or equivalent command enum for next/previous/first/last/random.
  - `Win32Handle`-style RAII wrappers under `src/platform/win32/`.
- Do not expose Win32 handles outside `ui/win32` and `platform/win32` except where unavoidable for rendering.

## Test Plan

- Add a first-party console test target under `tests/` using a lightweight in-repo test harness to avoid new runtime dependencies.
- Unit test:
  - extension-to-format detection;
  - path normalization for file vs folder startup;
  - next/previous/first/last/random navigation, including single-item and empty-folder cases;
  - `MoveToRandom()` no-deadlock behavior;
  - temp-path creation/deletion logic through injectable platform seams;
  - delete path buffer construction for long paths and double-null termination.
- Add build checks:
  - MSVC Debug and Release build.
  - MSYS/Makefile Debug and Release build.
  - `clang-format --dry-run --Werror` for first-party files.
  - `clang-tidy` on first-party files where compile commands are available.
  - MSVC `/analyze` with C++ Core Guidelines checker enabled for the Visual Studio project.

## Assumptions

- Preserve Windows XP compatibility.
- Keep both Visual Studio and MSYS/Makefile builds working unless a later decision explicitly removes one.
- Formatting/lint fixes apply to first-party code only; vendored `3rd-party/` code remains untouched.
- Prefer staged commits: style/tooling first, folder moves second, behavior-preserving RAII refactor third, async/UI architecture fixes fourth, tests throughout.


# ImgVw C++ Coding Style

ImgVw follows the C++ Core Guidelines as its engineering baseline, with Microsoft-style formatting enforced by
`clang-format`.

## Formatting

- Use `.clang-format` at the repository root.
- Use 4 spaces for indentation and no tabs.
- Keep lines at or below 120 columns where practical.
- Do not reformat files under `3rd-party/`.

Run formatting before committing first-party C++ or resource changes:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\format.ps1
```

To check formatting without modifying files:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\format.ps1 -Check
```

## Naming

- Classes, structs, enums, and public functions use `CamelCase`.
- Private data members use `lower_case_` with a trailing underscore.
- Constants use the existing `kName` style.
- Avoid Hungarian notation in new first-party code except where Win32 APIs require established names.

## Architecture

- Keep Win32 API use in `src/ui/win32/` and `src/platform/win32/` where practical.
- Keep image decoding and image state independent from window message handling.
- Prefer RAII wrappers for Win32 handles, GDI objects, mapped views, critical sections, and heap allocations.
- Do not block the UI thread on worker-thread completion; use status objects and window notifications.

## Error Handling

- Do not leave ignored Win32 failures in new code.
- Capture `GetLastError()` near the failing API call.
- Use explicit result/status objects at subsystem boundaries instead of mixing exceptions, flags, and silent failure.

## Compatibility

- Preserve Windows XP-compatible behavior unless a change explicitly updates the documented compatibility target.
- Prefer source that builds for both Win32 and x64.

## Static Analysis

Use `.clang-tidy` with the repository helper script:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\tidy.ps1
```

To apply safe automatic fixes:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\tidy.ps1 -Fix
```

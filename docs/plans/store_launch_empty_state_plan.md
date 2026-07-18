# Store Launch and Empty-State Experience Plan

Date: 2026-07-17

## Summary

ImgVw currently treats an empty startup path as an instruction to browse the process current directory. That behavior
is not appropriate for a Microsoft Store launch: a packaged desktop application does not receive a meaningful
user-selected working directory, and the observed directory can be `C:\Windows\System32`. ImgVw then searches that
directory, may search its subfolders, reports that no images were found, displays command-line usage, and exits.

Replace that fallback with an explicit empty-state experience. A no-argument launch should open ImgVw in a usable state
that offers **Open image...** and **Open folder...** actions. The same surface should provide recovery when a selected
folder contains no supported images. Once an image collection is open, the application should retain its current clean,
full-screen viewer behavior.

This is a focused usability and lifecycle change, not a general UI redesign. Preserve Windows XP compatibility for the
portable build and do not add Store capabilities merely to compensate for the current-directory behavior.

## Goals

- Make a Start menu or Microsoft Store launch useful when no path is supplied.
- Stop deriving user content from the process current directory.
- Let users explicitly choose an image or folder without leaving ImgVw.
- Keep an empty or unsuccessful browse operation recoverable instead of closing the application.
- Reuse the open actions from the existing context menu and keyboard accelerators.
- Preserve file-association activation, command-line file/folder launch, and the distraction-free viewer.
- Preserve the documented Windows XP compatibility target through guarded modern APIs or compatible fallbacks.
- Make replacing the active browse path safe with respect to collection, loading, cancellation, and cached state.

## Non-Goals

- Do not add a permanent toolbar, navigation sidebar, ribbon, address bar, or thumbnail browser.
- Do not replace the Win32 window framework or migrate the application to WinUI, WPF, or another UI toolkit.
- Do not automatically browse the Pictures folder, Documents folder, package directory, executable directory, or any
  other inferred location.
- Do not add `broadFileSystemAccess` or another MSIX capability unless a separately demonstrated requirement justifies
  it.
- Do not add a recent-items database in the initial implementation.
- Do not implement multi-file command-line activation as part of the initial empty-state work. Record and test the
  existing single-path behavior rather than implying that extra arguments are supported.
- Do not change supported image formats or image-decoding behavior.
- Do not automatically recurse through an empty folder without a clear user action.

## Current-State Assessment

### Startup and browsing

- `ImgVw::Run()` parses the process command line and passes all arguments to `ImgVwWindow::Create()`.
- The primary window consumes only the first path argument. When no path is present, `path_` remains empty and
  `launchedwithoutarguments_` is set.
- `ImgVwWindow::OnCreate()` always initializes the browser, including for an empty path.
- `ImgBrowser::BrowseAsync()` turns an empty path into `GetCurrentDirectory()` and begins folder collection.
- After collection completes with no images, `HandleStartupExitConditions()` attempts subfolder browsing and then
  displays a message before closing the window.

The browser layer should not assign semantic meaning to an empty path. The application/controller layer should decide
whether a startup request contains a path and should enter empty mode when it does not.

### Existing UI behavior

- The main window is a borderless `WS_POPUP` sized to the monitor.
- The primary window captures the mouse and hides the cursor during normal viewing.
- Painting already supports text overlays and DPI-aware measurements, but there are no persistent child controls in the
  main window.
- Commands are exposed through an accelerator table and a right-click popup menu.
- The only existing file-selection dialog chooses a custom ICC profile; there is no image or folder picker.
- There is no `WM_DROPFILES` or OLE drop-target handler.

### Store package behavior

- The Store manifest declares a full-trust desktop entry point and supported image file associations.
- File-association launch should continue passing an explicit image path to the executable.
- The package does not need a working-directory fixup for image browsing. Browsing should always start from an explicit
  activation path or an in-app user selection.

## User Experience Design

### No-argument launch

Show the normal ImgVw window with a centered empty-state panel:

```text
ImgVw

Open an image or browse a folder to begin.

[ Open image... ]  [ Open folder... ]

You can also drag an image or folder here.
```

The drag-and-drop sentence should be omitted until drag-and-drop is actually implemented. Do not advertise planned
behavior in a released UI.

Required interaction behavior:

- Show the arrow cursor and do not capture the mouse while the empty state is active.
- Give the first action normal keyboard focus.
- `Tab` and `Shift+Tab` move between actions.
- `Enter` or `Space` activates the focused action through normal button behavior.
- `Ctrl+O` opens the image picker.
- `Ctrl+Shift+O` opens the folder picker.
- `Escape` exits, preserving the current viewer convention.
- Cancelling either picker returns to the empty state without closing the application.
- Resizing or a DPI change keeps the content centered and usable.

Use standard Win32 `BUTTON` child controls for the initial implementation. Native controls provide keyboard focus,
accessibility semantics, disabled/focused states, and XP-compatible behavior without building a custom hit-testing and
focus system. Paint the heading, explanatory text, and optional status text in the parent window. Keep the controls
visible only in empty mode.

### Successful open

After the user selects a supported image or folder:

1. Hide the empty-state controls.
2. Start or replace the browse session with the selected explicit path.
3. Restore normal viewer capture and cursor behavior when the first collection is ready to display.
4. Preserve normal adjacent-image enumeration, navigation, caching, and slideshow behavior.

If opening fails synchronously because the selected path is missing or inaccessible, remain in empty mode and show a
concise error on the surface or in an error message box. Do not destroy the window.

### No images found

When an explicitly selected folder finishes collecting with no supported images, reuse the empty-state surface:

```text
No supported images were found in this folder.

<selected folder>

[ Choose another folder... ]  [ Open an image... ]
[ Search subfolders ]
```

Requirements:

- Do not close the application.
- Do not display command-line usage in a graphical Store launch.
- Do not automatically recurse. Let the user choose **Search subfolders**.
- Disable or hide **Search subfolders** after recursion has already completed.
- If recursive collection finds an image, transition to normal viewing.
- If recursive collection still finds nothing, retain the same recoverable state and update its status.

An explicitly supplied unsupported file should produce a specific unsupported-image error and a recoverable empty state.
An invalid command-line path may still show an error, but the preferred behavior is to keep the main window open so the
user can choose another path.

### Commands available while viewing

Add the following entries near the top of the existing context menu:

- **Open image...** (`Ctrl+O`)
- **Open folder...** (`Ctrl+Shift+O`)
- separator

These actions should replace the active collection in the same window. Cancelling a picker while an image is already
open must leave the current collection and image unchanged.

## Application State Model

Represent the top-level UI state explicitly instead of inferring it from `path_.empty()` or
`browser_.GetCurrentItem() == nullptr`. The latter can also mean that collection is in progress.

Suggested states:

```text
Empty
SelectingPath
Collecting
Viewing
NoImages
OpenError
Closing
```

A smaller enum is acceptable if the same transitions remain unambiguous. At minimum, distinguish:

- no path has been chosen;
- an explicit path is collecting;
- a collection has no current item yet but may still produce one;
- collection completed with no images;
- an image is available for viewing.

Centralize transitions in `ImgVwWindow`. The browser should report collection state and results, while the window owns
cursor, capture, control visibility, command availability, and error presentation.

Do not use `launchedwithoutarguments_` as the long-term state flag. The activation source matters only when choosing the
initial state; later behavior should depend on the current session state.

## Path Picker Design

Add a small Win32 path-picker abstraction under `src/platform/win32/`, for example:

```text
src/platform/win32/PathPicker.h
src/platform/win32/PathPicker.cpp
```

Suggested interface:

```cpp
enum class PathPickerStatus
{
    Selected,
    Cancelled,
    Failed,
};

struct PathPickerResult
{
    PathPickerStatus status;
    std::wstring path;
    DWORD error;
};

class PathPicker
{
  public:
    PathPickerResult SelectImage(HWND owner);
    PathPickerResult SelectFolder(HWND owner);
};
```

The final error representation may use `HRESULT` where appropriate, but cancellation must remain distinct from failure.
Keep dialog mechanics out of `ImgVwWindow` and keep browsing out of the picker.

### Modern Windows path

On Windows Vista and later, prefer `IFileOpenDialog`:

- Set filters matching the extensions accepted by `ImgItemHelper` and the Store manifest.
- Use `FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM` for image selection.
- Add `FOS_PICKFOLDERS` for folder selection.
- Set the owner window.
- Retrieve a filesystem path and reject non-filesystem Shell items.
- Treat the user-cancel HRESULT as `Cancelled`, not an error.

The existing application COM initialization can support the dialog. Do not assume the modern class exists on XP.

### Windows XP fallback

When `IFileOpenDialog` is unavailable:

- Use `GetOpenFileNameW` for image selection.
- Use `SHBrowseForFolderW` and `SHGetPathFromIDListW` for folder selection.
- Add `OFN_NOCHANGEDIR` to the legacy file dialog so it cannot mutate process-wide current-directory state.
- Use a sufficiently bounded buffer and report buffer-related failures rather than truncating paths silently.
- Keep all calls within the existing XP API target.

Use runtime capability detection or a failed `CoCreateInstance` fallback so the portable executable remains loadable on
XP. Do not introduce an unconditional import of a newer helper API.

## Browse-Session Lifecycle

Opening a path after startup is more than assigning `path_` and calling `BrowseAsync()` again. Current browser reset and
shutdown behavior does not define a complete, reusable session replacement boundary.

Add an explicit operation such as `ImgBrowser::OpenPath()` or harden `BrowseAsync()` to provide these guarantees:

1. Validate that the input path is non-empty. Empty is an invalid browser request, not an alias for CWD.
2. Signal cancellation for any active folder collection and target-size queueing.
3. Wait using the browser's defined bounded shutdown policy.
4. Close completed collector/queue handles safely.
5. Clear the file list, cached selection, pending subfolders, folder path, recursion state, target sizes, and stale
   notifications belonging to the previous generation.
6. Preserve or safely restart the loader infrastructure required by the new session.
7. Assign a session/generation identifier if old worker notifications can arrive after replacement.
8. Start collection only after the new session state is fully initialized.
9. Return an explicit result that distinguishes invalid path, inaccessible path, startup failure, and successful
   asynchronous collection.

Coordinate this work with `docs/plans/imgvw_stability_refactor_plan.md`. Do not add a second ad hoc cancellation scheme
that worsens the existing lifetime risks. If safe session replacement depends on the planned browser cancellation
hardening, implement that prerequisite first or keep path selection limited to the initial empty state until it exists.

The window should preserve the existing collection until a picker returns `Selected`. Once replacement begins, failure
must leave the application in a defined empty/error state; it must not display stale items from a partially reset
session.

## Current-Directory Policy

Adopt these rules throughout the application:

- No startup path means `Empty` UI state.
- An explicit `.` argument means browse the current directory because the caller deliberately supplied it.
- Relative explicit paths remain supported for command-line callers and resolve according to normal Win32 rules.
- File and folder pickers return explicit absolute filesystem paths.
- `ImgBrowser` never calls `GetCurrentDirectory()` to invent a browse target.
- Do not call `SetCurrentDirectory()` as part of opening or dialog handling.

This preserves useful command-line semantics without coupling graphical activation to process launch context.

## Cursor, Capture, and Focus

Introduce named helpers for entering and leaving interactive empty mode. Avoid balancing scattered `ShowCursor(TRUE)`
and `ShowCursor(FALSE)` calls, because the Win32 cursor visibility API uses a process counter and is easy to imbalance.

The state transition should define:

- whether this window owns mouse capture;
- whether it wants the cursor visible;
- which child control receives focus;
- whether dragging the borderless viewer window is enabled;
- whether navigation, deletion, slideshow, and recursion commands are enabled.

In empty mode:

- release capture if held;
- show the cursor;
- disable image-only commands;
- route context-menu and accelerator open commands normally.

In viewing mode:

- hide empty controls;
- restore capture/cursor behavior only for the primary viewer window;
- do not apply empty-mode transitions to secondary slideshow windows.

Test repeated picker open/cancel cycles for cursor counter drift.

## Painting and Layout

Keep layout DPI-aware and simple:

- Use the existing message font or another system UI font.
- Center a bounded content region in the client rectangle.
- Scale margins and control sizes using the existing per-window DPI helper.
- Recalculate positions on `WM_SIZE` and `WM_DPICHANGED`.
- Use high-contrast foreground/background colors and avoid encoding state through color alone.
- Ensure explanatory text does not overlap buttons at small resolutions or high DPI.
- Do not use resource-script dialogs for the main empty surface; it must occupy and resize with the existing main
  window.

Create the buttons once with the main window and show/hide/reposition them as state changes. Do not create and destroy
them on every paint or browse notification.

Add resource IDs for the new controls and commands. Keep `resources/ImgVw.rc` in its existing ANSI/Windows-1252 encoding
and CRLF line endings, and verify it with a build path that invokes the resource compiler.

## Drag-and-Drop Follow-Up

Drag-and-drop is useful but should be a follow-up after the empty state and session replacement are stable.

The conservative implementation is `DragAcceptFiles` plus `WM_DROPFILES`, which remains compatible with XP. Define
these behaviors before enabling the hint text:

- One dropped supported image opens that image and enumerates adjacent images as usual.
- One dropped folder opens that folder.
- Unsupported or mixed drops produce a concise error without losing the current collection.
- Multiple dropped files are either handled deliberately or rejected as a group; do not silently use the first while
  implying full multi-file support.
- Always call `DragFinish` and bound path-buffer handling.

Validate Explorer-to-packaged-app drops on supported Windows 10 and Windows 11 versions. Do not add OLE drag-and-drop
complexity unless `WM_DROPFILES` fails a demonstrated requirement.

## Files Expected To Change

Initial implementation will likely touch:

- `src/app/ImgVw.h`
- `src/ui/win32/ImgVwWindow.h`
- `src/ui/win32/ImgVwWindow.cpp`
- `src/browse/ImgBrowser.h`
- `src/browse/ImgBrowser.cpp`
- `src/platform/win32/PathPicker.h` (new)
- `src/platform/win32/PathPicker.cpp` (new)
- `resources/resource.h`
- `resources/ImgVw.rc`
- `ImgVw.vcxproj`
- `ImgVw.vcxproj.filters`
- `Makefile`
- tests under `tests/`
- `README.md` or Store-facing usage documentation

Keep Visual Studio and Makefile source lists synchronized when adding the picker files.

## Testing Strategy

### Unit and component tests

Extract and test state/lifecycle decisions where practical:

- no arguments select `Empty`, not a browser request;
- an explicit empty string is rejected by the browser boundary;
- explicit `.`, relative files, absolute files, and folders retain their intended semantics;
- picker cancellation preserves the existing collection;
- successful selection replaces the session exactly once;
- stale notifications from an earlier session do not change the new state;
- empty direct collection exposes the no-images state;
- recursive search is user initiated and can transition to viewing;
- repeated open operations clear old pending subfolder and recursion state.

Use a fake picker or inject selection results into controller-level tests. Unit tests should not automate the native
Shell dialogs.

### Manual unpackaged tests

- Launch `ImgVw.exe` without arguments from the repository, `C:\Windows\System32`, a folder containing images, and an
  empty folder. Every no-argument launch should show the same empty state.
- Cancel both pickers repeatedly.
- Open every supported image type through the image picker.
- Open local, removable, read-only, UNC, non-ASCII, and space-containing paths where supported.
- Open an empty folder, then search subfolders, then choose another folder.
- Replace an active collection while enumeration or decoding is in progress.
- Exercise context-menu commands and keyboard accelerators in empty, collecting, no-images, and viewing states.
- Verify focus, cursor, capture, resizing, high DPI, and multiple-monitor behavior.
- Verify the x86 portable build on Windows XP SP3, including both picker fallbacks.

### Packaged Store tests

Install a signed development package matching the Store manifest and test:

- Start menu launch with no arguments;
- launch from the Microsoft Store installation result;
- each declared file association through **Open with** and default-app activation;
- in-app image and folder selection;
- picker cancellation;
- path replacement while viewing;
- empty and inaccessible folders;
- recursive browsing;
- delete/recycle permissions after picker selection;
- x86 and x64 packages on their supported systems;
- Explorer drag-and-drop after that phase is implemented.

Confirm that no new capability declaration is required and that the package still passes validation.

## Implementation Sequence

### Phase 1: Correct startup semantics

1. Add an explicit top-level UI/session state.
2. Make no-argument startup enter `Empty` without initializing `ImgBrowser`.
3. Reject empty browse paths inside the browser boundary.
4. Remove the `GetCurrentDirectory()` fallback.
5. Replace command-line usage-and-exit behavior with a recoverable state.

This phase fixes the Store launch defect even before path replacement while viewing is enabled.

### Phase 2: Empty-state UI and initial selection

1. Add the centered text and native Open image/Open folder buttons.
2. Add DPI-aware layout and focus handling.
3. Centralize cursor/capture transitions between empty and viewing modes.
4. Add the path-picker abstraction with modern dialogs and XP fallbacks.
5. Open a selected initial path and transition to the existing viewer.

### Phase 3: Safe session replacement

1. Harden browser cancellation/reset according to the stability plan.
2. Add a complete replace-session operation and stale-notification protection.
3. Add context-menu entries and accelerators.
4. Preserve the current session when picker selection is cancelled.
5. Validate replacement during active collection and image loading.

### Phase 4: No-images recovery

1. Remove automatic recursive fallback.
2. Reuse the empty surface for completed zero-image collections.
3. Add the user-initiated Search subfolders action.
4. Keep invalid, inaccessible, and unsupported-path errors recoverable.

### Phase 5: Drag-and-drop and optional polish

1. Add and test one-image and one-folder `WM_DROPFILES` handling.
2. Enable the drag-and-drop hint only after the behavior ships.
3. Consider one recent location only as a separate product decision, with missing/network/removable-path handling and a
   clear way to remove it. Do not automatically reopen it by default in this plan.

## Build Verification

Run at minimum:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\format.ps1 -Check
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x64 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
```

Also build Visual Studio Release Win32 and x64 because the change touches resources, COM/Shell APIs, and project files.
Run Store package generation and validation after both release executables pass.

## Risks and Mitigations

### Browser lifetime regressions

Replacing a collection while worker activity is in flight can expose existing cancellation and ownership weaknesses.
Treat safe replacement as a browser lifecycle feature and coordinate it with the stability plan. Use session generation
checks where queued notifications can outlive a browse request.

### Cursor visibility imbalance

Existing `ShowCursor` calls are distributed across dialogs and interactions. Centralize state transitions and test
repeated open/cancel/error cycles. Do not assume one `ShowCursor(TRUE)` balances arbitrary prior calls.

### XP loader/import failure

Modern Shell interfaces must be optional at runtime. Verify the final import table and execute the fallback path on XP,
not only a modern build configured with `WINVER=0x0501`.

### Resource-file encoding churn

Edit the resource file with encoding-preserving operations, avoid broad formatting, and verify with both MSYS `windres`
and the Visual Studio resource compiler.

### Accidental UX expansion

Keep controls limited to empty/error states and the existing context menu. Once viewing begins, hide the new surface and
preserve the current distraction-free UI.

## Acceptance Criteria

The work is complete when:

- Launching the packaged or unpackaged app without arguments never browses `C:\Windows\System32` or any other inferred
  current directory.
- A no-argument launch presents visible, keyboard-accessible Open image and Open folder actions.
- Cancelling a picker leaves ImgVw open and usable.
- Selecting a supported image or folder transitions to the existing viewer behavior.
- An empty folder produces a recoverable no-images state with an explicit Search subfolders action.
- Open image and Open folder are available from the context menu and accelerators while viewing.
- Replacing a path during active work does not leak, race, display stale content, or use state from the previous
  session.
- File-association and explicit command-line launches continue to work.
- The cursor, capture, focus, and controls are correct in empty, collecting, viewing, and error states.
- No additional Store filesystem capability is introduced for ordinary user-selected paths.
- Visual Studio and MSYS release builds pass for x86 and x64.
- The Store development package passes no-argument, file-association, picker, empty-folder, and recursive-browse tests.
- The portable x86 build and picker fallbacks are verified on Windows XP SP3.

## References

- Microsoft: Prepare to package a desktop application (working-directory guidance):
  <https://learn.microsoft.com/windows/msix/desktop/desktop-to-uwp-prepare>
- Microsoft: MSIX containerization overview:
  <https://learn.microsoft.com/windows/msix/msix-containerization-overview>
- Microsoft: `SHBrowseForFolder` and the recommendation to use `IFileDialog` on Vista and later:
  <https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shbrowseforfolderw>
- Existing Store publication plan: `docs/plans/microsoft_store_publication_plan.md`
- Existing browser/thread lifetime plan: `docs/plans/imgvw_stability_refactor_plan.md`

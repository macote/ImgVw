# Cloud File Download Consent

Date: 2026-07-27

## Status

**Active; high priority.** ImgVw currently treats every enumerated image as locally readable. Folder discovery performs
a content probe and the loader prefetches adjacent images, so opening a folder containing online-only files can recall
file data before the user selects an image.

## Goal

Detect image content that is not fully present locally and obtain explicit user consent before ImgVw performs any
operation that can download that content.

The design must:

- work with OneDrive and other providers built on Windows Cloud Files or compatible hierarchical-storage attributes;
- preserve the documented Windows XP target and the existing `WINVER=0x0501` build;
- avoid reading file content during placeholder detection;
- prevent header probing, decoding, cache loading, and preloading before consent;
- keep the UI responsive while a provider retrieves content;
- fit ImgVw's full-screen, keyboard-first UI;
- retain normal behavior and performance for local images;
- avoid persistent global permission by default.

Call the feature **online-only image handling** in user-facing text. Do not infer OneDrive from paths, environment
variables, registry locations, account names, or branding.

## Non-Goals

The initial implementation does not:

- implement a cloud sync provider;
- authenticate to OneDrive or call Microsoft Graph;
- pin files permanently or change the user's Files On-Demand policy;
- dehydrate files after viewing;
- provide bandwidth limits or a general download manager;
- guarantee byte-level progress from every provider;
- change ImgVw's Windows XP minimum target.

## Current Download Triggers

The implementation must gate all of these paths:

1. `FolderScanner` calls `ImgFormatResolver::Resolve()` for every candidate.
2. `ImgFormatResolver::Resolve()` calls `ImgHeaderProbe::ReadPrefix()`, which opens and reads the file.
3. `ImgBrowser` queues discovered images for all active target sizes.
4. `ImgLoader` calls the decoder-specific `ImgItem::Load()` on a worker.
5. JPEG and HEIF loading map the complete source file; GDI+ opens other source images.
6. Sequential, random, and multi-monitor slideshows preload images that have not yet been displayed.

Checking only immediately before decode is insufficient because the header probe can already cause a recall. Checking
only in the folder scanner is also insufficient because direct file activation, drag and drop, reload, additional
target sizes, and cached browser paths can reach the loader independently.

## User Experience

### Default prompt

When the selected image requires remote content, pause its load and show a centered panel using the visual language of
`EmptyStateView`:

```text
Online-only image

vacation-042.heic
24.8 MB

ImgVw must download this image to display it.

[ Download ]  [ Skip ]

[ ] Allow downloads while browsing this folder
```

Requirements:

- Focus **Download** initially only when the user explicitly opened that file. When the image was reached through
  folder navigation or a slideshow, focus **Skip** initially.
- `Enter` activates the focused button.
- `Tab`, `Shift+Tab`, arrow keys, and the mouse operate the controls.
- Normal Previous and Next commands remain available while the panel is visible.
- `Skip` moves in the current navigation direction without reading the file.
- A skipped path remains marked as skipped for the browse session so random navigation does not immediately select it
  again. Navigating to it explicitly may show the panel again.
- The checkbox grants permission only for the current browse root and only until another path is opened or the window
  closes.
- Folder permission allows a cloud image to load when it becomes the selected image. It does not permit speculative
  cloud preloading.
- Direct activation of one online-only file uses **Download** and **Cancel** because there may be no meaningful next
  image yet.

Do not use a `MessageBox` for the normal consent flow. Modal prompts interrupt wheel navigation and slideshows and do
not match the existing empty-state surface.

### Retrieval and errors

After approval:

- replace the consent panel with `Downloading <filename>...`;
- use determinate progress only when the platform/provider supplies trustworthy progress;
- otherwise show an indeterminate activity state rather than a false percentage;
- allow navigation away without blocking the UI;
- report offline, authentication, provider, disk-space, and cancellation failures as a recoverable panel with
  **Retry** and **Skip**;
- never convert a failed download into a decoder-format error.

If a folder has no immediately available images but contains online-only images, show:

```text
This folder contains 12 online-only images.

[ Review online-only images ]  [ Choose another folder... ]
```

### Slideshows

- A slideshow pauses when the selected image needs consent.
- **Skip** resumes the slideshow at the next eligible image.
- Folder-session approval lets the slideshow retrieve each cloud image only when selected.
- Random cycles count skipped cloud paths as unavailable for the remainder of that cycle.
- Multi-monitor slideshow state and permission belong to the owner window. Secondary windows must not initiate an
  unapproved load independently.
- If consent is required on a secondary target, pause the coordinator and present the request through the owner window,
  including the filename. Resume only after the owner records a decision.

## Availability Model

Introduce a provider-neutral platform result:

```cpp
enum class FileAvailability
{
    Local,
    RecallRequired,
    PartiallyAvailable,
    Unknown
};

struct FileAvailabilityResult
{
    FileAvailability availability{FileAvailability::Unknown};
    ULONGLONG logical_size{};
    DWORD attributes{};
    DWORD reparse_tag{};
    DWORD win32_error{ERROR_SUCCESS};
};
```

Interpretation:

- `Local`: content may be read normally.
- `RecallRequired`: opening or reading is expected to retrieve remote content.
- `PartiallyAvailable`: some content is local, but ImgVw's full-file decode can still retrieve data; require consent.
- `Unknown`: detection failed or the provider state is ambiguous. Do not silently read when recall-related attributes
  are present; otherwise retain legacy local behavior.

The logical size is display metadata, not proof that bytes are local.

Do not treat a cloud reparse tag or `FILE_ATTRIBUTE_UNPINNED` alone as proof that a download is required. A cloud
placeholder may already be fully hydrated, and unpinned expresses retention intent rather than sufficient local
availability. Partial/recall state takes precedence over pinned state.

## Platform Detection

Add an XP-compatible helper under `src/platform/win32/`, for example `CloudFileAvailability`.

### Modern path

On Windows 10 version 1709 and later:

1. Obtain `WIN32_FIND_DATA` while enumerating a folder.
2. Dynamically load `CldApi.dll` with `LoadLibraryW`.
3. Resolve `CfGetPlaceholderStateFromFindData` or `CfGetPlaceholderStateFromAttributeTag` with `GetProcAddress`.
4. Interpret `CF_PLACEHOLDER_STATE_PARTIAL` and `CF_PLACEHOLDER_STATE_PARTIALLY_ON_DISK` as requiring consent.
5. Cache function discovery once per process; unload only during normal process teardown, if at all.

Do not link `CldApi.lib` and do not add a static import from `CldApi.dll`.

### Compatibility fallback

When Cloud Files APIs are unavailable, inspect enumeration metadata without opening the file:

- `FILE_ATTRIBUTE_RECALL_ON_OPEN`;
- `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`;
- `FILE_ATTRIBUTE_OFFLINE`;
- `FILE_ATTRIBUTE_REPARSE_POINT` and `WIN32_FIND_DATA::dwReserved0`;
- cloud and legacy placeholder reparse tags when defined by the build SDK.

Use guarded private definitions for newer attribute bits and tags if an XP-compatible SDK does not declare them. Keep
those definitions inside the Win32 platform implementation rather than leaking them into browse or image code.

`FILE_ATTRIBUTE_OFFLINE` exists on Windows XP and may represent legacy Remote Storage rather than OneDrive. Treat it as
recall-required because reading it can still retrieve remote content.

For a directly supplied file path, use metadata-only APIs. Do not open a data handle merely to classify availability.
If a reparse tag is required, use a path enumeration/query that does not request file data.

## Windows XP Compatibility Contract

The feature is acceptable only if all of the following remain true:

1. `WINVER` and `_WIN32_WINNT` remain `0x0501`.
2. The executable has no static import from `CldApi.dll`.
3. No unguarded import newer than Windows XP is introduced.
4. Modern Cloud Files entry points are accessed only through `LoadLibraryW` and `GetProcAddress`.
5. Absence of `CldApi.dll` is a normal capability result, not an error dialog.
6. New UI uses controls and messages available on XP. Do not require Task Dialog APIs.
7. Newer constants are compile-time values only and are guarded when absent from the SDK.
8. The fallback does not call `GetFileInformationByHandleEx`, `CancelIoEx`, or another post-XP API without dynamic
   discovery and a compatible fallback.
9. XP local-file browsing remains behaviorally identical when no offline/recall attributes are present.
10. Release import tables are inspected and the x86 release build is exercised on the XP SP3 test VM.

The Store package may run only on modern Windows, but the portable executable must continue to satisfy this contract.

## Browse Model Changes

Replace the path-only discovery boundary with an entry that carries non-content metadata:

```cpp
struct BrowseFile
{
    std::wstring path;
    ImgItem::Format extension_format{ImgItem::Format::Unsupported};
    FileAvailability availability{FileAvailability::Unknown};
    ULONGLONG logical_size{};
};
```

Exact ownership may differ, but the following properties are required:

- `FolderScanner` passes the `WIN32_FIND_DATA`-derived availability and logical size with each discovered file.
- `ImgFileList` keeps stable ordering and random-cycle behavior while retaining availability.
- Direct file activation constructs equivalent metadata before queueing.
- Availability is rechecked before an approved load because a provider can hydrate or dehydrate a file after
  enumeration.
- Permission and skip decisions are browse-session state, not properties of cache entries.
- Cache keys remain path and target size; an unapproved file never enters a loading cache state.

## Format Resolution Without Recall

Split format handling into two stages:

1. **Extension classification:** no file open; safe for every enumerated path.
2. **Content confirmation:** existing bounded header probe; allowed only for local or approved content.

For an online-only path:

- retain the extension-derived format while consent is pending;
- after approval and retrieval, run content detection before final decoder dispatch;
- preserve the current extension fallback when content detection cannot identify a supported signature;
- never read a header merely to decide whether the consent panel should be shown.

This split must retain support for mislabeled local files without recalling mislabeled cloud files during scanning.

## Load Authorization Boundary

Add an explicit authorization result immediately before any source-content open:

```cpp
enum class ContentReadAuthorization
{
    AllowedLocal,
    AllowedBySession,
    ConsentRequired,
    Skipped
};
```

The loader must reject `ConsentRequired` as a scheduling state rather than converting it to `ImgItem::Status::Error`.
Authorization must be checked centrally so new decoders cannot bypass it.

Rules:

- local images retain normal target-size preloading;
- cloud images are never preloaded speculatively;
- current-selection loading is queued only after authorization;
- reload requires authorization again unless the browse-session grant remains valid;
- permission is revoked when opening another browse root, clearing the browser, or destroying the owner window;
- a path becoming recall-required after queueing is re-gated before its first content read.

## Hydration Strategy

### Initial implementation

After consent, allow the existing loader worker to open the source. This lets the registered provider service the
normal read without adding a hard dependency on Cloud Files APIs.

The UI must regard the operation as retrieval until the source can be opened and must not block a paint or message
handler waiting for it. Existing loader shutdown timeouts and ownership rules still apply.

### Modern optional enhancement

On supported systems, dynamically resolve `CfHydratePlaceholder` and hydrate asynchronously before decoder dispatch.
This may improve cancellation and error classification, but it is not required for the first safe release.

If implemented:

- do not link `CldApi.lib`;
- use a dedicated state object owned independently of the window;
- never wait synchronously from the UI thread;
- treat `ERROR_IO_PENDING` as normal asynchronous progress;
- use only dynamically discovered cancellation APIs and retain a fallback that is valid on XP;
- close handles and discard late completion notifications safely after navigation or shutdown;
- recheck placeholder state before decode.

Do not pin or dehydrate the file. Hydration should have the same persistence semantics as an ordinary user-approved
read through the provider.

## Implementation Phases

### Phase 1: Detection and pure policy

1. Add availability result types and pure attribute/tag classification.
2. Add optional runtime Cloud Files API discovery.
3. Split extension classification from content probing.
4. Add tests for local, offline, recall-on-open, recall-on-data-access, partial, pinned, unpinned, ambiguous, and
   unavailable-API cases.
5. Verify no test classification path opens file content.

### Phase 2: Browse and preload gating

1. Carry availability and logical size from `FolderScanner`.
2. Extend browser entries without changing path ordering.
3. Gate header probes and every loader scheduling path.
4. Disable speculative cloud preloads for all target sizes and slideshow modes.
5. Recheck availability at the final authorization boundary.
6. Add concurrency tests proving an unapproved fake cloud item never calls `Load()`.

### Phase 3: Consent UI

1. Extend the centered panel with Download, Skip/Cancel, and an XP-compatible checkbox.
2. Add consent, downloading, and retrieval-error presentation states.
3. Connect keyboard, mouse, empty-folder, direct-open, and navigation behavior.
4. Store folder permission and skipped paths in browse-session state.
5. Coordinate owner and secondary slideshow windows.

### Phase 4: Retrieval robustness

1. Separate retrieval failures from decoder failures.
2. Ensure navigation and shutdown remain responsive during provider delays.
3. Add optional asynchronous `CfHydratePlaceholder` support if normal worker reads cannot meet cancellation needs.
4. Exercise provider-offline, sign-in-required, no-space, deleted, renamed, dehydrated-after-scan, and late-completion
   cases.

### Phase 5: Compatibility and release verification

1. Run unit and concurrency tests on MSYS x86/x64.
2. Build release x86/x64 through `scripts/build-msys.ps1`.
3. Build the Visual Studio configurations.
4. Inspect imports with `dumpbin /imports` and `objdump -p`; reject a `CldApi.dll` import.
5. Test the x86 portable build on Windows XP SP3 with ordinary local files and a `FILE_ATTRIBUTE_OFFLINE` fixture
   where the test environment supports it.
6. Test OneDrive Files On-Demand on supported Windows 10 and Windows 11 systems.
7. Test at least one non-OneDrive Cloud Files provider if available.

## Test Matrix

### Pure policy

- local file;
- fully present cloud placeholder;
- partial placeholder;
- recall-on-open;
- recall-on-data-access;
- offline legacy file;
- pinned and fully local;
- pinned but partial;
- unpinned but fully local;
- cloud reparse tag without partial state;
- unknown reparse tag;
- Cloud Files DLL and function unavailable.

### User flows

- direct file open, folder open, drag and drop, and shell activation;
- Download, Skip, Cancel, Retry, and folder-session approval;
- Previous/Next, First/Last, sequential/random slideshow, and multi-monitor slideshow;
- recursive folder search containing online-only files and directories;
- folder containing only cloud images;
- navigation away while retrieval is pending;
- application exit while retrieval is pending;
- provider hydrates or dehydrates a path after enumeration;
- a local file is replaced by an online-only file at the same path;
- a granted file is deleted or renamed before load.

### No-download assertions

Instrument the probe, dispatcher, loader, and fake file service to prove:

- scanning an online-only path reads zero content bytes;
- selecting a path without consent reads zero content bytes;
- preloading reads zero content bytes for cloud paths;
- Skip reads zero content bytes;
- loader creation for alternate target sizes reads zero content bytes;
- a session grant permits only selected-image reads, not speculative cloud reads.

## Expected Files

Likely first-party changes include:

- `src/platform/win32/CloudFileAvailability.{h,cpp}`;
- `src/platform/win32/ImgHeaderProbe.*`;
- `src/image/ImgFormatResolver.*`;
- `src/browse/FolderScanner.*`;
- `src/browse/ImgFileList.*`;
- `src/browse/ImgBrowser.*`;
- `src/image/ImgLoader.*`;
- `src/ui/win32/EmptyStateView.*` or a focused consent-panel type;
- `src/ui/win32/ImgVwWindow.*`;
- unit, platform, concurrency, and window-presentation tests;
- `ImgVw.vcxproj`, filters, and `Makefile` when adding source files.

Keep Cloud Files constants and dynamic-loading details in `src/platform/win32/`. Keep consent and preload policy out of
decoder implementations.

## Exit Criteria

This plan is complete when:

- no online-only image content is read before explicit current-session consent;
- folder enumeration and format discovery do not recall image bytes;
- local images retain current browsing and preload behavior;
- online-only images are never speculatively preloaded;
- direct open, navigation, recursive search, and every slideshow mode follow the same authorization policy;
- retrieval failures are distinguishable from decode failures;
- the consent surface is fully keyboard- and mouse-accessible;
- permission is scoped to the current browse session and is not persisted;
- x86/x64 tests and release builds pass;
- import inspection shows no static Cloud Files dependency or other post-XP import;
- ordinary local browsing passes on Windows XP SP3;
- OneDrive Files On-Demand behavior is verified on supported Windows 10 and Windows 11 systems.

## References

- Microsoft, `CfGetPlaceholderStateFromFindData`:
  https://learn.microsoft.com/windows/win32/api/cfapi/nf-cfapi-cfgetplaceholderstatefromfinddata
- Microsoft, `CfGetPlaceholderStateFromAttributeTag`:
  https://learn.microsoft.com/windows/win32/api/cfapi/nf-cfapi-cfgetplaceholderstatefromattributetag
- Microsoft, `CfHydratePlaceholder`:
  https://learn.microsoft.com/windows/win32/api/cfapi/nf-cfapi-cfhydrateplaceholder
- Microsoft, file attribute constants:
  https://learn.microsoft.com/windows/win32/fileio/file-attribute-constants
- Microsoft, reparse point tags:
  https://learn.microsoft.com/windows/win32/fileio/reparse-point-tags

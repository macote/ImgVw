# French Language Support

Date: 2026-09-07

## Status and goal

**Implemented on `feature/french-language-support` (v1.13.0).** Ship English and French in the same portable Windows executable,
preserving Windows XP compatibility, existing commands, and image-loading behavior. This plan is based on a review
of the current source, resources, build inputs, tests, and Store manifest.

## Review findings

| Area | Current implementation | Required work |
| --- | --- | --- |
| Native resources | `resources/ImgVw.rc` contains an English popup menu, About dialog, accelerators, and a minimal string table. | Add French UI resources and explicit language selection. Keep command IDs and shortcuts stable. |
| Window messages | `src/ui/win32/ImgVwWindow.cpp` embeds empty-state messages, picker errors, drag/drop guidance, and CMYK errors. | Replace visible literals with catalog lookups. |
| Welcome controls | `EmptyStateView.cpp` embeds button labels, search progress, and drag/drop guidance. | Localize controls and prose; measure translated text. |
| Statistics | `InfoOverlay.cpp` builds English labels and fixed-width columns; `OverlayText.h` formats sizes and percentages. | Localize complete messages, labels, units, and display formatting without changing statistics. |
| Pickers | `PathPicker.cpp` has separate modern and legacy image filters and legacy titles; the ICC picker is in `ImgVwWindow.cpp`. | Localize application-supplied text in every picker path. |
| Startup | `src/app/ImgVw.h` constructs an English temporary-directory failure before creating the window. | Resolve language before this failure can be shown. |
| Settings | `ImgSettings.h` currently initializes a temporary directory; it does not implement language preferences. | Add a small, separately testable preference reader/writer. |
| Layout | `EmptyStateLayout.h` uses fixed panel heights; About and overlay columns also have constrained dimensions. | Verify French expansion, wrapping, keyboard access, and DPI behavior. |
| Build | Makefile invokes windres with `--language 0x0409`; Visual Studio compiles the same RC file. | Use explicit resource languages and track included resource files as build dependencies. |
| Store | `packaging/store/Package.appxmanifest.in` declares only `en-us` and embeds English descriptions. | Add French package metadata and release materials when packaging the bilingual release. |

Adding a French menu alone would leave much of the application in English. Conversely, merely changing a thread
locale would not translate hard-coded text or reliably implement an explicit language override.

## Proposed user experience

- Default to **System**. Resolve French Windows UI languages, including Canadian and European variants, to one
  shared French translation. Resolve English and unsupported languages to English.
- Add a language submenu to the existing context menu, available both with an image and on the empty screen.
  Offer System, English, and the French language's native name. Keep the two explicit language names recognizable
  regardless of the current selection.
- Persist `system`, `en`, or `fr` as a per-user preference under an application-owned registry key using the existing
  Win32 registry conventions and RAII wrapper. Do not require administrator access or write beside the executable.
  Missing, malformed, or unsupported values mean System; a read failure must not prevent startup.
- Apply a successful selection immediately to all windows in the process, including multi-monitor slideshows.
  Preserve the current image, focus, browsing state, and slideshow timing. Future launches use the saved preference;
  other running processes pick it up on their next launch.
- If persistence fails, retain the session selection and show a localized, nonfatal explanation that it was not saved.
- Application-owned text follows the selected language. Windows-owned common-dialog buttons and shell prompts follow
  Windows; do not replace native dialogs merely to force their language.

These are proposed defaults, not existing behavior. Separate French regional catalogs, command-line language flags,
external language packs, and translation of the entire repository documentation are outside the initial scope.

## Design

### Language resolution and ownership

Introduce a small language enum, a pure resolution policy, and a Win32 adapter for OS UI language detection and
preference storage. Keep UI translation out of decoding, cache, browser state, and worker threads. Resolve the
effective language once before startup errors; avoid coupling localization initialization to temporary-directory
creation. Use immutable catalog data with process-level UI language state, changed only on the UI thread.

Use an XP-supported UI-language query for the baseline. If implementation adopts newer preferred-UI-language APIs,
load them dynamically and retain the XP fallback. Verify SDK documentation and the final import table during
implementation; do not add an unconditional Vista-or-later import or use the regional number format as a proxy for
the UI language.

### Embedded resources and deterministic lookup

Use the existing Windows resource system rather than adding a localization library or runtime file dependency.
Introduce stable `IDS_*` keys for visible text and parallel English/French string tables. Keep language resources
in small included files, such as `resources/lang/en.rc2` and `resources/lang/fr.rc2`, with explicit language directives.
Use one canonical resource language ID for each catalog; map all French OS variants to that ID in the resolver.

Provide one tested localization interface for strings, menus, and dialogs with this lookup order:
selected language, then English. Do not rely on implicit `LoadMenu`, `DialogBox`, or `LoadString` language fallback
to honor the saved override. Implement explicit resource lookup and the appropriate indirect menu/dialog loading
paths. If manually decoding string-table blocks, bound every access and respect their length-prefixed UTF-16 format.
Return owned strings where callers retain text; never leave pointers into temporary formatting buffers.

English must be complete. Missing French entries fall back to English at runtime but fail the translation coverage
check before release. Handle a missing English resource with a safe diagnostic rather than an empty or invalid UI.
Keep minimal startup error fallback text available if resource initialization itself fails.

Share icons, manifest, ICC profile, welcome image, and accelerators without duplicating binary payloads. Do not
translate resource identifiers, class names, registry values, filenames, wildcard patterns, or technical error codes.
Preserve product/version identity. Keep version-resource metadata coherent without treating it as the language switch.

Preserve Windows-1252 and CRLF for RC/RC2 files and explicit code-page declarations. Use representable French
typography; do not introduce unsupported Unicode punctuation silently. Keep new C++ UTF-8 without a BOM and use
wide strings at Win32 boundaries. Verify both resource compilers rather than assuming identical encoding behavior.

### Text, formatting, and translation

Extract whole sentences and format templates with documented parameters, allowing French to reorder values. Avoid
assembling translated prose from English fragments. Use a small validated placeholder contract, with matching
parameters across catalogs; do not expose arbitrary printf-style format strings to unchecked arguments.

Preserve English display behavior. For French, explicitly define decimal commas, percent spacing, and byte-unit
labels while preserving the existing 1024-based calculations. Use display-only formatting helpers, not a global
locale change that could affect file parsing or dependencies. Keep dimensions, paths, and hexadecimal errors intact.
Use singular/plural forms if count-bearing prose is introduced; do not add a general plural engine for label/value
statistics that do not need one.

Prepare a small glossary for navigation, slideshow, subfolders, cache, and CMYK/ICC terminology. Use consistent,
natural French suitable for both Canada and France, with a fluent French review before release. Check accents,
capitalization, punctuation, and contextual meaning, especially speed controls. Preserve function keys and Ctrl
shortcuts; choose unique translated mnemonics within each menu or button group.

### Refresh and layout

Give empty-state views and overlays an explicit language-refresh path. Retain semantic empty-state status and any
formatting parameters so a language change can rebuild its message; storing only previously translated prose would
leave stale text. Rebuild menus on their next opening and refresh native child-control captions and measurements.
Ensure the active language is also supplied when auxiliary slideshow windows are created.

Measure welcome prose and button captions with the actual font and DPI. Replace fixed heights or widths only where
needed to avoid clipping. Size the French About template appropriately. Derive overlay column widths from localized
headings and values, and handle narrow displays without losing essential information. Preserve tab order, focus,
mnemonics, contrast, and the existing drag/drop and keyboard behavior.

Build modern picker filter specifications from strings whose storage survives the dialog call. Construct legacy
filters as alternating display labels and unchanged patterns, including the final double NUL; a normal string-table
lookup must not be assumed to preserve an embedded-NUL filter. Cover the separate ICC picker too.

## Implementation sequence

1. **Inventory and English extraction.** Create the message inventory and glossary, introduce catalog IDs and the
   explicit resource-loading layer, and migrate English text without changing behavior. Keep English fallback and
   formatting tests passing. Include startup, empty-state, overlay, image/folder pickers, and ICC paths.
2. **Selection and preference.** Implement the pure resolver, XP-compatible adapter, validated persistence, submenu,
   and process-wide refresh. Test startup ordering, write failure, new windows, and multi-monitor behavior.
3. **French catalog and layout.** Translate all entries and native templates, adapt measurement where needed, review
   terminology and mnemonics, and verify both languages end to end.
4. **Build and regression gates.** Update `ImgVw.vcxproj`, its filters, Makefile, and test build inputs for added files.
   Add explicit RC include dependencies so touching either catalog rebuilds resources incrementally. Add focused
   resource integration tests; current test targets do not automatically link the application RC resources.
5. **Documentation and release packaging.** Document selection, fallback, and system-dialog behavior in README.
   Coordinate French manifest descriptions, file-association labels, resource language declarations, PRI generation,
   Store listing, and screenshots with the Store preflight plan. Do not advertise French support until UI validation
   is complete; package generation and resource indexing must be verified, not just manifest editing.

Each step should be a reviewable change. The work can proceed independently of the portable-core refactor; keep the
resolution/formatting policy separable and native resource loading in Win32 adapters so later frontends can use their
own catalogs. Newly implemented cloud-consent surfaces must enter the same catalog rather than add English literals.

## Verification and acceptance criteria

- Unit-test System resolution for English, multiple French variants, unsupported languages, detection failure,
  explicit overrides, and malformed/missing preferences. Test persistence failures without aborting startup.
- Verify equal catalog key coverage, matching placeholders, unique command mappings, French accents, and intentional
  English fallback using a deliberately missing French test resource. Exercise explicit resource selection on a host
  whose Windows language differs from the requested language.
- Test localized byte sizes and percentages at boundaries, zero counts, large values, and legacy double-NUL filters.
  Keep paths containing accents, ampersands, and non-Latin characters unchanged and rendered as literal text.
- Run the existing tests through `scripts/test-msys.ps1 -Arch x86` and `-Arch x64`; extend the established suites with
  focused language and layout tests. Link test resource fixtures where resource selection needs runtime verification.
- Build release x86 and x64 with `scripts/build-msys.ps1` and verify Visual Studio resource compilation. Use a clean
  build when introducing the resource/build configuration, then confirm editing only a translation triggers an
  incremental resource rebuild. Inspect imports for XP compatibility and run the x86 executable on XP when available.
- Manually cover English and French startup failures, welcome/empty/search states, all menu items, About, ICC errors,
  modern and legacy pickers, filename/statistics overlays, and multi-monitor slideshows. Confirm selection survives
  restart and changing it does not reload images or reset the slideshow.
- Inspect 100%, 150%, and 200% DPI, a small display, long paths, light/dark appearance, keyboard-only interaction,
  and French keyboard layouts. Require readable labels, no overlapping controls, and no clipped essential text.
- Verify portable and Store launches in both languages. Windows-owned dialog text may differ, as documented.

The feature is complete when every application-owned visible string has an English and reviewed French version,
selection and fallback are deterministic, existing navigation remains intact, and the build/runtime matrix above is
recorded. Record unavailable XP or Visual Studio validation explicitly as an outstanding release gate.

## Validation of this planning change

Source and resource review only; no application code or resource files changed. Build and runtime checks above are
implementation acceptance gates, not checks claimed to have passed for this plan.

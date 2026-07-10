# Microsoft Store Publication Plan

## Objective

Publish ImgVw in the Microsoft Store as a free, packaged Win32 desktop application with Store-managed installation,
updates, signing, and reputation. Preserve the existing portable GitHub releases and the documented Windows XP
compatibility target; Store packaging is a separate distribution layer for supported Windows 10 and Windows 11
devices.

This plan assumes the Microsoft Partner Center developer account is enrolled and able to create app products.

## Recommended Distribution Model

Use an MSIX package built directly from the release binaries, with an MSIX bundle containing the x86 and x64 packages.
Submit the bundle to Partner Center and let the Microsoft Store sign and distribute it.

Do not submit the current `scripts/install-imgvw.ps1` flow as the Store installer:

- Partner Center's unpackaged Win32 path accepts only an MSI or EXE installer.
- The installer must work silently and must be a standalone/offline installer, while the current script downloads an
  application release from GitHub during installation.
- The MSI/EXE route requires the installer and every installed PE file to have a trusted code-signing signature.
- A source-built MSIX is a natural fit for ImgVw's small, self-contained payload and gives users Store-managed clean
  install, uninstall, updates, signing, and SmartScreen reputation.

Keep the current PowerShell installer for legacy systems and direct GitHub distribution. Do not make MSIX APIs or
package identity a requirement in the application executable unless they are guarded so the unpackaged Windows XP path
continues to work.

Official references:

- [Distribute a Win32 app through the Microsoft Store](https://learn.microsoft.com/windows/apps/distribute-through-store/how-to-distribute-your-win32-app-through-microsoft-store)
- [MSI/EXE package requirements](https://learn.microsoft.com/windows/apps/publish/publish-your-app/msi/app-package-requirements)
- [MSIX documentation](https://learn.microsoft.com/windows/msix/)
- [Package a desktop app with Visual Studio](https://learn.microsoft.com/windows/msix/desktop/vs-package-overview)

## Current-State Assessment

ImgVw already has several favorable Store characteristics:

- It is a conventional Win32 GUI application with no driver, Windows service, background agent, or elevation request.
- Release builds exist for x86 and x64.
- Release binaries statically include their image and color-management libraries, so there are no separate runtime
  installers.
- The application already carries product and file version resources.
- Image files and folders can be supplied on the command line.
- Third-party notices and the LGPL relinking path are documented in `LICENSE.md` and `README.md`.

The Store work still requires:

- recording the reserved product's package identity values from Partner Center;
- replacing the initial package artwork with final Store-quality artwork;
- declaring the executable, visual identity, and supported image file associations in the package manifest;
- validating application behavior under package identity and MSIX filesystem/registry rules;
- preparing listing, support, privacy, licensing, and certification material;
- integrating Store package generation into the release checklist.

## Phase 1: Establish the Partner Center Product

1. In Partner Center, create a new app and reserve `ImgVw` if it is available. If it is unavailable, choose the final
   public Store name before creating package assets or listing copy.
2. Record the exact, case-sensitive values shown under **Product management > Product identity**:
   - Package/Identity/Name;
   - Package/Identity/Publisher;
   - Package/Properties/PublisherDisplayName;
   - reserved Store display name;
   - product ID and Store listing URL once assigned.
3. Treat those identity values as release configuration, not placeholders to invent locally. Store package identity is
   persistent and must remain stable across updates.
4. Select the initial commercial settings:
   - pricing: free;
   - discoverability: visible in the Store, unless a private certification flight is desired first;
   - markets: start with all markets where the English-only application and support model are appropriate;
   - category: choose the closest current photo/image-viewing category offered by Partner Center;
   - age rating: complete the IARC questionnaire accurately; the expected result should be suitable for all ages.
5. Use a publishing hold for the first submission so certification can finish without making the listing public before
   the final install and listing checks are complete.

Deliverable: a short release-owned identity record containing the Partner Center values and product decisions. Do not
commit account secrets, API credentials, certificate private keys, or Partner Center authentication tokens.

## Phase 2: Add Reproducible MSIX Packaging

Add a Windows Application Packaging Project, or an equivalent scripted `MakeAppx.exe` pipeline, without changing the
existing `ImgVw.vcxproj` application target. Prefer a packaging project in the solution initially because it can
associate with the existing Win32 project, obtain Store identity through Visual Studio, generate architecture packages,
and produce the Partner Center upload artifact.

The repository should gain an obvious first-party packaging area, for example:

```text
packaging/store/
    Package.appxmanifest
    Assets/
    README.md
```

Keep the Visual Studio project, solution, and any command-line packaging scripts in sync. Do not put generated `.msix`,
`.msixbundle`, `.msixupload`, or package staging output in source control.

### Package contents

Each architecture package should contain only files required at runtime or required for legal notices:

- the matching release build of `ImgVw.exe`;
- `LICENSE.md` with ImgVw and third-party notices;
- any separately required LGPL notices or relinking instructions if the installed application needs an offline path to
  them;
- the MSIX manifest and package visual assets.

Do not include the PowerShell installer, install marker, PATH modification, uninstall registration, or registry-based
association setup. MSIX and the Store own installation, updates, removal, and declared associations.

### Manifest design

Use Partner Center's exact identity values and declare:

- `TargetDeviceFamily` as `Windows.Desktop` with the lowest supported Windows 10 version that passes testing;
- one full-trust desktop application entry point for `ImgVw.exe`;
- display name `ImgVw`, the reserved publisher display name, description, and Store assets;
- x86 and x64 processor architecture in their respective packages;
- English (United States) as the initial resource language;
- only capabilities actually required by the application;
- file type associations for `.jpg`, `.jpeg`, `.png`, `.heic`, `.heif`, `.hif`, `.bmp`, `.gif`, `.ico`, `.tif`, and
  `.tiff`.

Keep the extension list synchronized with `ImgItemHelper::GetImgFormatFromExtension()` and
`scripts/install-imgvw.ps1`. Give the association a user-facing description such as `ImgVw image` and use the
appropriate manifest logo. Do not claim default ownership of an extension: Windows lets the user select the default
application.

Treat the existing folder context-menu integration as a separate investigation. Do not reproduce it with undocumented
or package-unsafe registry writes. Ship the first Store version without that verb if no supported MSIX manifest
extension provides equivalent behavior. Direct folder launch and drag/drop should remain available.

### Versions and architecture

Use one version source for the Win32 resources, embedded Win32 manifest, MSIX identity, About dialog, Git tag, and Store
release notes. Add a release check that fails when these versions disagree.

Use four-part MSIX versions and keep them monotonically increasing. Confirm Partner Center's current version rules when
the first package is created. Produce an x86 package and an x64 package with otherwise equivalent manifests, then bundle
them so Windows downloads only the applicable architecture. Do not add ARM64 until ImgVw and every static dependency
have a verified ARM64 build.

The MSIX minimum OS version limits only the Store package. The underlying executable must retain `WINVER=0x0501` and
`_WIN32_WINNT=0x0501` so the portable build remains compatible with Windows XP.

## Phase 3: Create Store Visual and Listing Assets

Create a Store-quality master icon rather than scaling the current small icon blindly. Preserve the recognizable ImgVw
identity, confirm that the artwork is owned or appropriately licensed, and export the package logo/tile scales required
by the selected manifest. At minimum, verify 16, 24, 32, 48, and 256 pixel Win32 icon rendering and provide the required
Store logo scale variants.

Prepare the initial English listing:

- product name matching the installed display name;
- a concise short description;
- a plain-language full description;
- feature list covering fast browsing, slideshow modes, multi-monitor viewing, recursive folder browsing, EXIF
  orientation, color-managed JPEG/PNG/HEIF display, and supported formats;
- at least four clean screenshots, even though Partner Center currently requires only one;
- square Store box art and any other currently required logo formats;
- copyright and publisher text;
- version-specific release notes for updates;
- accurate minimum system requirements: supported Windows 10/11 Desktop, mouse and/or keyboard, and matching x86/x64
  processor.

Do not promise editing capabilities, every possible HEIF codec/profile, automatic default-app changes, Windows XP
support through the Store, or behavior not covered by the submitted build.

See [Microsoft's current icon guidance](https://learn.microsoft.com/windows/apps/design/iconography/app-icon-construction)
and the [Store listing fields for desktop apps](https://learn.microsoft.com/windows/apps/publish/publish-your-app/msi/add-and-edit-store-listing-info).

## Phase 4: Support, Privacy, and Licensing Readiness

1. Publish a stable HTTPS support page, preferably in the GitHub repository or its project site, with:
   - supported Windows versions and image formats;
   - basic usage and default-app instructions;
   - known limitations;
   - issue-reporting and security-contact paths;
   - Store installation/update troubleshooting.
2. Decide the Partner Center data-access declaration from actual application behavior. ImgVw opens user-selected image
   files and enumerates their folders locally but does not currently advertise telemetry, accounts, cloud services, or
   data transmission.
3. Publish a short HTTPS privacy statement even if Partner Center does not require one for the selected answers. State
   precisely that image contents and paths stay on the device unless a future feature changes that behavior. Avoid an
   absolute `collects no data` claim until network calls, crash reporting, and dependencies have been audited.
4. Use `LICENSE.md` as the basis for the Store license terms, while recognizing that Store listing terms and bundled
   third-party notices serve different purposes.
5. Verify LGPL compliance for the statically linked `libheif` and `libde265` build in the Store channel. Keep the exact
   corresponding source, build scripts, object/relink materials, and written relinking offer or download path available
   for each distributed Store version for the required period. Make those materials accessible without requiring the
   installed PowerShell script.
6. Confirm that the embedded CGATS21 profile and every Store image/icon have distributable license records.

Have the privacy and LGPL conclusions reviewed by the publisher; this plan is an engineering checklist, not legal
advice.

## Phase 5: Package Compatibility Work

Install a locally signed development package on clean Windows 10 and Windows 11 virtual machines and test the actual
package, not only the unpackaged executable.

Investigate and correct any package-specific behavior in guarded code paths:

- launch from Start and from the Store installation result;
- open each declared file type through **Open with** and as the user-selected default;
- launch with a single image, multiple images, a directory, spaces, non-ASCII paths, long paths, UNC paths, removable
  media, and read-only media where supported by Windows;
- enumerate adjacent images after activation from a file association;
- delete and recycle images, including error handling when access is denied;
- read and write settings and the selected custom ICC profile under the effective packaged application-data location;
- display the built-in ICC fallback when no custom profile is available;
- browse recursively and use slideshow/multi-monitor modes;
- retain settings across Store updates and remove package-owned state on uninstall as Windows specifies;
- coexist with a portable or PowerShell-installed copy without corrupting associations or settings;
- run without administrator privileges and without writing beside the installed executable;
- work offline after installation.

Pay particular attention to the current use of `GetCurrentDirectory`, shell recycle operations, file deletion, and
application-data paths. Do not work around MSIX isolation by requesting broad capabilities unless a failing scenario
demonstrates a real need and the capability is supported for Store desktop apps.

## Phase 6: Validation and Certification Dry Run

For both x86 and x64 release payloads:

1. Run the normal unit tests:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Clean
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x64 -Clean
   ```

2. Run clean release builds:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
   ```

3. Build the MSIX packages and x86/x64 bundle from immutable release outputs.
4. Inspect the bundle contents and manifest; verify that no private keys, symbols, temporary files, build paths, or
   unrelated executables are present.
5. Install, update, downgrade-test, uninstall, and reinstall the locally signed package on clean VMs.
6. Run the currently available Windows App Certification Kit as a preflight check. Microsoft documents local WACK as
   deprecated but still useful; Partner Center certification remains authoritative.
7. Scan the final unpacked payload and package with Microsoft Defender and, optionally, a multi-engine service that does
   not redistribute private prerelease binaries.
8. Verify package architecture selection on 32-bit Windows where available and on 64-bit Windows.
9. Record test OS builds, package hash, package version, Git commit, compiler/toolchain, and results in the release
   evidence.

## Phase 7: Partner Center Submission

Complete every Partner Center section using the final package and reviewed metadata:

- pricing and availability, markets, discoverability, and publishing hold;
- category and product properties;
- truthful data-access answer and privacy URL if required;
- support contact and website;
- product declarations and accessibility status;
- system requirements;
- IARC age-rating questionnaire;
- x86/x64 MSIX bundle upload;
- English Store listing, screenshots, logos, description, features, and license terms;
- certification notes.

Certification notes should make testing easy. Include a concise description of ImgVw, state that it is an offline
Win32 image viewer, list supported formats, explain keyboard/mouse navigation, identify file-association scenarios, and
call out any behavior that may look destructive, especially Delete versus Shift+Delete. State that no account or test
credentials are required.

Submit under a publishing hold. When certification passes:

1. install the certified build from the Store using a non-publisher test account and clean machine;
2. verify listing presentation, architecture selection, launch, associations, updates, uninstall, publisher display,
   and absence of SmartScreen warnings;
3. release the publishing hold;
4. verify the public listing URL and acquisition from a second device/account;
5. add the Store badge/link to `README.md` and the project website only after the listing is public.

Microsoft's current submission checklist is documented at
[Create an app submission](https://learn.microsoft.com/windows/apps/publish/publish-your-app/msi/create-app-submission).
Recheck the live Partner Center fields at submission time because required metadata and policies can change.

## Phase 8: Updates and Ongoing Operations

Extend the release process so Store output comes from the same tagged commit as GitHub output but remains a distinct
artifact. A Store release should:

1. update all shared version sources;
2. build and test x86 and x64 application binaries;
3. create the GitHub portable assets and the Store MSIX bundle from those binaries;
4. archive hashes and build evidence;
5. publish the GitHub release as appropriate for legacy users;
6. submit the monotonically versioned Store bundle with release notes;
7. monitor certification and crash/quality reports;
8. test acquisition and update after Store rollout.

Start with manual Partner Center submission. Automate only after at least two successful releases have stabilized the
manifest, asset, version, and certification workflow. Keep API credentials outside the repository and use the official
Store submission API or supported CI integration when automation is justified.

Never replace a submitted artifact in place. Retain the exact package, hashes, source commit, dependency versions,
license/relink materials, screenshots, and listing text associated with each Store version.

## Rollback and Channel Coexistence

- Do not withdraw the portable or PowerShell distribution when the Store listing launches; it remains the path for
  Windows XP and users who prefer portable software.
- Document that Store and portable installations are separate channels and may have different update timing.
- Prefer preventing downgrade installation through package versioning. If a bad release passes certification, stop its
  rollout or submit a higher-version corrective package rather than attempting to reuse an old package version.
- Preserve user-owned image files under all install, update, uninstall, and rollback scenarios.
- Before changing package identity, publisher identity, or distribution model after publication, investigate migration
  consequences. Those identifiers are part of the installed app's continuity.

## Initial Implementation Sequence

1. [Complete] Create the MSIX product and record its Partner Center identity in `packaging/store/README.md`.
2. [Complete] Set Windows 10 version 1809 (build 17763) as the initial Store minimum.
3. [Complete] Add a scripted packaging pipeline, manifest, and initial package assets under `packaging/store/`.
4. [Ready] Produce unsigned x86 and x64 packages and an MSIX bundle for Partner Center upload.
5. Test package identity, data paths, file activation, folder enumeration, recycle/delete, and coexistence.
6. Fix only demonstrated package compatibility issues, keeping changes guarded for unpackaged Windows XP builds.
7. [In progress] Listing copy and a 300×300 Store tile are prepared; capture screenshots, publish the support/privacy
   pages, and prepare certification notes.
8. Complete licensing and LGPL release-material checks.
9. Run the full build, test, package, VM, and certification preflight matrix.
10. Submit under a publishing hold, validate the certified Store install, then publish.
11. Add the public Store link and incorporate Store packaging into the release checklist.

## Acceptance Criteria

- Partner Center accepts an x86/x64 MSIX bundle built reproducibly from a tagged release commit.
- The package identity exactly matches the reserved Partner Center product identity.
- Store installation, update, launch, and uninstall require no custom installer or elevation.
- The installed app displays the expected publisher and launches without a SmartScreen reputation prompt.
- All declared image types can activate ImgVw, and ImgVw can browse adjacent images from those locations where Windows
  grants normal desktop access.
- Settings, ICC profile behavior, recycle/delete, slideshows, recursive browsing, and multi-monitor behavior pass the
  packaged-app test matrix.
- x86 and x64 devices receive the correct payload.
- The Store package works offline after installation and contains no downloader or unrelated executable.
- Store listing, privacy statement, support material, license terms, and certification notes accurately describe the
  submitted build.
- Third-party notices and the LGPL relinking path are available for the exact Store release.
- The existing GitHub/PowerShell distribution and Windows XP-compatible build remain functional.
- The certified build is acquired successfully from the public Store listing after release.

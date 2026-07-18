# Microsoft Store package

This directory contains the source files for the x86/x64 MSIX bundle submitted to Partner Center. Generated packages
are written to `out/` and are ignored by Git.

## Build an upload bundle

Build both release executables first, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-store-package.ps1
```

The script contains the public identity assigned under **Partner Center > Product management > Product identity**:

- Package/Identity/Name: `Marc-AndrCt.ImgVw`
- Package/Identity/Publisher: `CN=B2B17E87-E414-4595-A511-7C4778B76C22`
- Package/Properties/PublisherDisplayName: `Marc-André Côté`

These values identify the Store product and must remain stable across updates. They are not credentials or signing
secrets.

The script reads the version from `resources/ImgVw.rc`, converts it to the required four-part MSIX version, verifies
the version embedded in both executables, stages only `ImgVw.exe` and `LICENSE.md`, validates each manifest with
`MakeAppx.exe`, and produces:

- `out/ImgVw_<version>_x86.msix`
- `out/ImgVw_<version>_x64.msix`
- `out/ImgVw_<version>.msixbundle`
- `out/SHA256SUMS.txt`

Upload the `.msixbundle` in the **Packages** section of the Partner Center submission. The Store applies its production
signature after certification. The unsigned bundle is expected not to install directly; local installation testing
requires signing it with a certificate whose subject matches `Publisher`.

The Store package supports Windows 10 version 1809 (build 17763) and later. This packaging-only minimum does not alter
the portable executable's Windows XP compatibility target.

## Submission metadata

Use `docs/store_listing_en-us.md` as the canonical English Partner Center listing and update text. Review its
description, product features, What's new section, and screenshot captions for every Store release. Keep the What's new
text consistent with the corresponding release section in `CHANGELOG.md` before uploading the bundle.

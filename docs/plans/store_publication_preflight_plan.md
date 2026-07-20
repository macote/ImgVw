# Microsoft Store Publication Preflight Plan

## Status

The Store product identity, Windows 10 1809 minimum, packaging script, manifest, x86/x64 packages, bundle, hashes,
listing draft, and initial visual assets are present. Publication, Partner Center validation, release-specific LGPL
materials, screenshots, public support/privacy pages, and certification remain external or unverified.

This plan replaces the remaining delivery work in `microsoft_store_publication_plan.md`; that document is retained as
the long-form packaging and policy record.

## Release-Ready Inputs

1. Update `docs/store_listing_en-us.md`, release notes, and screenshots from the candidate build.
2. Publish stable support and privacy URLs, then add them to the Partner Center listing.
3. Assemble the exact source archive, dependency versions, notices, licenses, relinking instructions, package hashes,
   and source commit for the candidate.
4. Build release x86 and x64 executables and run `scripts/build-store-package.ps1`; retain the generated bundle and
   `SHA256SUMS.txt` without modification.

## Compatibility Preflight

1. Test a locally signed package on supported Windows 10 and Windows 11 machines.
2. Exercise no-argument launch, file activation, folder activation, Open commands, drag and drop, empty-folder
   recovery, recursive search, delete/recycle behaviour, settings, ICC fallback, and single/multi-monitor slideshow.
3. Verify the installed package does not rely on an installer, downloader, undeclared capability, or write location
   outside its allowed model.
4. Confirm the portable release and PowerShell installer still work independently, including the Windows XP build.

## Submission and Follow-Up

1. Upload the bundle under the reserved identity and complete every Partner Center declaration from verified evidence.
2. Submit on a publishing hold, inspect certification results, then acquire and smoke-test the certified Store build.
3. Publish only after the acquired build matches the tested candidate behaviour.
4. Record the Store URL, package version, hashes, source commit, certification outcome, and release-specific materials
   in the release checklist.

## Exit Criteria

This plan is complete only after a public Store acquisition is verified. A locally generated unsigned bundle or a
successful `MakeAppx` validation is not evidence of Store publication.

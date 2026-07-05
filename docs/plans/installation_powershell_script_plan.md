# Installation PowerShell Script Plan

## Goal

Make ImgVw available through a repository-provided PowerShell installer that can:

- install or update the application executable and required redistributable files;
- register ImgVw as an Open With candidate for supported image extensions;
- register ImgVw for folders so Explorer can open a folder in ImgVw;
- optionally set legacy default file associations where Windows allows it;
- uninstall the files and registry entries created by the script;
- preserve the current Windows XP compatibility target.

The first implementation should be a script under `scripts/`, packaged with releases. It should not require an MSI,
installer framework, or background service.

## Scope

Add:

- `scripts/install-imgvw.ps1`
- documentation in `README.md` or release notes that shows install, update, and uninstall commands
- release packaging changes so the script ships beside `ImgVw.exe`

Do not change image decoding or runtime UI behavior for this work. The installed app remains the same executable that
also supports copy-and-run usage.

## Supported Extensions

Use the current source as the initial registration list:

- `.jpg`
- `.jpeg`
- `.png`
- `.heic`
- `.heif`
- `.hif`
- `.bmp`
- `.gif`
- `.ico`
- `.tif`
- `.tiff`

Keep this list in one obvious place inside the installer script and add a comment pointing to
`ImgItemHelper::GetImgFormatFromExtension()`. When supported formats change, update the installer list in the same
change as the decoder or extension mapping.

## Installer Interface

Recommended parameters:

```powershell
param(
    [ValidateSet("Install", "Uninstall")]
    [string]$Action = "Install",

    [ValidateSet("CurrentUser", "AllUsers")]
    [string]$Scope = "CurrentUser",

    [string]$InstallDir = "",
    [string]$SourceExe = "",
    [ValidateSet("Auto", "x86", "x64")]
    [string]$Arch = "Auto",
    [string]$Version = "latest",

    [switch]$SkipImageRegistration,
    [switch]$SkipFolderRegistration,
    [switch]$SetLegacyDefaults,
    [switch]$NoPath,
    [switch]$Force
)
```

Default behavior:

- install per-user to `%LOCALAPPDATA%\Programs\ImgVw`;
- check the latest GitHub release and download the matching release asset when `-SourceExe` is not provided;
- normalize the installed executable name to `ImgVw.exe`, even if the release asset or source executable has an
  architecture-qualified name;
- register image and folder shell integration under `HKCU\Software\Classes`;
- allow either registration area to be skipped with `-SkipImageRegistration` or `-SkipFolderRegistration`;
- do not take over default file associations unless `-SetLegacyDefaults` is supplied;
- add the install directory to the current user's `PATH` unless `-NoPath` is supplied.

For `-Scope AllUsers`, use `%ProgramFiles%\ImgVw` and `HKLM\Software\Classes`, and fail early with a clear message if
the script is not elevated.

Per-user installation is the default and preferred path. All-users installation is an explicit optional mode for managed
or shared machines, not the normal release instruction.

## README Install Instructions

The README should show a direct per-user install command that downloads the installer script and executes it with
`powershell -NoProfile`. The script itself must check the latest release and download the correct application asset:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing https://github.com/macote/ImgVw/releases/latest/download/install-imgvw.ps1 -OutFile $env:TEMP\install-imgvw.ps1; & $env:TEMP\install-imgvw.ps1"
```

That one-liner should be the primary install path. If release archives remain available, document this archive-local
command as the offline or manually downloaded fallback:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-imgvw.ps1
```

Also document:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-imgvw.ps1 -Version latest -Arch x64
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-imgvw.ps1 -Action Uninstall
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-imgvw.ps1 -Scope AllUsers
```

The README should state that the default install is per-user, adds ImgVw to the user's `PATH`, and registers Explorer
Open With entries without taking over default image apps.

## Files To Install

The release package should contain:

```text
ImgVw.exe
install-imgvw.ps1
LICENSE.md
README.md
```

If future builds require external DLLs or data files, add a manifest-like array in the script instead of copying every
file from the source directory. Do not copy build outputs, object files, or dependency source trees.

The installer should:

1. Create the target directory if needed.
2. Verify the source executable exists.
3. Select the correct executable architecture.
4. Copy the executable to the target path as `ImgVw.exe`.
5. Stop before overwriting a running executable unless `-Force` is used and the replacement can be performed safely.
6. Copy the expected documentation files.
7. Register shell integration.
8. Write a small install marker under the install directory with version, architecture, source path, installed files, and registry
   roots used.

## Architecture Selection

Release packaging should make bitness explicit. Prefer publishing separate assets:

```text
ImgVw-win32.zip
ImgVw-x64.zip
install-imgvw.ps1
```

The installer should support:

- `-Arch Auto`: choose `x64` on a 64-bit OS, otherwise choose `x86`;
- `-Arch x86`: install the Win32 build, including on 64-bit Windows when the user asks for it;
- `-Arch x64`: require a 64-bit OS and fail clearly on 32-bit Windows.

When run from an extracted release archive, the script should prefer the executable in the same directory and validate
that its machine type matches the requested architecture. When run from the README download command, it should download
the correct release asset for the selected architecture.

The installed file name should always be:

```text
%LOCALAPPDATA%\Programs\ImgVw\ImgVw.exe
```

or, for all-users installation:

```text
%ProgramFiles%\ImgVw\ImgVw.exe
```

Do not install architecture-qualified executable names into the final directory. Registry commands and `PATH` should
always refer to the normalized `ImgVw.exe` path.

## Latest Release Resolution

When `-SourceExe` is not supplied, the installer must query GitHub releases and use the selected release asset. Default
selection is `-Version latest`.

Use the GitHub releases API:

```text
https://api.github.com/repos/macote/ImgVw/releases/latest
```

For a pinned version, query the tag endpoint:

```text
https://api.github.com/repos/macote/ImgVw/releases/tags/<tag>
```

The script should:

1. Use `Invoke-WebRequest -UseBasicParsing` so it works on Windows PowerShell without Internet Explorer initialization.
2. Parse the JSON response with `ConvertFrom-Json` when available.
3. Fall back to a small, explicit parser only if older PowerShell compatibility requires it.
4. Select the asset that matches `-Arch`.
5. Download the asset to a temporary directory.
6. Verify the downloaded asset is present, non-empty, and has the expected extension.
7. Extract the archive if the asset is a `.zip`, then locate the executable inside it.
8. Validate the executable machine type before installing.
9. Record the release tag, asset name, browser download URL, and architecture in the install marker.

If GitHub is unreachable, no matching asset exists, or the downloaded archive is invalid, fail clearly and leave the
previous installation in place. The script should not silently install an older local binary unless `-SourceExe` was
explicitly provided.

The release asset naming convention should be stable enough for deterministic selection, for example:

```text
ImgVw-win32.zip
ImgVw-x64.zip
install-imgvw.ps1
```

The script should reject ambiguous matches instead of guessing.

## PATH Registration

By default, add the install directory to the user's `PATH` for `CurrentUser` installs. For `AllUsers`, add it to the
machine `PATH` only when elevated.

The script should:

- avoid duplicate `PATH` entries;
- preserve existing entry order;
- update the appropriate `Environment` registry key;
- broadcast `WM_SETTINGCHANGE` for `Environment` when possible;
- leave the running PowerShell session unchanged except for printing the installed command path.

After installation, `ImgVw.exe` should be invokable from a new terminal.

## Registry Model

Prefer per-user registration because it does not require elevation and avoids machine-wide side effects.

Use stable identifiers:

- application name: `ImgVw`
- executable name: `ImgVw.exe`
- image ProgID: `ImgVw.Image`
- folder shell verb key: `ImgVw.OpenFolder`

Every command value must quote the executable and argument path:

```text
"C:\Path\To\ImgVw.exe" "%1"
```

Do not use unquoted paths or `%*` for Explorer file/folder verbs.

## Image Open With Registration

For all supported extensions, register ImgVw as an Open With candidate without changing the user's current default app:

```text
HKCU\Software\Classes\Applications\ImgVw.exe
HKCU\Software\Classes\Applications\ImgVw.exe\shell\open\command
HKCU\Software\Classes\Applications\ImgVw.exe\SupportedTypes
HKCU\Software\Classes\<extension>\OpenWithProgids
HKCU\Software\Classes\<extension>\OpenWithList\ImgVw.exe
HKCU\Software\Classes\ImgVw.Image
HKCU\Software\Classes\ImgVw.Image\DefaultIcon
HKCU\Software\Classes\ImgVw.Image\shell\open\command
```

On Windows Vista and later, also register application capabilities so ImgVw appears in Default Programs / Default Apps:

```text
HKCU\Software\ImgVw\Capabilities
HKCU\Software\ImgVw\Capabilities\FileAssociations
HKCU\Software\RegisteredApplications
```

For `-Scope AllUsers`, write the equivalent `HKLM` keys.

Use `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0)` when available to refresh Explorer association state. From
PowerShell, prefer a tiny embedded C# `Add-Type` helper on modern Windows. If `Add-Type` is unavailable or fails on the
target machine, fall back to telling the user to restart Explorer or sign out.

## Default Association Policy

Modern Windows versions intentionally restrict programmatic default-app changes. The installer should not claim that it
can set defaults everywhere.

Implement `-SetLegacyDefaults` only for systems where direct registry defaults are honored, especially Windows XP and
older association behavior:

```text
HKCU\Software\Classes\<extension>\(Default) = ImgVw.Image
```

For Windows 8 and later, register capabilities and Open With entries, then print a concise message telling the user to
choose ImgVw from Windows Settings or Explorer's Open With UI. Do not attempt to generate undocumented UserChoice hashes.

## Folder Registration

Register an Explorer folder verb that opens the selected folder in ImgVw:

```text
HKCU\Software\Classes\Directory\shell\ImgVw.OpenFolder
HKCU\Software\Classes\Directory\shell\ImgVw.OpenFolder\command
HKCU\Software\Classes\Drive\shell\ImgVw.OpenFolder
HKCU\Software\Classes\Drive\shell\ImgVw.OpenFolder\command
```

Display text:

```text
Open with ImgVw
```

Command:

```text
"C:\Path\To\ImgVw.exe" "%1"
```

Register both `Directory` and `Drive` so normal folders and drive roots work. Keep this as a direct shell verb instead
of trying to force ImgVw into Windows' file-oriented Open With picker for folders.

## Uninstall Behavior

`-Action Uninstall` should:

1. Remove only registry values and keys owned by ImgVw.
2. Remove ImgVw from each supported extension's `OpenWithProgids` and `OpenWithList`.
3. Remove the folder shell verb keys.
4. Remove `RegisteredApplications` and capabilities entries that point to ImgVw.
5. Remove the install directory only if it matches the default path or contains an install marker created by the script.
6. Remove the user `PATH` entry if the script added it.
7. Refresh shell associations.

Never delete a user-supplied `-InstallDir` unless the install marker proves it was created by this installer and contains
only files the installer owns.

## PowerShell Compatibility

The repository already documents `powershell -NoProfile`; keep every example using it.

Target Windows PowerShell 2.0-compatible script syntax where practical for Windows XP-era systems:

- avoid PowerShell classes;
- avoid advanced features that require PowerShell 5+;
- use .NET APIs available on .NET Framework versions commonly present on older Windows;
- use `New-Item`, `Set-ItemProperty`, and `Remove-ItemProperty` with explicit error handling;
- avoid registry cmdlet assumptions that behave differently between 32-bit and 64-bit hosts unless tested.

If a necessary feature cannot work on Windows XP, isolate it behind an OS-version check and keep the core install and
legacy association path working.

## Implementation Steps

1. Add `scripts/install-imgvw.ps1` with parameter parsing, OS/scope detection, path resolution, and elevation checks.
2. Add helper functions for registry root selection, safe key creation, safe value removal, command quoting, and shell
   refresh.
3. Implement file copy and install marker writing.
4. Implement image Open With registration for the supported extension list.
5. Implement Vista+ capabilities registration and Windows XP legacy default support.
6. Implement folder and drive shell verb registration.
7. Implement uninstall with ownership checks.
8. Update release packaging so the script is included with release archives.
9. Update README or release notes with per-user install, all-users install, folder integration, default-app selection,
   and uninstall examples.
10. Add manual verification notes to the release checklist.

## Verification

Run these on a development machine:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install-imgvw.ps1 -Action Install
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install-imgvw.ps1 -Action Uninstall
```

Also verify:

- Explorer shows ImgVw in Open With for each supported image extension.
- A selected image opens with the correct quoted path when launched through Open With.
- A selected folder shows `Open with ImgVw` and opens that folder in ImgVw.
- A drive root shows `Open with ImgVw` and opens that root in ImgVw.
- Existing default image apps are not changed by the default install path.
- `-SetLegacyDefaults` works on a Windows XP or legacy-association VM.
- Windows 8 or later does not receive invalid `UserChoice` registry writes.
- `-Scope AllUsers` fails clearly without elevation and succeeds when elevated.
- Uninstall removes ImgVw entries without removing unrelated Open With apps.

## Acceptance Criteria

The work is complete when:

- a release user can install ImgVw with one documented PowerShell command;
- ImgVw appears as an Open With option for all currently supported image extensions;
- folders and drive roots have an `Open with ImgVw` Explorer verb;
- per-user install and uninstall do not require administrator rights;
- all-users install is available with elevation;
- the script is idempotent across repeated install/update/uninstall runs;
- uninstall removes only installer-owned files and registry entries;
- Windows XP-compatible association behavior is preserved or any limitation is explicitly documented;
- release archives include the installer script and documentation.

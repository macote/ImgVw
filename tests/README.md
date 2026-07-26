# ImgVw Tests

The default test command builds and runs the complete suite:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86
```

Use `-Suite` while iterating on one subsystem:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Suite core
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Suite platform
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Suite image
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Suite concurrency
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Suite ui
```

The direct Make targets are `test-core`, `test-platform`, `test-image`, `test-concurrency`, and `test-ui`.

Visual Studio uses the matching `TestSuite` project property. For example:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
    ImgVw.Tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:TestSuite=Ui /m
```

Valid Visual Studio values are `All`, `Core`, `Platform`, `Image`, `Concurrency`, and `Ui`. Each focused suite has a
separate intermediate directory and executable name, so switching suites does not replace the complete test binary.

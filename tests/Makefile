CXX ?= g++
DUMPMACHINE = $(shell $(CXX) -dumpmachine)
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DUNICODE -D_UNICODE -DIMGVW_TESTING \
	-DLIBHEIF_STATIC_BUILD -DLIBDE265_STATIC_BUILD \
	-I../src/browse -I../src/image -I../src/platform/win32 -I../src/ui/win32 -I../resources \
	-isystem ../3rd-party/libjpeg-turbo -isystem ../3rd-party/Little-CMS -isystem ../3rd-party/libheif \
	-isystem ../3rd-party/libde265
TARGETDIR = bin/$(DUMPMACHINE)
TARGET = $(TARGETDIR)/ImgVwTests.exe
CORETARGET = $(TARGETDIR)/ImgVwCoreTests.exe
PLATFORMTARGET = $(TARGETDIR)/ImgVwPlatformTests.exe
IMAGETARGET = $(TARGETDIR)/ImgVwImageTests.exe
CONCURRENCYTARGET = $(TARGETDIR)/ImgVwConcurrencyTests.exe
UITARGET = $(TARGETDIR)/ImgVwUiTests.exe
HEADERS = $(wildcard *.h support/*.h unit/*.h ../src/app/*.h ../src/browse/*.h ../src/image/*.h \
	../src/platform/win32/*.h ../src/ui/win32/*.h)
TESTMAIN = support/TestHarness.cpp support/TestMain.cpp
SOURCES = ImgVwTests.cpp concurrency/ConcurrencyTests.cpp image/ImageTests.cpp platform/PlatformTests.cpp \
	ui/RendererTests.cpp \
	unit/ImagePolicyTests.cpp unit/ImgFileListTests.cpp \
	support/JpegFixture.cpp support/TempFile.cpp support/TestHarness.cpp support/TestMain.cpp \
	../src/browse/BrowsePath.cpp ../src/browse/ImgBrowser.cpp ../src/browse/ImgFileList.cpp \
	../src/image/ColorProfile.cpp \
	../src/image/ColorTransform.cpp ../src/image/ExifOrientation.cpp ../src/image/ImgResampler.cpp \
	../src/image/ImageDispatcher.cpp ../src/image/ImgLoader.cpp ../src/image/ImageFormatDetector.cpp \
	../src/image/ImageFormatResolver.cpp ../src/image/ImgBuffer.cpp ../src/image/ImgGDIItem.cpp \
	../src/image/ImgHEIFItem.cpp ../src/image/ImgItem.cpp ../src/image/ImgItemHelper.cpp \
	../src/image/ImgJPEGDecoder.cpp ../src/image/ImgJPEGItem.cpp ../src/platform/win32/FileOperations.cpp \
	../src/platform/win32/ImageHeaderProbe.cpp \
	../src/ui/win32/ImgRenderer.cpp
CORESOURCES = unit/CoreTests.cpp unit/ImagePolicyTests.cpp unit/ImgFileListTests.cpp support/TempFile.cpp $(TESTMAIN) \
	../src/browse/ImgFileList.cpp ../src/image/ExifOrientation.cpp ../src/image/ImgResampler.cpp \
	../src/image/ImageFormatDetector.cpp ../src/image/ImageFormatResolver.cpp ../src/image/ImgBuffer.cpp \
	../src/image/ImgItemHelper.cpp ../src/platform/win32/ImageHeaderProbe.cpp
PLATFORMSOURCES = platform/PlatformTests.cpp support/TempFile.cpp $(TESTMAIN) \
	../src/browse/BrowsePath.cpp ../src/image/ImgBuffer.cpp ../src/platform/win32/FileOperations.cpp
IMAGESOURCES = image/ImageTests.cpp support/JpegFixture.cpp support/TempFile.cpp $(TESTMAIN) \
	../src/image/ColorProfile.cpp ../src/image/ColorTransform.cpp ../src/image/ExifOrientation.cpp \
	../src/image/ImgResampler.cpp ../src/image/ImgBuffer.cpp ../src/image/ImgGDIItem.cpp ../src/image/ImgItem.cpp \
	../src/image/ImgItemHelper.cpp ../src/image/ImgJPEGDecoder.cpp
CONCURRENCYSOURCES = concurrency/ConcurrencyTests.cpp support/JpegFixture.cpp support/TempFile.cpp $(TESTMAIN) \
	../src/browse/BrowsePath.cpp ../src/browse/ImgBrowser.cpp ../src/browse/ImgFileList.cpp \
	../src/image/ColorProfile.cpp ../src/image/ColorTransform.cpp ../src/image/ExifOrientation.cpp \
	../src/image/ImgResampler.cpp ../src/image/ImageDispatcher.cpp ../src/image/ImgLoader.cpp \
	../src/image/ImageFormatDetector.cpp ../src/image/ImageFormatResolver.cpp ../src/image/ImgBuffer.cpp \
	../src/image/ImgGDIItem.cpp ../src/image/ImgHEIFItem.cpp ../src/image/ImgItem.cpp \
	../src/image/ImgItemHelper.cpp ../src/image/ImgJPEGDecoder.cpp ../src/image/ImgJPEGItem.cpp \
	../src/platform/win32/ImageHeaderProbe.cpp
UISOURCES = ui/RendererTests.cpp $(TESTMAIN) ../src/ui/win32/ImgRenderer.cpp

ifeq ($(DUMPMACHINE),x86_64-w64-mingw32)
	JPEGLIBDIR = ../3rd-party/libjpeg-turbo/ucrt64
else
	JPEGLIBDIR = ../3rd-party/libjpeg-turbo
endif

ifeq ($(DUMPMACHINE),x86_64-w64-mingw32)
	LCMSLIBDIR = ../3rd-party/Little-CMS/ucrt64
else
	LCMSLIBDIR = ../3rd-party/Little-CMS
endif

ifeq ($(DUMPMACHINE),x86_64-w64-mingw32)
	HEIFLIBDIR = ../3rd-party/libheif/ucrt64
	DE265LIBDIR = ../3rd-party/libde265/ucrt64
else
	HEIFLIBDIR = ../3rd-party/libheif
	DE265LIBDIR = ../3rd-party/libde265
endif

LDLIBS = -L$(JPEGLIBDIR) -L$(LCMSLIBDIR) -L$(HEIFLIBDIR) -L$(DE265LIBDIR) \
	-lshell32 -lole32 -lgdi32 -lgdiplus -lshlwapi -ljpeg -llcms2 -lheif -lde265

.PHONY: all clean test test-core test-platform test-image test-concurrency test-ui

all: $(TARGET)

$(TARGETDIR):
	mkdir -p $@

$(TARGET): $(SOURCES) $(HEADERS) | $(TARGETDIR)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDLIBS)

$(CORETARGET): $(CORESOURCES) $(HEADERS) | $(TARGETDIR)
	$(CXX) $(CXXFLAGS) -DIMGVW_TEST_ENTRY=RunCoreTests $(CORESOURCES) -o $@ $(LDLIBS)

$(PLATFORMTARGET): $(PLATFORMSOURCES) $(HEADERS) | $(TARGETDIR)
	$(CXX) $(CXXFLAGS) -DIMGVW_TEST_ENTRY=RunPlatformTests $(PLATFORMSOURCES) -o $@ $(LDLIBS)

$(IMAGETARGET): $(IMAGESOURCES) $(HEADERS) | $(TARGETDIR)
	$(CXX) $(CXXFLAGS) -DIMGVW_TEST_ENTRY=RunImageTests $(IMAGESOURCES) -o $@ $(LDLIBS)

$(CONCURRENCYTARGET): $(CONCURRENCYSOURCES) $(HEADERS) | $(TARGETDIR)
	$(CXX) $(CXXFLAGS) -DIMGVW_TEST_ENTRY=RunConcurrencyTests $(CONCURRENCYSOURCES) -o $@ $(LDLIBS)

$(UITARGET): $(UISOURCES) $(HEADERS) | $(TARGETDIR)
	$(CXX) $(CXXFLAGS) -DIMGVW_TEST_ENTRY=RunUiTests $(UISOURCES) -o $@ $(LDLIBS)

test: $(TARGET)
	./$(TARGET)

test-core: $(CORETARGET)
	./$(CORETARGET)

test-platform: $(PLATFORMTARGET)
	./$(PLATFORMTARGET)

test-image: $(IMAGETARGET)
	./$(IMAGETARGET)

test-concurrency: $(CONCURRENCYTARGET)
	./$(CONCURRENCYTARGET)

test-ui: $(UITARGET)
	./$(UITARGET)

clean:
	$(RM) -r bin

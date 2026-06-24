CXX ?= g++
DUMPMACHINE = $(shell $(CXX) -dumpmachine)
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DUNICODE -D_UNICODE \
	-I../src/browse -I../src/image -I../src/platform/win32 -I../src/ui/win32 -I../resources \
	-isystem ../3rd-party/libjpeg-turbo -isystem ../3rd-party/Little-CMS
TARGET = ImgVwTests.exe
SOURCES = ImgVwTests.cpp ../src/browse/ImgFileList.cpp ../src/image/ColorProfile.cpp \
	../src/image/ColorTransform.cpp ../src/image/ExifOrientation.cpp ../src/image/ImgResampler.cpp \
	../src/image/ImgLoader.cpp ../src/image/ImageFormatDetector.cpp ../src/image/ImgBuffer.cpp \
	../src/image/ImgGDIItem.cpp ../src/image/ImgItem.cpp ../src/image/ImgItemHelper.cpp \
	../src/image/ImgJpegDecoder.cpp ../src/platform/win32/FileOperations.cpp ../src/platform/win32/ImageHeaderProbe.cpp \
	../src/ui/win32/ImgRenderer.cpp

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

LDLIBS = -L$(JPEGLIBDIR) -L$(LCMSLIBDIR) -lshell32 -lole32 -lgdi32 -lgdiplus -lshlwapi -ljpeg -llcms2

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDLIBS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)

CC = g++

SYS = ${MSYSTEM}
DUMPMACHINE = ${shell ${CC} -dumpmachine}

WARNS = -Wall

EXE = ImgVw.exe

WINDRES = windres

arch ?= x86
BINDIR = bin/${arch}
OBJDIR = obj/${arch}
SOURCE_DIRS = src/app src/browse src/image src/platform/win32 src/ui/win32

vpath %.cpp ${SOURCE_DIRS}
vpath %.rc resources

OBJECTS = ${OBJDIR}/ExifOrientation.o ${OBJDIR}/FileOperations.o ${OBJDIR}/ImgResampler.o ${OBJDIR}/ImgFileList.o ${OBJDIR}/ImgRenderer.o ${OBJDIR}/ImgBrowser.o ${OBJDIR}/ImgBuffer.o ${OBJDIR}/ImgGDIItem.o ${OBJDIR}/ImgHEIFItem.o ${OBJDIR}/ImgItem.o ${OBJDIR}/ImgItemHelper.o ${OBJDIR}/ImgJpegDecoder.o ${OBJDIR}/ImgJPEGItem.o ${OBJDIR}/ImgLoader.o ${OBJDIR}/ImgVwWindow.o ${OBJDIR}/Program.o ${OBJDIR}/Window.o ${OBJDIR}/ImgVw.o

CFLAGS = -std=c++17 -I. -Isrc/app -Isrc/browse -Isrc/image -Isrc/platform/win32 -Isrc/ui/win32 -Iresources -isystem 3rd-party/libjpeg-turbo -isystem 3rd-party/Little-CMS -isystem 3rd-party/libheif -isystem 3rd-party/libde265 -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DUNICODE -D_UNICODE -DLIBHEIF_STATIC_BUILD -DLIBDE265_STATIC_BUILD -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS -O2 ${WARNS} -fmessage-length=0 -fasynchronous-unwind-tables

ifeq (${DUMPMACHINE},x86_64-w64-mingw32)
	CFLAGS := -m64 ${CFLAGS}
endif

ifeq (${arch},x64)
	LDPATHS = -L3rd-party/libjpeg-turbo/ucrt64 -L3rd-party/Little-CMS/ucrt64 -L3rd-party/libheif/ucrt64 -L3rd-party/libde265/ucrt64
else
	LDPATHS = -L3rd-party/libjpeg-turbo -L3rd-party/Little-CMS -L3rd-party/libheif -L3rd-party/libde265
endif

LDLIBS = -lmsimg32 -lcomctl32 -lshlwapi -lole32 -lgdiplus -luxtheme -ljpeg -llcms2 -lheif -lde265

ifneq (${config},release)
	CFLAGS += -D_DEBUG -g
	LDFLAGS = -static -mwindows
else
	LDFLAGS = -s -static -mwindows
endif

    
.PHONY: all clean test

all: ${BINDIR}/${EXE}

test:
	${MAKE} -C tests test

clean:
ifneq (${SYS},)
	@if [ -d "${BINDIR}" ]; then rm -r "${BINDIR}"; fi
	@if [ -d "${OBJDIR}" ]; then rm -r "${OBJDIR}"; fi
else
	@if exist ${BINDIR}\* del /f /s /q ${BINDIR} 1>nul & rd /s /q ${BINDIR}
	@if exist ${OBJDIR}\* del /f /s /q ${OBJDIR} 1>nul & rd /s /q ${OBJDIR}
endif

${BINDIR} ${OBJDIR}:
ifneq (${SYS},)
	@if [ ! -d "$@" ]; then mkdir -p "$@"; fi
else
	@if not exist "$@" mkdir "$@"
endif

${OBJDIR}/ImgVw.o: ImgVw.rc | ${OBJDIR}
	${WINDRES} --include-dir resources --language 0x0409 "$<" -o ${OBJDIR}/ImgVw.o

${OBJDIR}/%.o: %.cpp | ${OBJDIR}
	${CC} ${CFLAGS} -c "$<" -o "$@"

${BINDIR}/${EXE}: ${OBJECTS} | ${BINDIR}
	${CC} ${LDFLAGS} ${LDPATHS} -o "$@" ${OBJECTS} ${LDLIBS}


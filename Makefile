CC = g++

SYS = ${MSYSTEM}
DUMPMACHINE = ${shell ${CC} -dumpmachine}

WARNS = -Wall

EXE = ImgVw.exe

WINDRES = windres

arch ?= x86
config ?= debug
BINDIR = bin/${arch}
OBJARCHDIR = obj/${arch}
OBJDIR = ${OBJARCHDIR}/${config}
SOURCE_DIRS = src/app src/browse src/image src/platform/win32 src/ui/win32

vpath %.cpp ${SOURCE_DIRS}
vpath %.rc resources

OBJECTS = ${OBJDIR}/BrowsePath.o ${OBJDIR}/BrowseSession.o ${OBJDIR}/ColorProfile.o ${OBJDIR}/ColorTransform.o ${OBJDIR}/DisplayPresenter.o ${OBJDIR}/EmptyStateView.o ${OBJDIR}/ExifOrientation.o ${OBJDIR}/FileOperations.o ${OBJDIR}/FolderScanner.o ${OBJDIR}/InfoOverlay.o ${OBJDIR}/ImgDispatcher.o ${OBJDIR}/ImgFormatDetector.o ${OBJDIR}/ImgFormatResolver.o ${OBJDIR}/ImgHeaderProbe.o ${OBJDIR}/ImgResampler.o ${OBJDIR}/ImgFileList.o ${OBJDIR}/ImgRenderer.o ${OBJDIR}/ImgBrowser.o ${OBJDIR}/ImgBuffer.o ${OBJDIR}/ImgGDIItem.o ${OBJDIR}/ImgHEIFItem.o ${OBJDIR}/ImgItem.o ${OBJDIR}/ImgItemHelper.o ${OBJDIR}/ImgJPEGDecoder.o ${OBJDIR}/ImgJPEGItem.o ${OBJDIR}/ImgLoader.o ${OBJDIR}/ImgVwWindow.o ${OBJDIR}/PathPicker.o ${OBJDIR}/PreloadScheduler.o ${OBJDIR}/ProcessDpiAwareness.o ${OBJDIR}/Program.o ${OBJDIR}/Window.o ${OBJDIR}/ImgVw.o
DEPENDENCIES = ${OBJECTS:.o=.d}

CFLAGS = -std=c++17 -I. -Isrc/app -Isrc/browse -Isrc/image -Isrc/platform/win32 -Isrc/ui/win32 -Iresources -isystem 3rd-party/libjpeg-turbo -isystem 3rd-party/Little-CMS -isystem 3rd-party/libheif -isystem 3rd-party/libde265 -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DUNICODE -D_UNICODE -DLIBHEIF_STATIC_BUILD -DLIBDE265_STATIC_BUILD -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS -O2 ${WARNS} -fmessage-length=0 -fasynchronous-unwind-tables -MMD -MP

ifeq (${DUMPMACHINE},x86_64-w64-mingw32)
	CFLAGS := -m64 ${CFLAGS}
endif

ifeq (${arch},x64)
	LDPATHS = -L3rd-party/libjpeg-turbo/ucrt64 -L3rd-party/Little-CMS/ucrt64 -L3rd-party/libheif/ucrt64 -L3rd-party/libde265/ucrt64
else
	LDPATHS = -L3rd-party/libjpeg-turbo -L3rd-party/Little-CMS -L3rd-party/libheif -L3rd-party/libde265
endif

LDLIBS = -lmsimg32 -lcomctl32 -ladvapi32 -lshell32 -lshlwapi -lole32 -luuid -lgdiplus -luxtheme -ljpeg -llcms2 -lheif -lde265

ifneq (${config},release)
	CFLAGS += -D_DEBUG -g
	LDFLAGS = -static -mwindows
else
	LDFLAGS = -s -static -mwindows
endif

    
.PHONY: all clean force test

all: ${BINDIR}/${EXE}

test:
	${MAKE} -C tests test

clean:
ifneq (${SYS},)
	@if [ -d "${BINDIR}" ]; then rm -r "${BINDIR}"; fi
	@if [ -d "${OBJARCHDIR}" ]; then rm -r "${OBJARCHDIR}"; fi
else
	@if exist ${BINDIR}\* del /f /s /q ${BINDIR} 1>nul & rd /s /q ${BINDIR}
	@if exist ${OBJARCHDIR}\* del /f /s /q ${OBJARCHDIR} 1>nul & rd /s /q ${OBJARCHDIR}
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

${BINDIR}/${EXE}: ${OBJECTS} force | ${BINDIR}
	${CC} ${LDFLAGS} ${LDPATHS} -o "$@" ${OBJECTS} ${LDLIBS}

force:

-include ${DEPENDENCIES}

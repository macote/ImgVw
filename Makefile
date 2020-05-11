CC = g++

SYS = ${MSYSTEM}
DUMPMACHINE = ${shell ${CC} -dumpmachine}

WARNS = -Wall

EXE = ImgVw.exe

WINDRES = windres

OBJECTS = obj/FileStream.o obj/ImgBrowser.o obj/ImgBuffer.o obj/ImgGDIItem.o obj/ImgItem.o obj/ImgItemHelper.o obj/ImgJPEGItem.o obj/ImgLoader.o obj/ImgVwWindow.o obj/Program.o obj/turbojpeg_ImgVw.o obj/Window.o obj/ImgVw.o obj/exif.o

CFLAGS = -std=c++17 -I3rd-party/libjpeg-turbo -I3rd-party/easyexif -I3rd-party/Little-CMS -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DUNICODE -D_UNICODE -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS -O2 ${WARNS} -fmessage-length=0 -fasynchronous-unwind-tables

ifeq (${DUMPMACHINE},x86_64-w64-mingw32) 
	CFLAGS = -m64 + ${CFLAGS}
endif

LDPATHS = -L3rd-party/libjpeg-turbo -L3rd-party/Little-CMS

LDLIBS = -lmsimg32 -lcomctl32 -lshlwapi -lole32 -lgdiplus -luxtheme -ljpeg -llcms2

ifneq (${config},release)
	CFLAGS += -D_DEBUG -g
	LDFLAGS = -static -mwindows
else
	LDFLAGS = -s -static -mwindows
endif

    
.PHONY: all clean

all: bin/${EXE}

clean:
ifeq (${SYS}, MINGW64)
	@if [ -d "bin" ]; then rm -r bin; fi
	@if [ -d "obj" ]; then rm -r obj; fi
else
	@if exist bin\* del /f /s /q bin 1>nul & rd /s /q bin
	@if exist obj\* del /f /s /q obj 1>nul & rd /s /q obj
endif

bin obj:
ifeq (${SYS}, MINGW64)
	@if [ ! -d "bin" ]; then mkdir bin; fi
	@if [ ! -d "obj" ]; then mkdir obj; fi
else
	@if not exist "$@" mkdir "$@"
endif

obj/exif.o: 3rd-party/easyexif/exif.cpp | obj
	${CC} ${CFLAGS} -c 3rd-party/easyexif/exif.cpp -o obj/exif.o

obj/ImgVw.o: ImgVw.rc
	${WINDRES} --language 0x0409 ImgVw.rc -o obj/ImgVw.o

obj/%.o: %.cpp | obj
	${CC} ${CFLAGS} -c "$<" -o "$@"

bin/${EXE}: ${OBJECTS} | bin
	${CC} ${LDFLAGS} ${LDPATHS} -o "$@" ${OBJECTS} ${LDLIBS}


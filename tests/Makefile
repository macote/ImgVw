CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DUNICODE -D_UNICODE \
	-I../src/browse -I../src/image -I../src/platform/win32 -I../src/ui/win32 -isystem ../3rd-party/Little-CMS
TARGET = ImgVwTests.exe
SOURCES = ImgVwTests.cpp ../src/browse/ImgFileList.cpp ../src/image/ImgLoader.cpp \
	../src/platform/win32/FileOperations.cpp ../src/ui/win32/ImgRenderer.cpp
LDLIBS = -lshell32 -lgdi32

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDLIBS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)

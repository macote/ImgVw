CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DUNICODE -D_UNICODE \
	-I../src/browse -I../src/image -I../src/platform/win32 -isystem ../3rd-party/Little-CMS
TARGET = ImgFileListTests.exe
SOURCES = ImgFileListTests.cpp ../src/browse/ImgFileList.cpp ../src/image/ImgLoader.cpp

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)

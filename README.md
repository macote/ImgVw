# ImgVw

ImgVw is a simple and fast image viewer for Windows.

## Features

- Fast disk-based display caching
- Auto-rotate JPEG images based on EXIF information

## How to build

This project is compatible with:
- Visual Studio 2017
- Eclipse Neon CDT with MinGW (GCC 6.3.0)

## Usage

Pass a file or folder as an argument.

| Shortcut | Description |
|:-:|-|
| Left Arrow | Browse backward |
| Right Arrow | Browse forward |
| Delete | Move to recycle bin if possible or delete |
| Escape | Exit |

## 3rd-party libraries

ImgVw uses the following libraries :
- [libjpeg-turbo](http://libjpeg-turbo.virtualgl.org/)
- [easyexif](https://github.com/mayanklahiri/easyexif)

## License

ImgVw is BSD-licensed.
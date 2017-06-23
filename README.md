# ImgVw

ImgVw is a simple, fast and portable image viewer for Windows.

## Features

- Fast display caching
- Copy and run, no installation required
- Auto-rotate JPEG images based on EXIF information
- Works on Windows XP and later

## How to build

This project is compatible with:
- Visual Studio 2017
- Eclipse Neon CDT with MinGW (GCC 6.3.0)

## Usage

Pass a file or folder as an argument.

| Shortcut | Description |
|:-:|:-|
| Left Arrow \| Mouse Wheel Up | Browse backward |
| Right Arrow \| Mouse Wheel Down | Browse forward |
| Home | Go to first |
| End | Go to last |
| F5 | Toggle slideshow |
| F6 | Increase slideshow speed |
| F7 | Decrease slideshow speed |
| Delete | Move to recycle bin if possible or delete |
| Shift + Delete | Delete |
| Escape | Exit |

## 3rd-party libraries

ImgVw uses the following libraries :
- [libjpeg-turbo](http://libjpeg-turbo.virtualgl.org/)
- [easyexif](https://github.com/mayanklahiri/easyexif)

## License

ImgVw is BSD-licensed.
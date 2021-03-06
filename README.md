# ImgVw

ImgVw is a simple, fast and portable image viewer for Windows.

## Features

- Fast display caching
- Copy and run, no installation required
- Auto-rotate JPEG images based on EXIF information
- Works on Windows XP and later

## Usage

Pass a file or folder as an argument.

| Shortcut | Description |
|:-:|:-|
| Left Arrow \| Mouse Wheel Up | Browse backward |
| Right Arrow \| Mouse Wheel Down | Browse forward |
| Home | Go to first |
| End | Go to last |
| F5 | Toggle slideshow |
| Shift + F5 | Toggle slideshow (random mode) |
| F6 | Increase slideshow speed |
| F7 | Decrease slideshow speed |
| F8 | Add images found in subfolders |
| Ctrl + I | Select default ICC profile |
| Delete | Move to recycle bin if possible or delete |
| Shift + Delete | Delete |
| Enter | Display current file path |
| Escape | Exit |

## 3rd-party libraries

ImgVw uses the following libraries :
- [easyexif](https://github.com/mayanklahiri/easyexif)
- [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo)
- [Little-CMS](https://github.com/mm2/Little-CMS)

## License

ImgVw is BSD-licensed.
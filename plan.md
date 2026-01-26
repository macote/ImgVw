1.  *Initialize Rust Project*
    - Create a new cargo project named `imgvw-rs`.
    - Add dependencies: `winit`, `softbuffer`, `image`, `walkdir`, `anyhow`, `log`, `env_logger`, `crossbeam-channel`.

2.  *Implement Image Loading and Caching*
    - Create a module `loader` to handle image loading.
    - Implement a `Loader` struct that runs a background thread.
    - It should accept paths to load and return decoded image buffers (e.g., `RgbaImage`).
    - Implement basic caching (LRU or simple map) to store loaded images.
    - Handle image rotation based on EXIF data (using `image` crate's `guess_format` and `load`, it might handle some orientation, but explicit EXIF check might be needed. For MVP, we can rely on `image` crate's auto handling if available or skip EXIF for the first step). *Update: `image` crate usually doesn't auto-rotate based on EXIF unless using specific readers, but we can stick to basic loading first.*

3.  *Implement File Browser*
    - Create a module `browser`.
    - Implement scanning the current directory (or a target directory) for supported image files.
    - Maintain a list of files and the current index.
    - Provide methods to go Next/Prev/Random.

4.  *Implement Main Window and Event Loop*
    - In `main.rs`, set up `winit` event loop.
    - Create a window using `winit`.
    - Initialize `softbuffer` context for the window.
    - Integrate `browser` and `loader`.
    - Handle keyboard input (Left/Right arrow, Esc to exit).

5.  *Implement Rendering*
    - On `RedrawRequested`, get the current image from the loader/cache.
    - Resize the image to fit the window (aspect ratio preserved) using `image::imageops::resize`.
    - Draw the resized image into the `softbuffer` buffer.
    - Handle window resize events.

6.  *Verify and Refine*
    - Run the application and test with sample images.
    - Ensure basic navigation works.
    - Ensure performance is acceptable (basic resizing might be slow on UI thread, might need to offload resizing or cache resized versions). *Self-correction: Resizing every frame on resize is slow. We should cache the display buffer or resize only when window size changes or image changes.*

7.  *Complete pre commit steps*
    - Run `cargo fmt` and `cargo clippy`.
    - Ensure the code builds and runs.

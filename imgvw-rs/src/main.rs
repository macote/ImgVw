mod loader;
mod browser;

use winit::event::{Event, WindowEvent, KeyEvent, ElementState};
use winit::keyboard::{Key, NamedKey};
use winit::event_loop::{EventLoop, ControlFlow};
use winit::window::WindowBuilder;
use softbuffer::{Context, Surface};
use std::sync::Arc;
use browser::Browser;
use loader::{Loader, LoadRequest, LoadResult};
use image::imageops::FilterType;

fn main() -> anyhow::Result<()> {
    env_logger::init();

    let event_loop = EventLoop::new()?;
    let window = Arc::new(WindowBuilder::new()
        .with_title("ImgVw RS")
        .with_inner_size(winit::dpi::LogicalSize::new(800.0, 600.0))
        .build(&event_loop)?);

    let context = Context::new(window.clone()).map_err(|e| anyhow::anyhow!("Context creation failed: {}", e))?;
    let mut surface = Surface::new(&context, window.clone()).map_err(|e| anyhow::anyhow!("Surface creation failed: {}", e))?;

    let mut browser = Browser::new();
    let loader = Loader::new();

    // Start by loading current directory
    if let Ok(cwd) = std::env::current_dir() {
        let _ = browser.load_directory(&cwd);
    }

    // If command line args provided
    if let Some(arg) = std::env::args().nth(1) {
        let path = std::path::PathBuf::from(arg);
        if path.is_dir() {
            let _ = browser.load_directory(&path);
        } else if path.is_file() {
            if let Some(parent) = path.parent() {
                 let _ = browser.load_directory(parent);
                 browser.set_current_file(&path);
            }
        }
    }

    // Trigger first load
    if let Some(path) = browser.current_path() {
        loader.send_request(LoadRequest::Load(path.clone()));
    }

    let mut current_image: Option<image::RgbaImage> = None;

    event_loop.run(move |event, elwt| {
        elwt.set_control_flow(ControlFlow::Poll);

        // Check for loader results
        while let Some(res) = loader.try_recv() {
             match res {
                 LoadResult::Loaded { path, image } => {
                     if let Some(current_path) = browser.current_path() {
                         if *current_path == path {
                             current_image = Some(image);
                             window.request_redraw();
                             window.set_title(&format!("ImgVw RS - {:?}", path));
                         }
                     }
                 },
                 LoadResult::Error { path, error } => {
                     eprintln!("Failed to load {:?}: {}", path, error);
                 }
             }
        }

        match event {
            Event::WindowEvent { window_id, event } if window_id == window.id() => {
                match event {
                    WindowEvent::CloseRequested => elwt.exit(),
                    WindowEvent::KeyboardInput { event: KeyEvent { state: ElementState::Pressed, logical_key, .. }, .. } => {
                        let mut needs_load = false;
                        match logical_key {
                            Key::Named(NamedKey::Escape) => elwt.exit(),
                            Key::Named(NamedKey::ArrowRight) => {
                                browser.next();
                                needs_load = true;
                            },
                            Key::Named(NamedKey::ArrowLeft) => {
                                browser.prev();
                                needs_load = true;
                            },
                             _ => {}
                        }

                        if needs_load {
                            if let Some(path) = browser.current_path() {
                                loader.send_request(LoadRequest::Load(path.clone()));
                            }
                        }
                    },
                    WindowEvent::RedrawRequested => {
                        let (width, height) = {
                            let size = window.inner_size();
                            (size.width, size.height)
                        };

                        if width > 0 && height > 0 {
                             let _ = surface.resize(
                                 std::num::NonZeroU32::new(width).unwrap(),
                                 std::num::NonZeroU32::new(height).unwrap(),
                             );

                             let mut buffer = surface.buffer_mut().unwrap();

                             // Fill with black
                             buffer.fill(0);

                             if let Some(img) = &current_image {
                                 let img_width = img.width();
                                 let img_height = img.height();

                                 if img_width > 0 && img_height > 0 {
                                     let scale = (width as f64 / img_width as f64).min(height as f64 / img_height as f64);
                                     let new_width = (img_width as f64 * scale) as u32;
                                     let new_height = (img_height as f64 * scale) as u32;

                                     if new_width > 0 && new_height > 0 {
                                         // Use Triangle filter for speed/quality balance. Nearest is faster.
                                         // For smoother resizing, Triangle is okay.
                                         let resized = image::imageops::resize(img, new_width, new_height, FilterType::Triangle);

                                         let x_offset = (width - new_width) / 2;
                                         let y_offset = (height - new_height) / 2;

                                         for y in 0..new_height {
                                             for x in 0..new_width {
                                                 // Unsafe get_pixel might be faster but checked is safer
                                                 let pixel = resized.get_pixel(x, y);
                                                 let r = pixel[0] as u32;
                                                 let g = pixel[1] as u32;
                                                 let b = pixel[2] as u32;

                                                 let color = (r << 16) | (g << 8) | b;

                                                 let buffer_idx = ((y + y_offset) * width + (x + x_offset)) as usize;
                                                 if buffer_idx < buffer.len() {
                                                     buffer[buffer_idx] = color;
                                                 }
                                             }
                                         }
                                     }
                                 }
                             }

                             let _ = buffer.present();
                        }
                    },
                    _ => {}
                }
            },
            _ => {}
        }
    })?;

    Ok(())
}

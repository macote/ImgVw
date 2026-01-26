use crossbeam_channel::{Sender, Receiver};
use image::{RgbaImage, ImageReader};
use std::path::PathBuf;
use std::thread;
use std::collections::HashMap;

pub struct Loader {
    request_tx: Sender<LoadRequest>,
    result_rx: Receiver<LoadResult>,
    handle: Option<thread::JoinHandle<()>>,
}

#[derive(Debug)]
pub enum LoadRequest {
    Load(PathBuf),
    Quit,
}

#[derive(Debug)]
pub enum LoadResult {
    Loaded { path: PathBuf, image: RgbaImage },
    Error { path: PathBuf, error: String },
}

impl Loader {
    pub fn new() -> Self {
        let (request_tx, request_rx) = crossbeam_channel::unbounded();
        let (result_tx, result_rx) = crossbeam_channel::unbounded();

        let handle = thread::spawn(move || {
            let mut cache: HashMap<PathBuf, RgbaImage> = HashMap::new();

            while let Ok(req) = request_rx.recv() {
                match req {
                    LoadRequest::Load(path) => {
                        if let Some(img) = cache.get(&path) {
                             let _ = result_tx.send(LoadResult::Loaded { path: path.clone(), image: img.clone() });
                        } else {
                            let load_res = (|| -> Result<RgbaImage, String> {
                                let reader = ImageReader::open(&path).map_err(|e| e.to_string())?;
                                let reader = reader.with_guessed_format().map_err(|e| e.to_string())?;
                                let dyn_img = reader.decode().map_err(|e| e.to_string())?;
                                Ok(dyn_img.to_rgba8())
                            })();

                            let res = match load_res {
                                Ok(img) => {
                                    cache.insert(path.clone(), img.clone());

                                    // Simple cache eviction
                                    if cache.len() > 20 {
                                        if let Some(key) = cache.keys().next().cloned() {
                                            cache.remove(&key);
                                        }
                                    }

                                    LoadResult::Loaded { path, image: img }
                                },
                                Err(e) => LoadResult::Error { path, error: e },
                            };
                            let _ = result_tx.send(res);
                        }
                    },
                    LoadRequest::Quit => break,
                }
            }
        });

        Self {
            request_tx,
            result_rx,
            handle: Some(handle),
        }
    }

    pub fn send_request(&self, req: LoadRequest) {
        let _ = self.request_tx.send(req);
    }

    pub fn try_recv(&self) -> Option<LoadResult> {
        self.result_rx.try_recv().ok()
    }
}

impl Drop for Loader {
    fn drop(&mut self) {
        let _ = self.request_tx.send(LoadRequest::Quit);
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
    }
}

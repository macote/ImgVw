use std::path::{Path, PathBuf};
use walkdir::WalkDir;

pub struct Browser {
    files: Vec<PathBuf>,
    current_index: usize,
}

impl Browser {
    pub fn new() -> Self {
        Self {
            files: Vec::new(),
            current_index: 0,
        }
    }

    pub fn load_directory(&mut self, path: impl AsRef<Path>) -> Result<(), std::io::Error> {
        self.files.clear();
        let path = path.as_ref();

        for entry in WalkDir::new(path).max_depth(1).into_iter().filter_map(|e| e.ok()) {
             let p = entry.path();
             if p.is_file() {
                 if let Some(ext) = p.extension().and_then(|s| s.to_str()) {
                     let ext = ext.to_lowercase();
                     match ext.as_str() {
                         "jpg" | "jpeg" | "png" | "bmp" | "gif" | "webp" | "tiff" => {
                             self.files.push(p.to_path_buf());
                         }
                         _ => {}
                     }
                 }
             }
        }

        // Sort files alphabetically
        self.files.sort();
        self.current_index = 0;
        Ok(())
    }

    pub fn set_current_file(&mut self, path: &Path) {
        if let Some(pos) = self.files.iter().position(|p| p == path) {
            self.current_index = pos;
        }
    }

    pub fn current_path(&self) -> Option<&PathBuf> {
        if self.files.is_empty() {
            None
        } else {
            self.files.get(self.current_index)
        }
    }

    pub fn next(&mut self) {
        if self.files.is_empty() { return; }
        if self.current_index + 1 < self.files.len() {
            self.current_index += 1;
        } else {
            self.current_index = 0;
        }
    }

    pub fn prev(&mut self) {
        if self.files.is_empty() { return; }
        if self.current_index > 0 {
            self.current_index -= 1;
        } else {
            self.current_index = self.files.len() - 1;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use tempfile::tempdir;

    #[test]
    fn test_browser_navigation() {
        let dir = tempdir().unwrap();
        let file1 = dir.path().join("a.jpg");
        let file2 = dir.path().join("b.png");
        File::create(&file1).unwrap();
        File::create(&file2).unwrap();

        let mut browser = Browser::new();
        browser.load_directory(dir.path()).unwrap();

        assert_eq!(browser.current_path(), Some(&file1));

        browser.next();
        assert_eq!(browser.current_path(), Some(&file2));

        browser.next();
        assert_eq!(browser.current_path(), Some(&file1)); // loop

        browser.prev();
        assert_eq!(browser.current_path(), Some(&file2));
    }
}

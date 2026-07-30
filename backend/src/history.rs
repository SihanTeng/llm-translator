//! JSON translation history (`{data_dir}/history.json`), newest first,
//! capped at 500 entries. Writes are atomic (temp file + rename).

use serde::{Deserialize, Serialize};
use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

pub const MAX_ENTRIES: usize = 500;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct HistoryEntry {
    #[serde(default)]
    pub ts: i64,
    #[serde(default)]
    pub source: String,
    #[serde(default)]
    pub translation: String,
}

pub struct HistoryStore {
    path: PathBuf,
    entries: Vec<HistoryEntry>,
    loaded: bool,
}

impl HistoryStore {
    pub fn new(path: PathBuf) -> Self {
        Self {
            path,
            entries: Vec::new(),
            loaded: false,
        }
    }

    /// Appends an entry at the front, trimming both fields; empty source or
    /// translation is skipped. Persists immediately.
    pub fn add(&mut self, source: &str, translation: &str) {
        let source = source.trim();
        let translation = translation.trim();
        if source.is_empty() || translation.is_empty() {
            return;
        }
        self.ensure_loaded();
        let ts = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs() as i64)
            .unwrap_or(0);
        self.entries.insert(
            0,
            HistoryEntry {
                ts,
                source: source.to_string(),
                translation: translation.to_string(),
            },
        );
        self.entries.truncate(MAX_ENTRIES);
        self.save();
    }

    pub fn entries(&mut self) -> &[HistoryEntry] {
        self.ensure_loaded();
        &self.entries
    }

    /// The full array as compact JSON (`[{"ts":..,"source":..,"translation":..}]`).
    pub fn to_json(&mut self) -> String {
        self.ensure_loaded();
        serde_json::to_string(&self.entries).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn clear(&mut self) {
        self.ensure_loaded();
        self.entries.clear();
        self.save();
    }

    /// Loads lazily on first use; a missing, empty or corrupt file means an
    /// empty list. Malformed entries are skipped individually (a bad object
    /// must not destroy the rest of the history on the next save).
    fn ensure_loaded(&mut self) {
        if self.loaded {
            return;
        }
        self.loaded = true;
        let Ok(bytes) = fs::read(&self.path) else {
            return;
        };
        let Ok(values) = serde_json::from_slice::<Vec<serde_json::Value>>(&bytes) else {
            return;
        };
        self.entries = values
            .iter()
            .map(|value| HistoryEntry {
                ts: value.get("ts").and_then(|v| v.as_i64()).unwrap_or_default(),
                source: value
                    .get("source")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default()
                    .to_string(),
                translation: value
                    .get("translation")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default()
                    .to_string(),
            })
            .filter(|entry| !entry.source.is_empty() && !entry.translation.is_empty())
            .collect();
    }

    fn save(&self) {
        if let Some(parent) = self.path.parent() {
            if fs::create_dir_all(parent).is_err() {
                return;
            }
        }
        let mut tmp = self.path.clone().into_os_string();
        tmp.push(".tmp");
        let tmp = PathBuf::from(tmp);
        let json = serde_json::to_string(&self.entries).unwrap_or_default();
        let Ok(mut file) = fs::File::create(&tmp) else {
            return;
        };
        if file.write_all(json.as_bytes()).is_err() {
            return;
        }
        let _ = file.sync_all();
        drop(file);
        let _ = fs::rename(&tmp, &self.path);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    fn temp_dir(tag: &str) -> PathBuf {
        static COUNTER: AtomicUsize = AtomicUsize::new(0);
        let unique = format!(
            "translator-backend-test-{}-{}-{}",
            tag,
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::SeqCst)
        );
        let dir = std::env::temp_dir().join(unique);
        let _ = fs::remove_dir_all(&dir);
        dir
    }

    #[test]
    fn add_and_persist_across_instances() {
        let dir = temp_dir("persist");
        let path = dir.join("history.json");

        let mut store = HistoryStore::new(path.clone());
        store.add("hello", "你好");
        store.add("world", "世界");

        // A fresh instance over the same path sees the file's contents.
        let mut reloaded = HistoryStore::new(path);
        let entries = reloaded.entries();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].source, "world");
        assert_eq!(entries[1].source, "hello");
        assert!(entries[0].ts > 0);

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn newest_first_and_trimming() {
        let dir = temp_dir("order");
        let mut store = HistoryStore::new(dir.join("history.json"));
        store.add("  first  ", " one ");
        store.add("second", "two");
        let entries = store.entries();
        assert_eq!(entries[0].source, "second");
        assert_eq!(entries[1].source, "first");
        assert_eq!(entries[1].translation, "one");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn empty_fields_are_skipped() {
        let dir = temp_dir("empty");
        let mut store = HistoryStore::new(dir.join("history.json"));
        store.add("", "translation");
        store.add("source", "   ");
        store.add("   ", "translation");
        assert!(store.entries().is_empty());
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn cap_drops_oldest() {
        let dir = temp_dir("cap");
        let mut store = HistoryStore::new(dir.join("history.json"));
        for i in 0..510 {
            store.add(&format!("source {i}"), &format!("translation {i}"));
        }
        let entries = store.entries();
        assert_eq!(entries.len(), MAX_ENTRIES);
        assert_eq!(entries[0].source, "source 509");
        assert_eq!(entries[MAX_ENTRIES - 1].source, "source 10");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn missing_and_corrupt_files_load_empty() {
        let dir = temp_dir("corrupt");
        let path = dir.join("history.json");

        let mut store = HistoryStore::new(path.clone());
        assert!(store.entries().is_empty());

        fs::create_dir_all(&dir).unwrap();
        fs::write(&path, b"this is not json").unwrap();
        let mut store = HistoryStore::new(path.clone());
        assert!(store.entries().is_empty());

        fs::write(&path, b"").unwrap();
        let mut store = HistoryStore::new(path);
        assert!(store.entries().is_empty());

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn clear_empties_and_persists() {
        let dir = temp_dir("clear");
        let path = dir.join("history.json");
        let mut store = HistoryStore::new(path.clone());
        store.add("hello", "你好");
        store.clear();
        assert!(store.entries().is_empty());

        let mut reloaded = HistoryStore::new(path);
        assert!(reloaded.entries().is_empty());
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn malformed_entries_are_skipped_individually() {
        let dir = temp_dir("badentries");
        let path = dir.join("history.json");
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            &path,
            r#"[
                {"ts": 123, "source": "hello", "translation": "你好"},
                42,
                {"ts": "nope", "source": 5, "translation": true},
                {"source": "", "translation": "dangling"},
                {"ts": 124, "source": "world", "translation": "世界"}
            ]"#,
        )
        .unwrap();

        let mut store = HistoryStore::new(path.clone());
        let entries = store.entries();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].source, "hello");
        assert_eq!(entries[1].source, "world");

        // A save after loading must not lose the surviving entries.
        store.add("new", "新");
        let mut reloaded = HistoryStore::new(path);
        let entries = reloaded.entries();
        assert_eq!(entries.len(), 3);
        assert_eq!(entries[1].source, "hello");
        assert_eq!(entries[2].source, "world");

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn json_shape() {
        let dir = temp_dir("json");
        let mut store = HistoryStore::new(dir.join("history.json"));
        assert_eq!(store.to_json(), "[]");
        store.add("hello", "你好");
        let json = store.to_json();
        let parsed: Vec<HistoryEntry> = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0].source, "hello");
        assert_eq!(parsed[0].translation, "你好");
        assert!(json.starts_with("[{\"ts\":"));
        let _ = fs::remove_dir_all(&dir);
    }
}

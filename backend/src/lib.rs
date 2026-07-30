//! Shared LLM translation backend: provider registry, prompt templates,
//! streaming OpenAI-compatible/Anthropic clients, word-card parsing and
//! JSON history, exposed to the Qt shell through the C ABI in `ffi`
//! (`include/translator_backend.h`).

pub mod client;
pub mod ffi;
pub mod history;
pub mod prompts;
pub mod providers;
pub mod wordcard;

pub use client::{Backend, RequestTimeouts, Sink};
pub use history::{HistoryEntry, HistoryStore};
pub use providers::{provider_by_id, providers, ApiStyle, JsonMode, ProviderInfo};
pub use wordcard::{extract_json_payload, WordCard};

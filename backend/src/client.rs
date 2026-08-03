//! Streaming LLM request engine: one blocking worker thread per request,
//! cancellable via a shared atomic flag. Every `translate` call ends with
//! exactly one `on_done`.

use crate::history::HistoryStore;
use crate::prompts;
use crate::providers::{provider_by_id, ApiStyle, JsonMode, ProviderInfo};
use crate::wordcard::{extract_json_payload, WordCard};
use std::io::{BufRead, BufReader};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::Duration;
use ureq::Agent;

/// Stream callback: one UTF-8 token.
pub type TokenFn = Box<dyn Fn(&str) + Send>;
/// Terminal callback: `ok` plus an error message ("" on success,
/// "cancelled" after `cancel`).
pub type DoneFn = Box<dyn Fn(bool, &str) + Send>;

/// Stream/terminal callbacks for one request. Both fire on the worker thread.
pub struct Sink {
    pub on_token: TokenFn,
    pub on_done: DoneFn,
}

impl Sink {
    fn token(&self, token: &str) {
        (self.on_token)(token);
    }

    fn done(&self, ok: bool, message: &str) {
        (self.on_done)(ok, message);
    }
}

#[derive(Clone)]
struct ClientConfig {
    provider: &'static ProviderInfo,
    api_key: String,
    model: String,
    base_url: String,
}

#[derive(Clone)]
enum ConfigState {
    Unconfigured,
    Invalid(String),
    Ready(ClientConfig),
}

/// Timeouts applied to every outgoing request. Without them a stalled peer
/// would park the worker thread forever, breaking the exactly-one-`on_done`
/// guarantee. Configurable so tests can use short values.
#[derive(Debug, Clone, Copy)]
pub struct RequestTimeouts {
    /// Max time to establish the connection.
    pub connect: Duration,
    /// End-to-end cap per request, DNS through the full response body
    /// (a stalled stream is bounded by this).
    pub global: Duration,
}

impl Default for RequestTimeouts {
    fn default() -> Self {
        Self {
            connect: Duration::from_secs(10),
            global: Duration::from_secs(120),
        }
    }
}

struct Shared {
    agent: Agent,
    config: Mutex<ConfigState>,
    /// Monotonic op id of the in-flight request (0 = none ever started).
    /// Bumped on every `translate` and `cancel` so superseded workers stop
    /// emitting tokens, skip history, and report `cancelled`.
    active_op: AtomicU64,
    history: Mutex<HistoryStore>,
}

impl Shared {
    fn is_current(&self, op: u64) -> bool {
        self.active_op.load(Ordering::SeqCst) == op
    }

    /// Invalidates any in-flight op and returns the new current id.
    fn begin_op(&self) -> u64 {
        self.active_op.fetch_add(1, Ordering::SeqCst) + 1
    }
}

/// The backend instance behind the `TbBackend` handle. Owns the HTTP agent,
/// the current configuration, the active request generation, and the
/// history store.
pub struct Backend {
    shared: Arc<Shared>,
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    // A poisoned mutex (a worker panicked while holding it) must not take
    // down the FFI boundary; the data is still usable.
    mutex.lock().unwrap_or_else(|err| err.into_inner())
}

impl Backend {
    pub fn new(data_dir: PathBuf) -> Backend {
        Self::with_timeouts(data_dir, RequestTimeouts::default())
    }

    pub fn with_timeouts(data_dir: PathBuf, timeouts: RequestTimeouts) -> Backend {
        let config = Agent::config_builder()
            .http_status_as_error(false)
            .timeout_connect(Some(timeouts.connect))
            .timeout_global(Some(timeouts.global))
            .build();
        Backend {
            shared: Arc::new(Shared {
                agent: Agent::new_with_config(config),
                config: Mutex::new(ConfigState::Unconfigured),
                active_op: AtomicU64::new(0),
                history: Mutex::new(HistoryStore::new(data_dir.join("history.json"))),
            }),
        }
    }

    pub fn configure(
        &self,
        provider_id: &str,
        api_key: &str,
        model: &str,
        base_url_override: &str,
    ) {
        let state = match provider_by_id(provider_id) {
            Some(provider) => {
                let effective_base = if base_url_override.is_empty() {
                    provider.base_url.as_str()
                } else {
                    base_url_override
                };
                ConfigState::Ready(ClientConfig {
                    provider,
                    api_key: api_key.to_string(),
                    model: if model.is_empty() {
                        provider.default_model.clone()
                    } else {
                        model.to_string()
                    },
                    // A trailing slash would produce "//chat/completions".
                    base_url: effective_base.trim_end_matches('/').to_string(),
                })
            }
            None => ConfigState::Invalid(format!("Unknown provider: {provider_id}")),
        };
        *lock(&self.shared.config) = state;
    }

    /// Invalidates the in-flight request (if any); its worker reports
    /// `on_done(false, "cancelled")` when it next checks the op id.
    pub fn cancel(&self) {
        let _ = self.shared.begin_op();
    }

    /// Starts a request on a fresh worker thread, cancelling any in-flight
    /// one first. Exactly one `sink.on_done` will fire for this call.
    /// Superseded ops never write history and only report `cancelled`.
    pub fn translate(&self, text: &str, context: &str, target: &str, json_mode: bool, sink: Sink) {
        let op = self.shared.begin_op();
        let state = lock(&self.shared.config).clone();
        let shared = Arc::clone(&self.shared);
        let text = text.to_string();
        let context = context.to_string();
        let target = target.to_string();
        std::thread::spawn(move || {
            let panicked = catch_unwind(AssertUnwindSafe(|| {
                run(
                    &shared, &state, &text, &context, &target, json_mode, op, &sink,
                );
            }))
            .is_err();
            if panicked {
                // Only report a panic if this op is still current — a
                // superseded worker's unwind must not fake an error for the
                // newer request.
                if shared.is_current(op) {
                    sink.done(false, "Internal backend error");
                } else {
                    sink.done(false, "cancelled");
                }
            }
        });
    }

    pub fn history_json(&self) -> String {
        lock(&self.shared.history).to_json()
    }

    pub fn history_clear(&self) {
        lock(&self.shared.history).clear();
    }
}

impl Drop for Backend {
    fn drop(&mut self) {
        self.cancel();
    }
}

struct Request {
    url: String,
    headers: Vec<(&'static str, String)>,
    body: String,
    streamed: bool,
}

fn build_request(
    cfg: &ClientConfig,
    system_prompt: &str,
    user_content: &str,
    json_mode: bool,
) -> Request {
    match cfg.provider.style {
        ApiStyle::OpenAiCompatible => {
            // response_format JSON mode is requested non-streamed (single
            // body); prompt_only providers stream and buffer instead.
            let streamed = !(json_mode && cfg.provider.json_mode == JsonMode::ResponseFormat);
            let mut body = serde_json::json!({
                "model": cfg.model,
                "stream": streamed,
                "messages": [
                    { "role": "system", "content": system_prompt },
                    { "role": "user", "content": user_content },
                ],
            });
            if cfg.provider.disable_thinking {
                body["thinking"] = serde_json::json!({ "type": "disabled" });
            }
            if json_mode && cfg.provider.json_mode == JsonMode::ResponseFormat {
                body["response_format"] = serde_json::json!({ "type": "json_object" });
            }
            Request {
                url: format!("{}/chat/completions", cfg.base_url),
                headers: vec![
                    ("Authorization", format!("Bearer {}", cfg.api_key)),
                    ("Accept", "text/event-stream".to_string()),
                    ("Content-Type", "application/json".to_string()),
                ],
                body: body.to_string(),
                streamed,
            }
        }
        ApiStyle::Anthropic => Request {
            url: format!("{}/v1/messages", cfg.base_url),
            headers: vec![
                ("x-api-key", cfg.api_key.clone()),
                ("anthropic-version", "2023-06-01".to_string()),
                ("Accept", "text/event-stream".to_string()),
                ("Content-Type", "application/json".to_string()),
            ],
            body: serde_json::json!({
                "model": cfg.model,
                // Required by the API; an upper bound only — translations
                // stop far earlier with stop_reason end_turn.
                "max_tokens": 4096,
                "system": system_prompt,
                "messages": [{ "role": "user", "content": user_content }],
                "stream": true,
            })
            .to_string(),
            streamed: true,
        },
    }
}

fn missing_key_message(provider: &ProviderInfo) -> String {
    if provider.env_var.is_empty() {
        format!(
            "No API key configured for {}. Open Settings to add one.",
            provider.name
        )
    } else {
        format!(
            "No API key configured for {}. Open Settings to add one, or set the {} environment variable.",
            provider.name, provider.env_var
        )
    }
}

/// Trims, then caps at `max` chars, appending an ellipsis when truncated.
fn clipped(text: &str, max: usize) -> String {
    let trimmed = text.trim();
    if trimmed.chars().count() > max {
        let mut out: String = trimmed.chars().take(max).collect();
        out.push('…');
        out
    } else {
        trimmed.to_string()
    }
}

/// OpenAI-style SSE payload → text delta ("" to skip).
pub(crate) fn parse_openai_delta(payload: &str) -> String {
    let Ok(doc) = serde_json::from_str::<serde_json::Value>(payload) else {
        return String::new();
    };
    doc["choices"][0]["delta"]["content"]
        .as_str()
        .unwrap_or_default()
        .to_string()
}

/// Anthropic-style SSE payload → text delta ("" to skip).
pub(crate) fn parse_anthropic_delta(payload: &str) -> String {
    let Ok(doc) = serde_json::from_str::<serde_json::Value>(payload) else {
        return String::new();
    };
    if doc["type"].as_str() != Some("content_block_delta") {
        return String::new();
    }
    let delta = &doc["delta"];
    if delta["type"].as_str() != Some("text_delta") {
        return String::new();
    }
    delta["text"].as_str().unwrap_or_default().to_string()
}

/// Non-streamed OpenAI response body → `choices[0].message.content`.
fn parse_openai_message_content(body: &str) -> String {
    let Ok(doc) = serde_json::from_str::<serde_json::Value>(body) else {
        return String::new();
    };
    doc["choices"][0]["message"]["content"]
        .as_str()
        .unwrap_or_default()
        .to_string()
}

/// Terminal for a superseded op: always `cancelled`, never history.
fn cancelled(sink: &Sink) {
    sink.done(false, "cancelled");
}

#[allow(clippy::too_many_arguments)]
fn run(
    shared: &Shared,
    state: &ConfigState,
    text: &str,
    context: &str,
    target: &str,
    json_mode: bool,
    op: u64,
    sink: &Sink,
) {
    // Config/key failures still report for this op only if it is current —
    // a supersede between spawn and run must not surface as a real error.
    if !shared.is_current(op) {
        return cancelled(sink);
    }

    let cfg = match state {
        ConfigState::Ready(cfg) => cfg,
        ConfigState::Invalid(message) => {
            return if shared.is_current(op) {
                sink.done(false, message);
            } else {
                cancelled(sink);
            };
        }
        ConfigState::Unconfigured => {
            return if shared.is_current(op) {
                sink.done(false, "No provider configured");
            } else {
                cancelled(sink);
            };
        }
    };

    if cfg.api_key.is_empty() {
        return if shared.is_current(op) {
            sink.done(false, &missing_key_message(cfg.provider));
        } else {
            cancelled(sink);
        };
    }

    let system_prompt = if json_mode {
        prompts::word(target)
    } else {
        prompts::phrase(target)
    };
    let user_content = if json_mode {
        let mut content = format!("Text: {text}");
        if !context.is_empty() {
            content.push_str("\nSentence: ");
            content.push_str(context);
        }
        content
    } else {
        text.to_string()
    };

    let request = build_request(cfg, &system_prompt, &user_content, json_mode);
    let mut builder = shared.agent.post(&request.url);
    for (name, value) in &request.headers {
        builder = builder.header(*name, value);
    }
    let mut response = match builder.send(request.body.into_bytes()) {
        Ok(response) => response,
        Err(err) => {
            return if shared.is_current(op) {
                sink.done(false, &format!("Network error: {err}"));
            } else {
                cancelled(sink);
            };
        }
    };

    // A supersede during connect/send takes effect before any body read —
    // the only pre-read checkpoint for non-streamed requests.
    if !shared.is_current(op) {
        return cancelled(sink);
    }

    let status = response.status().as_u16();
    if !(200..300).contains(&status) {
        let body = response.body_mut().read_to_string().unwrap_or_default();
        if !shared.is_current(op) {
            return cancelled(sink);
        }
        let detail = clipped(&body, 300);
        let message = format!(
            "{} request failed (HTTP {status}): {detail}",
            cfg.provider.name
        );
        return sink.done(false, &message);
    }

    let mut accumulated = String::new();
    if request.streamed {
        let mut lines = BufReader::new(response.body_mut().as_reader()).lines();
        loop {
            if !shared.is_current(op) {
                return cancelled(sink);
            }
            let line = match lines.next() {
                Some(Ok(line)) => line,
                Some(Err(err)) => {
                    return if shared.is_current(op) {
                        sink.done(false, &format!("Network error: {err}"));
                    } else {
                        cancelled(sink);
                    };
                }
                None => break,
            };
            let Some(payload) = line.strip_prefix("data:") else {
                continue;
            };
            let payload = payload.trim();
            // [DONE] terminates the stream; don't wait for the server to
            // close the connection.
            if payload == "[DONE]" {
                break;
            }
            let delta = match cfg.provider.style {
                ApiStyle::OpenAiCompatible => parse_openai_delta(payload),
                ApiStyle::Anthropic => parse_anthropic_delta(payload),
            };
            if delta.is_empty() {
                continue;
            }
            // Re-check after parse: a supersede mid-token must not leak UI.
            if !shared.is_current(op) {
                return cancelled(sink);
            }
            accumulated.push_str(&delta);
            if !json_mode {
                sink.token(&delta);
            }
        }
    } else {
        match response.body_mut().read_to_string() {
            Ok(body) => accumulated = parse_openai_message_content(&body),
            Err(err) => {
                return if shared.is_current(op) {
                    sink.done(false, &format!("Network error: {err}"));
                } else {
                    cancelled(sink);
                };
            }
        }
    }

    if !shared.is_current(op) {
        return cancelled(sink);
    }

    // Word mode delivers the whole structured reply as a single token,
    // canonicalized so callers always receive a clean JSON object string.
    let final_text = if json_mode {
        let canonical = extract_json_payload(&accumulated);
        if !canonical.is_empty() {
            if !shared.is_current(op) {
                return cancelled(sink);
            }
            sink.token(&canonical);
        }
        canonical
    } else {
        accumulated
    };

    let translation = if json_mode {
        match WordCard::parse(&final_text) {
            Some(card) if card.is_phrase() => card.translation.clone().unwrap_or_default(),
            Some(card) => card.plain_text(),
            None => String::new(),
        }
    } else {
        final_text
    };

    // History only while still the active op — prevents a just-superseded
    // worker from polluting the store after a newer translate began.
    {
        let mut history = lock(&shared.history);
        if !shared.is_current(op) {
            return cancelled(sink);
        }
        history.add(text, &translation);
    }

    sink.done(true, "");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn open_ai_deltas() {
        let cases: &[(&str, &str)] = &[
            (
                r#"{"choices":[{"delta":{"content":"Hello"},"index":0}]}"#,
                "Hello",
            ),
            (r#"{"choices":[{"delta":{"content":"你好"}}]}"#, "你好"),
            (r#"{"choices":[{"delta":{"content":""}}]}"#, ""),
            (r#"{"choices":[{"delta":{"role":"assistant"}}]}"#, ""),
            (r#"{"choices":[{"delta":{},"finish_reason":"stop"}]}"#, ""),
            (r#"{"choices":[]}"#, ""),
            ("not json {", ""),
            ("", ""),
        ];
        for (payload, expected) in cases {
            assert_eq!(&parse_openai_delta(payload), expected, "payload: {payload}");
        }
    }

    #[test]
    fn anthropic_deltas() {
        let cases: &[(&str, &str)] = &[
            (
                r#"{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}"#,
                "Hello",
            ),
            (
                r#"{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"你好"}}"#,
                "你好",
            ),
            (
                r#"{"type":"message_start","message":{"id":"msg_1","content":[]}}"#,
                "",
            ),
            (
                r#"{"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}"#,
                "",
            ),
            (
                r#"{"type":"message_delta","delta":{"stop_reason":"end_turn"}}"#,
                "",
            ),
            (r#"{"type":"message_stop"}"#, ""),
            (r#"{"type":"ping"}"#, ""),
            (
                r#"{"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"hmm"}}"#,
                "",
            ),
            ("not json {", ""),
            ("", ""),
        ];
        for (payload, expected) in cases {
            assert_eq!(
                &parse_anthropic_delta(payload),
                expected,
                "payload: {payload}"
            );
        }
    }

    #[test]
    fn missing_key_messages() {
        let base = ProviderInfo {
            id: "test".to_string(),
            name: "TestProvider".to_string(),
            style: ApiStyle::OpenAiCompatible,
            json_mode: JsonMode::ResponseFormat,
            base_url: String::new(),
            default_model: "m".to_string(),
            alt_model: String::new(),
            env_var: "TEST_API_KEY".to_string(),
            key_page: String::new(),
            disable_thinking: false,
        };
        assert_eq!(
            missing_key_message(&base),
            "No API key configured for TestProvider. Open Settings to add one, or set the TEST_API_KEY environment variable."
        );
        let no_env = ProviderInfo {
            env_var: String::new(),
            ..base
        };
        assert_eq!(
            missing_key_message(&no_env),
            "No API key configured for TestProvider. Open Settings to add one."
        );
    }

    #[test]
    fn clipped_body() {
        assert_eq!(clipped("  short  ", 300), "short");
        let long = "x".repeat(400);
        let out = clipped(&long, 300);
        assert_eq!(out.chars().count(), 301);
        assert!(out.ends_with('…'));
    }
}

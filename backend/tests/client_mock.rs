//! Integration tests for the streaming client against a hermetic mock
//! server (127.0.0.1, ephemeral port).

mod common;

use common::{MockServer, ANTHROPIC_TOKENS, EXPECT_KEY, TOKENS};
use std::net::TcpListener;
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;
use std::time::Duration;
use translator_backend::{Backend, Sink};

fn temp_dir(tag: &str) -> PathBuf {
    static COUNTER: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "translator-backend-itest-{}-{}-{}",
        tag,
        std::process::id(),
        COUNTER.fetch_add(1, Ordering::SeqCst)
    ));
    let _ = std::fs::remove_dir_all(&dir);
    dir
}

struct Outcome {
    tokens: Vec<String>,
    done: (bool, String),
}

fn collect(rx_tokens: mpsc::Receiver<String>, rx_done: mpsc::Receiver<(bool, String)>) -> Outcome {
    let done = rx_done
        .recv_timeout(Duration::from_secs(15))
        .expect("on_done must fire");
    // The worker sends every token before done (same thread), so the token
    // queue is final once done arrives.
    let tokens = rx_tokens.try_iter().collect();
    Outcome { tokens, done }
}

fn channels() -> (Sink, mpsc::Receiver<String>, mpsc::Receiver<(bool, String)>) {
    let (tx_tokens, rx_tokens) = mpsc::channel::<String>();
    let (tx_done, rx_done) = mpsc::channel::<(bool, String)>();
    let sink = Sink {
        on_token: Box::new(move |token| {
            let _ = tx_tokens.send(token.to_string());
        }),
        on_done: Box::new(move |ok, message| {
            let _ = tx_done.send((ok, message.to_string()));
        }),
    };
    (sink, rx_tokens, rx_done)
}

fn translate(
    backend: &Backend,
    text: &str,
    context: &str,
    target: &str,
    json_mode: bool,
) -> Outcome {
    let (sink, rx_tokens, rx_done) = channels();
    backend.translate(text, context, target, json_mode, sink);
    collect(rx_tokens, rx_done)
}

fn history_entries(backend: &Backend) -> Vec<serde_json::Value> {
    serde_json::from_str(&backend.history_json()).expect("history is a JSON array")
}

#[test]
fn openai_phrase_streams_tokens() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("phrase"));
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    let outcome = translate(
        &backend,
        "Translate this sentence.",
        "",
        "Simplified Chinese",
        false,
    );

    assert_eq!(outcome.done, (true, String::new()));
    assert_eq!(outcome.tokens, TOKENS.map(str::to_string));

    let request = server.last_request();
    assert!(request.path.ends_with("/chat/completions"));
    assert_eq!(request.header("authorization"), Some("Bearer test-key"));
    assert_eq!(request.header("accept"), Some("text/event-stream"));
    let body = request.json();
    assert_eq!(body["model"], "deepseek-v4-flash");
    assert_eq!(body["stream"], true);
    assert_eq!(body["messages"][0]["role"], "system");
    let system = body["messages"][0]["content"].as_str().unwrap();
    assert!(
        system.contains("Simplified Chinese"),
        "prompt targets the language"
    );
    assert!(!system.contains("{target}"));
    assert_eq!(body["messages"][1]["role"], "user");
    // Phrase mode sends the raw text, no "Text:" decoration.
    assert_eq!(body["messages"][1]["content"], "Translate this sentence.");
    // DeepSeek gets thinking disabled even in phrase mode.
    assert_eq!(body["thinking"], serde_json::json!({ "type": "disabled" }));
    assert!(body.get("response_format").is_none());

    let history = history_entries(&backend);
    assert_eq!(history.len(), 1);
    assert_eq!(history[0]["source"], "Translate this sentence.");
    assert_eq!(history[0]["translation"], TOKENS.concat());
    assert!(history[0]["ts"].as_i64().unwrap() > 0);
}

#[test]
fn openai_word_response_format_is_single_shot() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("wordrf"));
    backend.configure("deepseek", EXPECT_KEY, "", &server.url()); // "" model -> provider default

    let outcome = translate(
        &backend,
        "bank",
        "I sat on the bank.",
        "Simplified Chinese",
        true,
    );

    assert_eq!(outcome.done, (true, String::new()));
    assert_eq!(outcome.tokens.len(), 1, "word mode emits exactly one token");
    let card: serde_json::Value = serde_json::from_str(&outcome.tokens[0]).unwrap();
    assert_eq!(card["type"], "word");
    assert_eq!(card["word"], "bank");

    let body = server.last_request().json();
    assert_eq!(
        body["stream"], false,
        "response_format mode is not streamed"
    );
    assert_eq!(
        body["response_format"],
        serde_json::json!({ "type": "json_object" })
    );
    assert_eq!(body["thinking"], serde_json::json!({ "type": "disabled" }));
    assert_eq!(
        body["model"], "deepseek-v4-flash",
        "empty model falls back to provider default"
    );
    let system = body["messages"][0]["content"].as_str().unwrap();
    assert!(system.contains(r#""type": "word""#), "word prompt is used");
    assert_eq!(
        body["messages"][1]["content"], "Text: bank\nSentence: I sat on the bank.",
        "word mode decorates the user content"
    );

    // History stores the card's plain-text rendering.
    let history = history_entries(&backend);
    assert_eq!(history.len(), 1);
    assert_eq!(history[0]["source"], "bank");
    assert_eq!(
        history[0]["translation"],
        "bank /bæŋk/ n.\nthe land next to a river\nBank here means the side of a river.\nWe sat on the bank."
    );
}

#[test]
fn openai_word_phrase_reply_goes_to_history_as_translation() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("wordphrase"));
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    // Contains a space -> mock replies with {"type": "phrase", ...}.
    let outcome = translate(&backend, "ice cream", "", "Simplified Chinese", true);
    assert_eq!(outcome.done, (true, String::new()));
    assert_eq!(outcome.tokens.len(), 1);
    let reply: serde_json::Value = serde_json::from_str(&outcome.tokens[0]).unwrap();
    assert_eq!(reply["type"], "phrase");

    let history = history_entries(&backend);
    assert_eq!(history[0]["translation"], "这是一个模拟翻译。");
}

#[test]
fn openai_word_prompt_only_streams_then_canonicalizes() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("wordpo"));
    backend.configure("gemini", EXPECT_KEY, "gemini-3.5-flash-lite", &server.url());

    let outcome = translate(&backend, "hola", "", "French", true);

    assert_eq!(outcome.done, (true, String::new()));
    // Streamed JSON text is buffered and delivered once, canonicalized
    // (markdown fence stripped).
    assert_eq!(outcome.tokens.len(), 1);
    assert_eq!(
        outcome.tokens[0],
        r#"{"type": "phrase", "translation": "你好"}"#
    );

    let body = server.last_request().json();
    assert_eq!(
        body["stream"], true,
        "prompt_only providers stream word mode"
    );
    assert!(body.get("response_format").is_none());
    assert!(
        body.get("thinking").is_none(),
        "gemini has no thinking toggle"
    );

    let history = history_entries(&backend);
    assert_eq!(history[0]["translation"], "你好");
}

#[test]
fn anthropic_style_streams_text_deltas() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("anthropic"));
    backend.configure("anthropic", EXPECT_KEY, "claude-haiku-4-5", &server.url());

    let outcome = translate(&backend, "Hello world", "", "Spanish", false);

    assert_eq!(outcome.done, (true, String::new()));
    assert_eq!(outcome.tokens, ANTHROPIC_TOKENS.map(str::to_string));

    let request = server.last_request();
    assert!(request.path.ends_with("/v1/messages"));
    assert_eq!(request.header("x-api-key"), Some(EXPECT_KEY));
    assert_eq!(request.header("anthropic-version"), Some("2023-06-01"));
    let body = request.json();
    assert_eq!(body["model"], "claude-haiku-4-5");
    assert_eq!(body["max_tokens"], 4096);
    assert_eq!(body["stream"], true);
    let system = body["system"].as_str().unwrap();
    assert!(system.contains("Spanish"));
    assert_eq!(body["messages"][0]["role"], "user");
    assert_eq!(body["messages"][0]["content"], "Hello world");

    let history = history_entries(&backend);
    assert_eq!(history[0]["translation"], ANTHROPIC_TOKENS.concat());
}

#[test]
fn missing_api_key_message_mentions_env_var() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("nokey"));
    backend.configure("deepseek", "", "deepseek-v4-flash", &server.url());

    let outcome = translate(&backend, "hello", "", "Simplified Chinese", false);

    assert!(!outcome.done.0);
    assert_eq!(
        outcome.done.1,
        "No API key configured for DeepSeek. Open Settings to add one, or set the DEEPSEEK_API_KEY environment variable."
    );
    assert!(outcome.tokens.is_empty());
    assert!(
        server.requests().is_empty(),
        "no request is sent without a key"
    );
    assert!(
        history_entries(&backend).is_empty(),
        "failed calls record no history"
    );
}

#[test]
fn missing_api_key_message_without_env_var() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("nokeycustom"));
    backend.configure("custom", "", "some-model", &server.url());

    let outcome = translate(&backend, "hello", "", "Simplified Chinese", false);

    assert!(!outcome.done.0);
    assert_eq!(
        outcome.done.1,
        "No API key configured for Custom (OpenAI-compatible). Open Settings to add one."
    );
}

#[test]
fn http_error_includes_status_and_body() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("httperr"));
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    let outcome = translate(
        &backend,
        "TRIGGER_HTTP_ERROR",
        "",
        "Simplified Chinese",
        false,
    );

    assert!(!outcome.done.0);
    assert_eq!(
        outcome.done.1,
        r#"DeepSeek request failed (HTTP 429): {"error": "rate limited"}"#
    );
    assert!(history_entries(&backend).is_empty());
}

#[test]
fn http_error_body_is_clipped_to_300_chars() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("longerr"));
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    let outcome = translate(
        &backend,
        "TRIGGER_LONG_ERROR",
        "",
        "Simplified Chinese",
        false,
    );

    assert!(!outcome.done.0);
    let expected = format!("DeepSeek request failed (HTTP 500): {}…", "x".repeat(300));
    assert_eq!(outcome.done.1, expected);
}

#[test]
fn wrong_key_yields_provider_error() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("badkey"));
    backend.configure("deepseek", "wrong-key", "deepseek-v4-flash", &server.url());

    let outcome = translate(&backend, "hello", "", "Simplified Chinese", false);

    assert!(!outcome.done.0);
    assert_eq!(
        outcome.done.1,
        r#"DeepSeek request failed (HTTP 401): {"error":{"message":"unauthorized"}}"#
    );
}

#[test]
fn network_error_on_unreachable_host() {
    // Bind then drop to get an address that refuses connections.
    let addr = TcpListener::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap();
    let backend = Backend::new(temp_dir("neterr"));
    backend.configure(
        "deepseek",
        EXPECT_KEY,
        "deepseek-v4-flash",
        &format!("http://{addr}"),
    );

    let outcome = translate(&backend, "hello", "", "Simplified Chinese", false);

    assert!(!outcome.done.0);
    assert!(
        outcome.done.1.starts_with("Network error: "),
        "got: {}",
        outcome.done.1
    );
}

#[test]
fn cancel_reports_cancelled_and_skips_history() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("cancel"));
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    let (sink, rx_tokens, rx_done) = channels();
    backend.translate("slow text please", "", "Simplified Chinese", false, sink);
    assert_eq!(
        rx_tokens.recv_timeout(Duration::from_secs(5)).as_deref(),
        Ok("first"),
        "first token arrives before cancel"
    );
    backend.cancel();
    let done = rx_done
        .recv_timeout(Duration::from_secs(5))
        .expect("cancelled request still completes with on_done");
    assert_eq!(done, (false, "cancelled".to_string()));
    assert!(
        history_entries(&backend).is_empty(),
        "cancelled calls record no history"
    );

    // The backend stays usable: a follow-up request succeeds.
    let outcome = translate(
        &backend,
        "Translate this sentence.",
        "",
        "Simplified Chinese",
        false,
    );
    assert_eq!(outcome.done, (true, String::new()));
    assert_eq!(outcome.tokens, TOKENS.map(str::to_string));
    let history = history_entries(&backend);
    assert_eq!(history.len(), 1);
}

#[test]
fn new_translate_cancels_in_flight_request() {
    let server = MockServer::start();
    let backend = Backend::new(temp_dir("supersede"));
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    let (slow_sink, slow_rx_tokens, slow_rx_done) = channels();
    backend.translate("slow one", "", "Simplified Chinese", false, slow_sink);
    let _ = slow_rx_tokens.recv_timeout(Duration::from_secs(5));

    // Starting a second request cancels the first; each gets exactly one done.
    let outcome = translate(
        &backend,
        "Translate this sentence.",
        "",
        "Simplified Chinese",
        false,
    );
    assert_eq!(outcome.done, (true, String::new()));

    let slow_done = slow_rx_done
        .recv_timeout(Duration::from_secs(5))
        .expect("superseded request gets its on_done");
    assert_eq!(slow_done, (false, "cancelled".to_string()));
}

#[test]
fn unknown_provider_and_unconfigured_fail_cleanly() {
    let backend = Backend::new(temp_dir("badcfg"));

    let outcome = translate(&backend, "hello", "", "Simplified Chinese", false);
    assert_eq!(outcome.done, (false, "No provider configured".to_string()));

    backend.configure("bogus", EXPECT_KEY, "model", "http://127.0.0.1:1");
    let outcome = translate(&backend, "hello", "", "Simplified Chinese", false);
    assert_eq!(outcome.done, (false, "Unknown provider: bogus".to_string()));
}

#[test]
fn history_clear_empties_store() {
    let server = MockServer::start();
    let dir = temp_dir("histclear");
    let backend = Backend::new(dir.clone());
    backend.configure("deepseek", EXPECT_KEY, "deepseek-v4-flash", &server.url());

    let outcome = translate(
        &backend,
        "Translate this sentence.",
        "",
        "Simplified Chinese",
        false,
    );
    assert!(outcome.done.0);
    assert_eq!(history_entries(&backend).len(), 1);

    backend.history_clear();
    assert_eq!(backend.history_json(), "[]");

    // Persisted: a new backend over the same dir is also empty.
    let reloaded = Backend::new(dir);
    assert_eq!(reloaded.history_json(), "[]");
}

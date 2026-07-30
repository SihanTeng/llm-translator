//! Shared hermetic mock LLM server for integration tests (std::net only).
//! Speaks both wire styles like tests/mock_deepseek_server.py, plus content
//! triggers for error and cancellation scenarios.
#![allow(dead_code)]

use std::io::{BufRead, BufReader, Read, Write};
use std::net::{SocketAddr, TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

pub const EXPECT_KEY: &str = "test-key";

pub const TOKENS: [&str; 8] = [
    "Hello",
    ",",
    " this",
    " is",
    " a",
    " streamed",
    " translation",
    ".",
];
pub const ANTHROPIC_TOKENS: [&str; 4] = ["Hola", " desde", " Anthropic", "."];

pub fn word_card_json() -> String {
    serde_json::json!({
        "type": "word",
        "word": "bank",
        "phonetic": "/bæŋk/",
        "pos": "n.",
        "meaning": "the land next to a river",
        "explanation": "Bank here means the side of a river.",
        "example": "We sat on the bank.",
    })
    .to_string()
}

pub fn phrase_reply_json() -> String {
    serde_json::json!({ "type": "phrase", "translation": "这是一个模拟翻译。" }).to_string()
}

#[derive(Debug, Clone)]
pub struct RecordedRequest {
    pub method: String,
    pub path: String,
    pub headers: Vec<(String, String)>,
    pub body: String,
}

impl RecordedRequest {
    pub fn header(&self, name: &str) -> Option<&str> {
        self.headers
            .iter()
            .find(|(n, _)| n == &name.to_lowercase())
            .map(|(_, v)| v.as_str())
    }

    pub fn json(&self) -> serde_json::Value {
        serde_json::from_str(&self.body).expect("request body is JSON")
    }
}

pub struct MockServer {
    addr: SocketAddr,
    requests: Arc<Mutex<Vec<RecordedRequest>>>,
    shutdown: Arc<AtomicBool>,
    accept: Option<JoinHandle<()>>,
}

impl MockServer {
    pub fn start() -> MockServer {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind mock server");
        listener.set_nonblocking(true).unwrap();
        let addr = listener.local_addr().unwrap();
        let requests: Arc<Mutex<Vec<RecordedRequest>>> = Arc::new(Mutex::new(Vec::new()));
        let shutdown = Arc::new(AtomicBool::new(false));
        let accept = {
            let requests = Arc::clone(&requests);
            let shutdown = Arc::clone(&shutdown);
            std::thread::spawn(move || {
                while !shutdown.load(Ordering::SeqCst) {
                    match listener.accept() {
                        Ok((stream, _)) => {
                            let requests = Arc::clone(&requests);
                            std::thread::spawn(move || handle_connection(stream, requests));
                        }
                        Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                            std::thread::sleep(Duration::from_millis(2));
                        }
                        Err(_) => break,
                    }
                }
            })
        };
        MockServer {
            addr,
            requests,
            shutdown,
            accept: Some(accept),
        }
    }

    pub fn url(&self) -> String {
        format!("http://{}", self.addr)
    }

    pub fn requests(&self) -> Vec<RecordedRequest> {
        self.requests.lock().unwrap().clone()
    }

    pub fn last_request(&self) -> RecordedRequest {
        self.requests
            .lock()
            .unwrap()
            .last()
            .expect("at least one request")
            .clone()
    }
}

impl Drop for MockServer {
    fn drop(&mut self) {
        self.shutdown.store(true, Ordering::SeqCst);
        if let Some(accept) = self.accept.take() {
            let _ = accept.join();
        }
    }
}

fn handle_connection(stream: TcpStream, requests: Arc<Mutex<Vec<RecordedRequest>>>) {
    let _ = stream.set_read_timeout(Some(Duration::from_secs(10)));
    let Ok(request) = read_request(&stream) else {
        return;
    };
    requests.lock().unwrap().push(request.clone());
    let mut writer = stream;
    respond(&mut writer, &request);
}

fn read_request(stream: &TcpStream) -> std::io::Result<RecordedRequest> {
    let mut reader = BufReader::new(stream.try_clone()?);
    let mut request_line = String::new();
    if reader.read_line(&mut request_line)? == 0 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::UnexpectedEof,
            "closed",
        ));
    }
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default().to_string();
    let path = parts.next().unwrap_or_default().to_string();

    let mut headers = Vec::new();
    loop {
        let mut line = String::new();
        reader.read_line(&mut line)?;
        let trimmed = line.trim_end();
        if trimmed.is_empty() {
            break;
        }
        if let Some((name, value)) = trimmed.split_once(':') {
            headers.push((name.trim().to_lowercase(), value.trim().to_string()));
        }
    }

    let content_length: usize = headers
        .iter()
        .find(|(name, _)| name == "content-length")
        .and_then(|(_, value)| value.parse().ok())
        .unwrap_or(0);
    let mut body = vec![0u8; content_length];
    reader.read_exact(&mut body)?;

    Ok(RecordedRequest {
        method,
        path,
        headers,
        body: String::from_utf8_lossy(&body).into_owned(),
    })
}

fn write_json(stream: &mut TcpStream, status: u16, status_text: &str, body: &str) {
    let _ = write!(
        stream,
        "HTTP/1.1 {status} {status_text}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    );
}

fn write_sse_head(stream: &mut TcpStream) {
    let _ = write!(
        stream,
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n"
    );
    let _ = stream.flush();
}

fn respond(stream: &mut TcpStream, request: &RecordedRequest) {
    if request.path.ends_with("/chat/completions") {
        respond_openai(stream, request);
    } else if request.path.ends_with("/v1/messages") {
        respond_anthropic(stream, request);
    } else {
        write_json(stream, 404, "Not Found", "{}");
    }
}

fn respond_openai(stream: &mut TcpStream, request: &RecordedRequest) {
    if request.header("authorization") != Some("Bearer test-key") {
        write_json(
            stream,
            401,
            "Unauthorized",
            r#"{"error":{"message":"unauthorized"}}"#,
        );
        return;
    }
    let body = request.json();
    let messages = body["messages"].as_array().cloned().unwrap_or_default();
    let user = messages
        .last()
        .and_then(|m| m["content"].as_str())
        .unwrap_or_default()
        .to_string();

    if user.contains("TRIGGER_HTTP_ERROR") {
        write_json(
            stream,
            429,
            "Too Many Requests",
            r#"{"error": "rate limited"}"#,
        );
        return;
    }
    if user.contains("TRIGGER_LONG_ERROR") {
        write_json(stream, 500, "Internal Server Error", &"x".repeat(400));
        return;
    }

    if body["stream"].as_bool() == Some(true) {
        write_sse_head(stream);
        if user.starts_with("Text:") {
            // prompt_only word mode: the model streams JSON text (fenced).
            for chunk in [
                "```json\n{\"type\":",
                " \"phrase\", \"transl",
                "ation\": \"你好\"}",
                "\n```",
            ] {
                send_openai_chunk(stream, chunk);
            }
        } else if user.contains("slow") {
            // Cancellation scenario: one token, then a long slow drip.
            send_openai_chunk(stream, "first");
            for _ in 0..120 {
                std::thread::sleep(Duration::from_millis(50));
                if send_openai_chunk_result(stream, "x").is_err() {
                    return; // client went away
                }
            }
        } else {
            for token in TOKENS {
                send_openai_chunk(stream, token);
            }
        }
        let _ = write!(stream, "data: [DONE]\n\n");
        let _ = stream.flush();
    } else {
        let selected = user
            .lines()
            .next()
            .unwrap_or_default()
            .strip_prefix("Text:")
            .unwrap_or(user.lines().next().unwrap_or_default())
            .trim()
            .to_string();
        let reply = if selected.contains(' ') {
            phrase_reply_json()
        } else {
            word_card_json()
        };
        let payload = serde_json::json!({
            "choices": [{
                "message": { "role": "assistant", "content": reply },
                "finish_reason": "stop",
            }],
        })
        .to_string();
        write_json(stream, 200, "OK", &payload);
    }
}

fn send_openai_chunk_result(stream: &mut TcpStream, content: &str) -> std::io::Result<()> {
    let chunk = serde_json::json!({ "choices": [{ "delta": { "content": content }, "index": 0 }] });
    write!(stream, "data: {chunk}\n\n")?;
    stream.flush()
}

fn send_openai_chunk(stream: &mut TcpStream, content: &str) {
    if send_openai_chunk_result(stream, content).is_ok() {
        std::thread::sleep(Duration::from_millis(10));
    }
}

fn respond_anthropic(stream: &mut TcpStream, request: &RecordedRequest) {
    if request.header("x-api-key") != Some(EXPECT_KEY) {
        write_json(
            stream,
            401,
            "Unauthorized",
            r#"{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}"#,
        );
        return;
    }
    write_sse_head(stream);
    let send = |stream: &mut TcpStream, event: &str, data: serde_json::Value| {
        let _ = write!(stream, "event: {event}\ndata: {data}\n\n");
        let _ = stream.flush();
        std::thread::sleep(Duration::from_millis(10));
    };
    send(
        stream,
        "message_start",
        serde_json::json!({"type": "message_start", "message": {"id": "msg_mock", "role": "assistant", "content": []}}),
    );
    send(
        stream,
        "content_block_start",
        serde_json::json!({"type": "content_block_start", "index": 0, "content_block": {"type": "text", "text": ""}}),
    );
    for token in ANTHROPIC_TOKENS {
        send(
            stream,
            "content_block_delta",
            serde_json::json!({"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": token}}),
        );
    }
    send(
        stream,
        "content_block_stop",
        serde_json::json!({"type": "content_block_stop", "index": 0}),
    );
    send(
        stream,
        "message_delta",
        serde_json::json!({"type": "message_delta", "delta": {"stop_reason": "end_turn"}}),
    );
    send(
        stream,
        "message_stop",
        serde_json::json!({"type": "message_stop"}),
    );
}

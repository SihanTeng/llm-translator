//! Smoke tests for the C ABI: a full request through the extern functions,
//! history string ownership, and NULL robustness.

mod common;

use common::{MockServer, EXPECT_KEY, TOKENS};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{mpsc, Mutex};
use std::time::Duration;
use translator_backend::ffi::*;

fn temp_dir(tag: &str) -> PathBuf {
    static COUNTER: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "translator-backend-ffi-{}-{}-{}",
        tag,
        std::process::id(),
        COUNTER.fetch_add(1, Ordering::SeqCst)
    ));
    let _ = std::fs::remove_dir_all(&dir);
    dir
}

struct FfiState {
    tokens: Mutex<Vec<String>>,
    done_tx: mpsc::Sender<(bool, String)>,
}

extern "C" fn on_token(ctx: *mut c_void, utf8: *const c_char, len: usize) {
    let state = unsafe { &*(ctx.cast::<FfiState>()) };
    let bytes = unsafe { std::slice::from_raw_parts(utf8.cast::<u8>(), len) };
    state
        .tokens
        .lock()
        .unwrap()
        .push(String::from_utf8_lossy(bytes).into_owned());
}

extern "C" fn on_done(ctx: *mut c_void, ok: bool, message: *const c_char) {
    let state = unsafe { &*(ctx.cast::<FfiState>()) };
    let message = if message.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(message) }
            .to_string_lossy()
            .into_owned()
    };
    let _ = state.done_tx.send((ok, message));
}

fn c(s: &str) -> CString {
    CString::new(s).unwrap()
}

#[test]
fn full_request_through_c_abi() {
    let server = MockServer::start();
    let dir = temp_dir("roundtrip");

    let backend = unsafe { tb_backend_new(c(dir.to_str().unwrap()).as_ptr()) };
    assert!(!backend.is_null());
    unsafe {
        tb_backend_configure(
            backend,
            c("deepseek").as_ptr(),
            c(EXPECT_KEY).as_ptr(),
            c("").as_ptr(),
            c(&server.url()).as_ptr(),
        );
    }

    let (done_tx, done_rx) = mpsc::channel();
    let state = Box::into_raw(Box::new(FfiState {
        tokens: Mutex::new(Vec::new()),
        done_tx,
    }));
    unsafe {
        tb_backend_translate(
            backend,
            c("Translate this sentence.").as_ptr(),
            c("").as_ptr(),
            c("Simplified Chinese").as_ptr(),
            false,
            Some(on_token),
            Some(on_done),
            state.cast::<c_void>(),
        );
    }

    let done = done_rx
        .recv_timeout(Duration::from_secs(15))
        .expect("on_done fires");
    assert_eq!(done, (true, String::new()));
    let state = unsafe { Box::from_raw(state) };
    assert_eq!(
        *state.tokens.lock().unwrap(),
        TOKENS.map(str::to_string).to_vec()
    );

    // History string is owned by the caller and freed with tb_string_free.
    let json = unsafe { tb_history_json(backend) };
    assert!(!json.is_null());
    let json_str = unsafe { CStr::from_ptr(json) }
        .to_string_lossy()
        .into_owned();
    unsafe { tb_string_free(json) };
    let entries: serde_json::Value = serde_json::from_str(&json_str).unwrap();
    assert_eq!(entries[0]["source"], "Translate this sentence.");
    assert_eq!(entries[0]["translation"], TOKENS.concat());

    unsafe {
        tb_history_clear(backend);
        let json = tb_history_json(backend);
        assert_eq!(CStr::from_ptr(json).to_string_lossy(), "[]");
        tb_string_free(json);
        tb_backend_free(backend);
    }
}

#[test]
fn null_arguments_are_tolerated() {
    unsafe {
        tb_backend_free(std::ptr::null_mut());
        tb_backend_cancel(std::ptr::null_mut());
        tb_history_clear(std::ptr::null_mut());
        tb_backend_configure(
            std::ptr::null_mut(),
            c("deepseek").as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        );
        tb_backend_translate(
            std::ptr::null_mut(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            false,
            None,
            None,
            std::ptr::null_mut(),
        );
        tb_string_free(std::ptr::null_mut());

        let json = tb_history_json(std::ptr::null_mut());
        assert!(!json.is_null());
        assert_eq!(CStr::from_ptr(json).to_string_lossy(), "[]");
        tb_string_free(json);
    }
}

#[test]
fn invalid_utf8_and_null_strings_are_lossy_not_fatal() {
    let dir = temp_dir("lossy");
    let backend = unsafe { tb_backend_new(c(dir.to_str().unwrap()).as_ptr()) };
    // 0xFF is invalid UTF-8; configure must not crash, provider id won't match.
    let bogus: &[u8] = b"bogus-\xFF\0";
    unsafe {
        tb_backend_configure(
            backend,
            bogus.as_ptr().cast::<c_char>(),
            std::ptr::null(), // NULL strings behave like ""
            std::ptr::null(),
            std::ptr::null(),
        );
    }
    let (done_tx, done_rx) = mpsc::channel();
    let state = Box::into_raw(Box::new(FfiState {
        tokens: Mutex::new(Vec::new()),
        done_tx,
    }));
    unsafe {
        tb_backend_translate(
            backend,
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            false,
            Some(on_token),
            Some(on_done),
            state.cast::<c_void>(),
        );
    }
    let (ok, message) = done_rx.recv_timeout(Duration::from_secs(5)).unwrap();
    drop(unsafe { Box::from_raw(state) });
    assert!(!ok);
    assert!(message.starts_with("Unknown provider: "), "got: {message}");
    unsafe { tb_backend_free(backend) };
}

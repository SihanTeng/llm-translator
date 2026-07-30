//! C ABI matching `include/translator_backend.h`. All functions are safe to
//! call from C given the documented pointer validity; callbacks always fire
//! on a backend worker thread.

use crate::client::{Backend, Sink};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::path::PathBuf;

/// Opaque handle: `typedef struct TbBackend TbBackend;` in the header.
pub type TbBackend = Backend;

/// `void (*TbTokenFn)(void *ctx, const char *utf8, size_t len)` — the token
/// is NOT NUL-terminated; `len` bytes are valid at `utf8` during the call.
pub type TbTokenFn = Option<extern "C" fn(*mut c_void, *const c_char, usize)>;
/// `void (*TbDoneFn)(void *ctx, bool ok, const char *message_utf8)`.
pub type TbDoneFn = Option<extern "C" fn(*mut c_void, bool, *const c_char)>;

/// The opaque `void *ctx` travels with the callbacks onto the worker thread.
#[derive(Clone, Copy)]
struct SendCtx(*mut c_void);
unsafe impl Send for SendCtx {}

/// Reads a NUL-terminated C string defensively: NULL becomes "" and invalid
/// UTF-8 is replaced (U+FFFD) rather than rejected.
fn lossy(ptr: *const c_char) -> String {
    if ptr.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned()
}

fn into_raw_cstring(s: &str) -> *mut c_char {
    match CString::new(s) {
        Ok(s) => s.into_raw(),
        // serde_json output never contains NUL, but never return NULL.
        Err(_) => CString::default().into_raw(),
    }
}

/// Creates a backend; `data_dir` is where `history.json` lives (created if
/// missing).
///
/// # Safety
/// `data_dir_utf8` must be NULL or point to a valid NUL-terminated string.
/// The returned pointer must be freed exactly once with `tb_backend_free`.
#[no_mangle]
pub unsafe extern "C" fn tb_backend_new(data_dir_utf8: *const c_char) -> *mut TbBackend {
    Box::into_raw(Box::new(Backend::new(PathBuf::from(lossy(data_dir_utf8)))))
}

/// # Safety
/// `backend` must be a pointer from `tb_backend_new` (or NULL), not already
/// freed.
#[no_mangle]
pub unsafe extern "C" fn tb_backend_free(backend: *mut TbBackend) {
    if !backend.is_null() {
        drop(unsafe { Box::from_raw(backend) });
    }
}

/// Selects the provider (an id from `spec/providers.json`), API key, model
/// ("" for the provider default) and base URL override ("" for the provider
/// default).
///
/// # Safety
/// `backend` must be a live pointer from `tb_backend_new`; the string
/// arguments must be NULL or valid NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn tb_backend_configure(
    backend: *mut TbBackend,
    provider_id: *const c_char,
    api_key: *const c_char,
    model: *const c_char,
    base_url_override: *const c_char,
) {
    let Some(backend) = (unsafe { backend.as_ref() }) else {
        return;
    };
    backend.configure(
        &lossy(provider_id),
        &lossy(api_key),
        &lossy(model),
        &lossy(base_url_override),
    );
}

/// Starts a request on a worker thread, cancelling any in-flight one. Fires
/// exactly one `on_done` for this call; `ctx` must stay valid until then.
///
/// # Safety
/// `backend` must be a live pointer from `tb_backend_new`; the string
/// arguments must be NULL or valid NUL-terminated strings; the callbacks and
/// `ctx` must remain callable/valid until `on_done` runs.
#[no_mangle]
pub unsafe extern "C" fn tb_backend_translate(
    backend: *mut TbBackend,
    text: *const c_char,
    context: *const c_char,
    target_language_name: *const c_char,
    json_mode: bool,
    on_token: TbTokenFn,
    on_done: TbDoneFn,
    ctx: *mut c_void,
) {
    let Some(backend) = (unsafe { backend.as_ref() }) else {
        return;
    };
    let ctx = SendCtx(ctx);
    let sink = Sink {
        on_token: Box::new(move |token: &str| {
            // Bind the whole wrapper: with edition-2021 disjoint captures the
            // field access below would otherwise capture the raw pointer
            // (not Send) instead of SendCtx.
            let ctx = ctx;
            if let Some(on_token) = on_token {
                on_token(ctx.0, token.as_ptr().cast::<c_char>(), token.len());
            }
        }),
        on_done: Box::new(move |ok: bool, message: &str| {
            let ctx = ctx;
            if let Some(on_done) = on_done {
                let message = CString::new(message).unwrap_or_default();
                on_done(ctx.0, ok, message.as_ptr());
            }
        }),
    };
    backend.translate(
        &lossy(text),
        &lossy(context),
        &lossy(target_language_name),
        json_mode,
        sink,
    );
}

/// # Safety
/// `backend` must be a live pointer from `tb_backend_new`.
#[no_mangle]
pub unsafe extern "C" fn tb_backend_cancel(backend: *mut TbBackend) {
    if let Some(backend) = unsafe { backend.as_ref() } {
        backend.cancel();
    }
}

/// History as a JSON string (`[{"ts":..,"source":..,"translation":..}]`,
/// newest first, max 500). Never NULL ("[]" when empty).
///
/// # Safety
/// `backend` must be a live pointer from `tb_backend_new`. The returned
/// string must be freed with `tb_string_free`.
#[no_mangle]
pub unsafe extern "C" fn tb_history_json(backend: *mut TbBackend) -> *mut c_char {
    let json = match unsafe { backend.as_ref() } {
        Some(backend) => backend.history_json(),
        None => "[]".to_string(),
    };
    into_raw_cstring(&json)
}

/// # Safety
/// `backend` must be a live pointer from `tb_backend_new`.
#[no_mangle]
pub unsafe extern "C" fn tb_history_clear(backend: *mut TbBackend) {
    if let Some(backend) = unsafe { backend.as_ref() } {
        backend.history_clear();
    }
}

/// # Safety
/// `s` must be NULL or a pointer returned by `tb_history_json`, not already
/// freed.
#[no_mangle]
pub unsafe extern "C" fn tb_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(unsafe { CString::from_raw(s) });
    }
}

// C ABI of the translator backend (Rust crate in backend/).
// Hand-maintained contract — keep in sync with backend/src/ffi.rs.
//
// Threading: all callbacks fire on a backend worker thread. Consumers with
// an event loop (Qt) must marshal to their main thread themselves.
// Every tb_backend_translate() call guarantees exactly one on_done call
// (ok=false with message "cancelled" when cancelled), which marks the end
// of ctx's required lifetime.
#ifndef TRANSLATOR_BACKEND_H
#define TRANSLATOR_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TbBackend TbBackend;

// One streamed token (UTF-8, NOT NUL-terminated).
typedef void (*TbTokenFn)(void *ctx, const char *utf8, size_t len);
// Terminal event: ok=true -> finished (message is ""); ok=false -> error
// (message describes it; "cancelled" after tb_backend_cancel).
typedef void (*TbDoneFn)(void *ctx, bool ok, const char *message_utf8);

// data_dir: where history.json lives (created if missing).
TbBackend *tb_backend_new(const char *data_dir_utf8);
void tb_backend_free(TbBackend *backend);

// provider_id: an id from spec/providers.json. base_url_override: "" for
// the provider default.
void tb_backend_configure(TbBackend *backend, const char *provider_id, const char *api_key,
    const char *model, const char *base_url_override);

// Starts a request on a worker thread, cancelling any in-flight one via an
// op-id bump (superseded workers report cancelled and never write history).
//   text: raw selected text; context: containing sentence or "".
//   target_language_name: English name ("Simplified Chinese").
//   json_mode: short-selection mode (dictionary card / phrase JSON).
// ctx must stay valid until on_done runs.
void tb_backend_translate(TbBackend *backend, const char *text, const char *context,
    const char *target_language_name, bool json_mode, TbTokenFn on_token, TbDoneFn on_done,
    void *ctx);
// Invalidates the active op; the worker reports on_done(false, "cancelled").
void tb_backend_cancel(TbBackend *backend);

// History as a JSON string ([{"ts":..,"source":..,"translation":..}], newest
// first, max 500). Caller frees with tb_string_free. Never NULL ("[]").
char *tb_history_json(TbBackend *backend);
void tb_history_clear(TbBackend *backend);
void tb_string_free(char *s);

#ifdef __cplusplus
}
#endif

#endif // TRANSLATOR_BACKEND_H

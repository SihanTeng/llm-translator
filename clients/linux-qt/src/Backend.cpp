#include "Backend.h"

#include "translator_backend.h"

#include <QMetaObject>
#include <QPointer>

namespace {
// Heap-allocated per request; freed by the on_done path (the Rust contract
// guarantees exactly one on_done per tb_backend_translate call).
struct CallbackContext {
    QPointer<Backend> backend;
    quint64 generation = 0;
    std::shared_ptr<std::atomic_bool> alive;
};

// Both callbacks fire on a Rust worker thread: marshal to the GUI thread.
// Queued invocations from one thread to the same receiver are ordered, so
// every token lambda runs before the done lambda. The alive flag is checked
// before touching the QPointer (Backend's destructor sets it first), and the
// generation check inside the queued lambda drops events from superseded or
// cancelled requests — the Rust cancel only flags the worker, so stale
// callbacks do arrive and must not reach the new request's state.
void onToken(void *ctx, const char *utf8, size_t len) {
    auto *context = static_cast<CallbackContext *>(ctx);
    if (!context->alive->load())
        return;
    const QPointer<Backend> backend = context->backend;
    if (!backend)
        return;
    const QString delta = QString::fromUtf8(utf8, static_cast<qsizetype>(len));
    QMetaObject::invokeMethod(
        backend,
        [context, delta] {
            Backend *b = context->backend;
            if (b && context->generation == b->currentGeneration())
                emit b->tokenReceived(delta);
        },
        Qt::QueuedConnection);
}

void onDone(void *ctx, bool ok, const char *messageUtf8) {
    auto *context = static_cast<CallbackContext *>(ctx);
    const QString message = QString::fromUtf8(messageUtf8 ? messageUtf8 : "");
    const bool dispatch = context->alive->load() && context->backend;
    if (dispatch) {
        QMetaObject::invokeMethod(
            context->backend,
            [context, ok, message] {
                Backend *b = context->backend;
                if (b && context->generation == b->currentGeneration()) {
                    if (ok)
                        emit b->requestFinished();
                    // "cancelled" mirrors the old silent cancel(): no signal.
                    else if (message != QStringLiteral("cancelled"))
                        emit b->errorOccurred(message);
                }
                delete context;
            },
            Qt::QueuedConnection);
    } else {
        delete context;
    }
}

QByteArray utf8(const QString &s) {
    return s.toUtf8();
}
} // namespace

Backend::Backend(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_impl(tb_backend_new(utf8(dataDir).constData()))
    , m_alive(std::make_shared<std::atomic_bool>(true)) { }

Backend::~Backend() {
    m_alive->store(false);
    if (m_impl)
        tb_backend_free(m_impl);
}

void Backend::configure(const ProviderInfo &provider, const QString &apiKey, const QString &model,
    const QString &baseUrlOverride) {
    tb_backend_configure(m_impl, utf8(provider.id).constData(), utf8(apiKey).constData(),
        utf8(model).constData(), utf8(baseUrlOverride).constData());
}

void Backend::translate(
    const QString &text, const QString &context, const QString &targetLanguageName, bool jsonMode) {
    ++m_generation;
    auto *callbackContext = new CallbackContext { this, m_generation, m_alive };
    tb_backend_translate(m_impl, utf8(text).constData(), utf8(context).constData(),
        utf8(targetLanguageName).constData(), jsonMode, &onToken, &onDone, callbackContext);
}

void Backend::cancel() {
    // Bumps the generation so events still in flight from the cancelled
    // request are dropped at delivery; the Rust side stops the worker.
    ++m_generation;
    tb_backend_cancel(m_impl);
}

QString Backend::historyJson() {
    char *json = tb_history_json(m_impl);
    if (!json)
        return QStringLiteral("[]");
    const QString result = QString::fromUtf8(json);
    tb_string_free(json);
    return result;
}

void Backend::historyClear() {
    tb_history_clear(m_impl);
}

#include "Backend.h"

#include "translator_backend.h"

#include <QMetaObject>
#include <QPointer>

namespace {
// Heap-allocated per request; freed by the on_done path (the Rust contract
// guarantees exactly one on_done per tb_backend_translate call).
struct CallbackContext {
    QPointer<Backend> backend;
};

// Both callbacks fire on a Rust worker thread: marshal to the GUI thread.
// Queued invocations from one thread to the same receiver are ordered, so
// every token lambda runs before the done lambda.
void onToken(void *ctx, const char *utf8, size_t len) {
    auto *context = static_cast<CallbackContext *>(ctx);
    if (!context->backend)
        return;
    const QString delta = QString::fromUtf8(utf8, static_cast<qsizetype>(len));
    QMetaObject::invokeMethod(
        context->backend,
        [context, delta] {
            if (context->backend)
                emit context->backend->tokenReceived(delta);
        },
        Qt::QueuedConnection);
}

void onDone(void *ctx, bool ok, const char *messageUtf8) {
    auto *context = static_cast<CallbackContext *>(ctx);
    const QString message = QString::fromUtf8(messageUtf8 ? messageUtf8 : "");
    if (context->backend) {
        QMetaObject::invokeMethod(
            context->backend,
            [context, ok, message] {
                if (context->backend) {
                    if (ok)
                        emit context->backend->requestFinished();
                    // "cancelled" mirrors the old silent cancel(): no signal.
                    else if (message != QStringLiteral("cancelled"))
                        emit context->backend->errorOccurred(message);
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
    , m_impl(tb_backend_new(utf8(dataDir).constData())) { }

Backend::~Backend() {
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
    auto *callbackContext = new CallbackContext { this };
    tb_backend_translate(m_impl, utf8(text).constData(), utf8(context).constData(),
        utf8(targetLanguageName).constData(), jsonMode, &onToken, &onDone, callbackContext);
}

void Backend::cancel() {
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

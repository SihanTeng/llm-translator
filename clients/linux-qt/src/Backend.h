#pragma once

#include "Provider.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

struct TbBackend;

// Qt wrapper over the Rust backend (C ABI, backend/include/
// translator_backend.h). Signals deliberately mirror the retired C++
// LlmClient so AppController's flow is unchanged. The Rust side owns
// providers, prompts, streaming, and the history file.
class Backend : public QObject {
    Q_OBJECT

public:
    // dataDir: where the Rust side writes history.json
    // (QStandardPaths::AppDataLocation from main.cpp).
    explicit Backend(const QString &dataDir, QObject *parent = nullptr);
    ~Backend() override;

    void configure(const ProviderInfo &provider, const QString &apiKey, const QString &model,
        const QString &baseUrlOverride = { });

    // Starts a request, cancelling any in-flight one. Prompts and request
    // decoration ("Text:"/"Sentence:") are built by the Rust side.
    void translate(const QString &text, const QString &context, const QString &targetLanguageName,
        bool jsonMode);
    void cancel();

    // History is recorded by the Rust side on every successful request.
    [[nodiscard]] QString historyJson();
    void historyClear();

    // GUI thread only: events from older requests must be dropped (the
    // Rust cancel is asynchronous, so stale callbacks do arrive).
    [[nodiscard]] quint64 currentGeneration() const { return m_generation; }

signals:
    void tokenReceived(const QString &delta);
    void requestFinished();
    void errorOccurred(const QString &message);

private:
    TbBackend *m_impl = nullptr;
    quint64 m_generation = 0;
    // Set false at the start of ~Backend so worker-thread callbacks stop
    // before the QPointer they also read can be destroyed (the Rust side
    // holds no QObject state, so this is the only cross-thread lifetime).
    std::shared_ptr<std::atomic_bool> m_alive;
};

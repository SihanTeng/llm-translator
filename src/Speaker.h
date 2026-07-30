#pragma once

#include <QObject>

class QProcess;

// Text-to-speech via speech-dispatcher's spd-say CLI (installed by default
// on GNOME/Fedora desktops). Used instead of QtTextToSpeech because that
// module is not part of the bundled Qt SDK; a subprocess keeps the build
// dependency-free and degrades gracefully when spd-say is absent.
class Speaker : public QObject {
    Q_OBJECT

public:
    explicit Speaker(QObject *parent = nullptr);

    // True when spd-say is on PATH.
    static bool isAvailable();

    // Speaks the text, interrupting anything still playing. No-op when the
    // text is empty or spd-say is unavailable.
    void speak(const QString &text);
    void stop();

private:
    QProcess *m_process = nullptr;
};

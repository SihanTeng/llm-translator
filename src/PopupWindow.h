#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QTextBrowser;
class QToolButton;

// Frameless, always-on-top popup shown next to the text selection.
// Displays the streamed translation as it arrives.
class PopupWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PopupWindow(QWidget *parent = nullptr);

    void startTranslation(const QString &sourceText, const QPoint &globalPos);
    void appendToken(const QString &delta);
    void showError(const QString &message);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    void placeNear(const QPoint &globalPos);

    QLabel *m_sourceLabel;
    QTextBrowser *m_resultView;
    QToolButton *m_copyButton;
    QToolButton *m_closeButton;
    QString m_result;
};

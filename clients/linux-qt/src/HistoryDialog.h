#pragma once

#include <QDialog>

class Backend;
class QLineEdit;
class QListWidget;
class QPushButton;

// Modal viewer for the translation history (recorded by the Rust backend
// in history.json): a filterable list of "source -> translation" pairs
// with copy and clear actions.
class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistoryDialog(Backend *backend, QWidget *parent = nullptr);

private:
    void rebuild();
    void copySelected();

    Backend *m_backend;
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QPushButton *m_copyButton;
    int m_totalEntries = 0; // unfiltered count, for the Clear confirmation
};

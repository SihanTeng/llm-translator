#pragma once

#include <QDialog>

class HistoryStore;
class QLineEdit;
class QListWidget;
class QPushButton;

// Modal viewer for the persisted translation history: a filterable list of
// "source -> translation" pairs with copy and clear actions.
class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistoryDialog(HistoryStore *store, QWidget *parent = nullptr);

private:
    void rebuild();
    void copySelected();

    HistoryStore *m_store;
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QPushButton *m_copyButton;
};

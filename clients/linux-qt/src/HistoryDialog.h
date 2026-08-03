#pragma once

#include <QDialog>

class Backend;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QShowEvent;

// Modal viewer for the translation history (recorded by the Rust backend
// in history.json): a filterable index of source → translation pairs with
// copy and clear actions.
//
// Hallmark · genre: modern-minimal · macrostructure: Index-First
// theme: Cobalt (palette-mapped) · tone: utilitarian · designed-as: app-dialog
// pre-emit critique: P4 H5 E4 S4 R5 V4
class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistoryDialog(Backend *backend, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void copySelected();
    void updateChrome();
    void placeEmptyLabel();

    Backend *m_backend;
    QLabel *m_titleLabel;
    QLabel *m_metaLabel;
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QLabel *m_emptyLabel;
    QPushButton *m_copyButton;
    QPushButton *m_clearButton;
    int m_totalEntries = 0; // unfiltered count, for the Clear confirmation
    int m_visibleEntries = 0;
};

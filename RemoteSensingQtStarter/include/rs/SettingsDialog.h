#pragma once

#include <QDialog>
#include <QLabel>

class QComboBox;
class QTabWidget;
class QTextBrowser;

namespace rs {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void applyLanguage();
    void retranslateUi();

private:
    QTabWidget *tabs_{nullptr};
    QComboBox *languageCombo_{nullptr};
    QLabel *languageLabel_{nullptr};
    QTextBrowser *helpBrowser_{nullptr};
};

} // namespace rs

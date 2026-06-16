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
    void applySettings();
    void retranslateUi();

private:
    QTabWidget *tabs_{nullptr};
    QComboBox *languageCombo_{nullptr};
    QComboBox *themeCombo_{nullptr};
    QLabel *languageLabel_{nullptr};
    QLabel *themeLabel_{nullptr};
    QTextBrowser *helpBrowser_{nullptr};
};

} // namespace rs

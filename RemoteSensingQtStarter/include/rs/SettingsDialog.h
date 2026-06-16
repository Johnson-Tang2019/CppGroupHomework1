#pragma once

#include <QDialog>
#include <QLabel>

class QComboBox;
class QDateEdit;
class QLineEdit;
class QPushButton;
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
    void chooseAvatar();

private:
    void loadProfile();
    void updateAvatarPreview(const QString &path);

    QTabWidget *tabs_{nullptr};
    QComboBox *languageCombo_{nullptr};
    QComboBox *themeCombo_{nullptr};
    QLabel *languageLabel_{nullptr};
    QLabel *themeLabel_{nullptr};
    QTextBrowser *helpBrowser_{nullptr};
    QLabel *avatarPreview_{nullptr};
    QPushButton *avatarButton_{nullptr};
    QLabel *nicknameLabel_{nullptr};
    QLabel *birthdayLabel_{nullptr};
    QLabel *addressLabel_{nullptr};
    QLabel *emailLabel_{nullptr};
    QLabel *avatarLabel_{nullptr};
    QLineEdit *nicknameEdit_{nullptr};
    QDateEdit *birthdayEdit_{nullptr};
    QLineEdit *addressEdit_{nullptr};
    QLineEdit *emailEdit_{nullptr};
    QString avatarPath_;
};

} // namespace rs

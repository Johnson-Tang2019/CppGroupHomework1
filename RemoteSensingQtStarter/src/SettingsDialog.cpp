#include "rs/SettingsDialog.h"
#include "rs/AppTheme.h"
#include "rs/Translation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace rs {

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setModal(true);
    resize(640, 520);

    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);

    auto *generalPage = new QWidget(this);
    auto *generalLayout = new QVBoxLayout(generalPage);
    auto *form = new QFormLayout;
    languageLabel_ = new QLabel(generalPage);
    languageCombo_ = new QComboBox(generalPage);
    languageCombo_->addItem(QString(), static_cast<int>(AppLanguage::Chinese));
    languageCombo_->addItem(QString(), static_cast<int>(AppLanguage::English));
    languageCombo_->addItem(QString(), static_cast<int>(AppLanguage::Russian));
    languageCombo_->addItem(QString(), static_cast<int>(AppLanguage::French));
    languageCombo_->addItem(QString(), static_cast<int>(AppLanguage::ClassicalChinese));
    form->addRow(languageLabel_, languageCombo_);

    themeLabel_ = new QLabel(generalPage);
    themeCombo_ = new QComboBox(generalPage);
    for (const AppThemeId themeId : AppTheme::instance().availableThemes()) {
        themeCombo_->addItem(QString(), static_cast<int>(themeId));
    }
    form->addRow(themeLabel_, themeCombo_);

    careModeCheck_ = new QCheckBox(generalPage);
    form->addRow(careModeCheck_);

    generalLayout->addLayout(form);
    generalLayout->addStretch();
    tabs_->addTab(generalPage, QString());

    auto *profilePage = new QWidget(this);
    auto *profileLayout = new QVBoxLayout(profilePage);
    auto *profileForm = new QFormLayout;

    avatarLabel_ = new QLabel(profilePage);
    auto *avatarBox = new QWidget(profilePage);
    auto *avatarLayout = new QHBoxLayout(avatarBox);
    avatarLayout->setContentsMargins(0, 0, 0, 0);
    avatarPreview_ = new QLabel(avatarBox);
    avatarPreview_->setFixedSize(96, 96);
    avatarPreview_->setAlignment(Qt::AlignCenter);
    avatarPreview_->setStyleSheet(QStringLiteral("border:1px solid #c8c8c8; background:#ffffff;"));
    avatarButton_ = new QPushButton(avatarBox);
    avatarLayout->addWidget(avatarPreview_);
    avatarLayout->addWidget(avatarButton_);
    avatarLayout->addStretch(1);
    profileForm->addRow(avatarLabel_, avatarBox);

    nicknameLabel_ = new QLabel(profilePage);
    nicknameEdit_ = new QLineEdit(profilePage);
    profileForm->addRow(nicknameLabel_, nicknameEdit_);

    birthdayLabel_ = new QLabel(profilePage);
    birthdayEdit_ = new QDateEdit(profilePage);
    birthdayEdit_->setCalendarPopup(true);
    birthdayEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    birthdayEdit_->setMinimumDate(QDate(1900, 1, 1));
    birthdayEdit_->setMaximumDate(QDate::currentDate());
    profileForm->addRow(birthdayLabel_, birthdayEdit_);

    addressLabel_ = new QLabel(profilePage);
    addressEdit_ = new QLineEdit(profilePage);
    profileForm->addRow(addressLabel_, addressEdit_);

    emailLabel_ = new QLabel(profilePage);
    emailEdit_ = new QLineEdit(profilePage);
    profileForm->addRow(emailLabel_, emailEdit_);

    profileLayout->addLayout(profileForm);
    profileLayout->addStretch();
    tabs_->addTab(profilePage, QString());

    helpBrowser_ = new QTextBrowser(this);
    helpBrowser_->setOpenExternalLinks(true);
    tabs_->addTab(helpBrowser_, QString());

    layout->addWidget(tabs_, 1);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(&Translation::instance(), &Translation::languageChanged, this, &SettingsDialog::retranslateUi);
    connect(avatarButton_, &QPushButton::clicked, this, &SettingsDialog::chooseAvatar);

    languageCombo_->setCurrentIndex(
        languageCombo_->findData(static_cast<int>(Translation::instance().language())));
    themeCombo_->setCurrentIndex(
        themeCombo_->findData(static_cast<int>(AppTheme::instance().theme())));
    careModeCheck_->setChecked(AppTheme::instance().careMode());
    loadProfile();
    retranslateUi();
}

void SettingsDialog::applySettings() {
    Translation::instance().setLanguage(
        static_cast<AppLanguage>(languageCombo_->currentData().toInt()));
    AppTheme::instance().setTheme(static_cast<AppThemeId>(themeCombo_->currentData().toInt()));
    AppTheme::instance().setCareMode(careModeCheck_->isChecked());

    QSettings settings;
    settings.beginGroup(QStringLiteral("profile"));
    settings.setValue(QStringLiteral("nickname"), nicknameEdit_->text().trimmed());
    settings.setValue(QStringLiteral("avatarPath"), avatarPath_);
    settings.setValue(QStringLiteral("birthday"), birthdayEdit_->date());
    settings.setValue(QStringLiteral("address"), addressEdit_->text().trimmed());
    settings.setValue(QStringLiteral("email"), emailEdit_->text().trimmed());
    settings.endGroup();
    settings.sync();
}

void SettingsDialog::loadProfile() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("profile"));
    nicknameEdit_->setText(settings.value(QStringLiteral("nickname")).toString());
    avatarPath_ = settings.value(QStringLiteral("avatarPath")).toString();
    birthdayEdit_->setDate(settings.value(QStringLiteral("birthday"), QDate(2000, 1, 1)).toDate());
    addressEdit_->setText(settings.value(QStringLiteral("address")).toString());
    emailEdit_->setText(settings.value(QStringLiteral("email")).toString());
    settings.endGroup();
    updateAvatarPreview(avatarPath_);
}

void SettingsDialog::chooseAvatar() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("\u9009\u62e9\u5934\u50cf"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QPixmap preview(path);
    if (preview.isNull()) {
        return;
    }

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString targetPath = dataDir + QStringLiteral("/profile_avatar.png");
    preview.toImage().save(targetPath, "PNG");
    avatarPath_ = targetPath;
    updateAvatarPreview(avatarPath_);
}

void SettingsDialog::updateAvatarPreview(const QString &path) {
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        avatarPreview_->setPixmap(QPixmap());
        avatarPreview_->setText(QStringLiteral("No Avatar"));
        return;
    }

    avatarPreview_->setText(QString());
    avatarPreview_->setPixmap(pixmap.scaled(92, 92, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SettingsDialog::retranslateUi() {
    auto &t = Translation::instance();
    setWindowTitle(t.tr(QStringLiteral("settings.title")));
    languageLabel_->setText(t.tr(QStringLiteral("settings.language")));
    languageCombo_->setItemText(0, t.tr(QStringLiteral("settings.lang.zh")));
    languageCombo_->setItemText(1, t.tr(QStringLiteral("settings.lang.en")));
    languageCombo_->setItemText(2, t.tr(QStringLiteral("settings.lang.ru")));
    languageCombo_->setItemText(3, t.tr(QStringLiteral("settings.lang.fr")));
    languageCombo_->setItemText(4, t.tr(QStringLiteral("settings.lang.gu")));
    themeLabel_->setText(t.tr(QStringLiteral("settings.theme")));
    for (int i = 0; i < themeCombo_->count(); ++i) {
        const auto themeId = static_cast<AppThemeId>(themeCombo_->itemData(i).toInt());
        themeCombo_->setItemText(i, t.tr(AppTheme::instance().themeKey(themeId)));
    }
    careModeCheck_->setText(t.tr(QStringLiteral("settings.care_mode")));
    tabs_->setTabText(0, t.tr(QStringLiteral("settings.tab.general")));
    tabs_->setTabText(1, QStringLiteral("个人资料"));
    tabs_->setTabText(2, t.tr(QStringLiteral("settings.tab.guide")));
    avatarLabel_->setText(QStringLiteral("头像"));
    avatarButton_->setText(QStringLiteral("上传头像"));
    nicknameLabel_->setText(QStringLiteral("昵称"));
    birthdayLabel_->setText(QStringLiteral("生日"));
    addressLabel_->setText(QStringLiteral("地址"));
    emailLabel_->setText(QStringLiteral("邮箱"));
    helpBrowser_->setHtml(t.helpGuideHtml());
}

} // namespace rs

#include "rs/SettingsDialog.h"
#include "rs/AppTheme.h"
#include "rs/Translation.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
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
    form->addRow(languageLabel_, languageCombo_);

    themeLabel_ = new QLabel(generalPage);
    themeCombo_ = new QComboBox(generalPage);
    for (const AppThemeId themeId : AppTheme::instance().availableThemes()) {
        themeCombo_->addItem(QString(), static_cast<int>(themeId));
    }
    form->addRow(themeLabel_, themeCombo_);

    generalLayout->addLayout(form);
    generalLayout->addStretch();
    tabs_->addTab(generalPage, QString());

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

    languageCombo_->setCurrentIndex(
        languageCombo_->findData(static_cast<int>(Translation::instance().language())));
    themeCombo_->setCurrentIndex(
        themeCombo_->findData(static_cast<int>(AppTheme::instance().theme())));
    retranslateUi();
}

void SettingsDialog::applySettings() {
    Translation::instance().setLanguage(
        static_cast<AppLanguage>(languageCombo_->currentData().toInt()));
    AppTheme::instance().setTheme(static_cast<AppThemeId>(themeCombo_->currentData().toInt()));
}

void SettingsDialog::retranslateUi() {
    auto &t = Translation::instance();
    setWindowTitle(t.tr(QStringLiteral("settings.title")));
    languageLabel_->setText(t.tr(QStringLiteral("settings.language")));
    languageCombo_->setItemText(0, t.tr(QStringLiteral("settings.lang.zh")));
    languageCombo_->setItemText(1, t.tr(QStringLiteral("settings.lang.en")));
    themeLabel_->setText(t.tr(QStringLiteral("settings.theme")));
    for (int i = 0; i < themeCombo_->count(); ++i) {
        const auto themeId = static_cast<AppThemeId>(themeCombo_->itemData(i).toInt());
        themeCombo_->setItemText(i, t.tr(AppTheme::instance().themeKey(themeId)));
    }
    tabs_->setTabText(0, t.tr(QStringLiteral("settings.tab.general")));
    tabs_->setTabText(1, t.tr(QStringLiteral("settings.tab.guide")));
    helpBrowser_->setHtml(t.helpGuideHtml());
}

} // namespace rs

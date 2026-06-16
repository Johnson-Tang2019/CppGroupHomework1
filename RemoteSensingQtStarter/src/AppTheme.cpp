#include "rs/AppTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QSettings>

#include <QtMath>

namespace rs {

namespace {

int scaledPx(int basePx, bool careMode) {
    return careMode ? qRound(basePx * 1.28) : basePx;
}

struct ThemeColors {
    QString windowBg;
    QString panelBg;
    QString tabBg;
    QString viewBg;
    QString text;
    QString textMuted;
    QString textAccent;
    QString logText;
    QString menuBarBg;
    QString menuHover;
    QString border;
    QString borderLight;
    QString accent;
    QString accentSoft;
    QString accentLight;
    QString selectedBg;
    QString statusBarBg;
    QString altRow;
};

ThemeColors paletteFor(AppThemeId theme) {
    switch (theme) {
    case AppThemeId::LightBlue:
        return {QStringLiteral("#f0f8ff"), QStringLiteral("#f8fcff"), QStringLiteral("#eef6fc"),
                QStringLiteral("#e3f0fa"), QStringLiteral("#2a4050"), QStringLiteral("#5a6a7a"),
                QStringLiteral("#4a6880"), QStringLiteral("#2e6da4"), QStringLiteral("#ffffff"),
                QStringLiteral("#e3f0fa"), QStringLiteral("#b8d4ea"), QStringLiteral("#d0e4f0"),
                QStringLiteral("#5b9bd5"), QStringLiteral("#a8cce8"), QStringLiteral("#c8dff0"),
                QStringLiteral("#7eb8da"), QStringLiteral("#f5faff"), QStringLiteral("#eef6fc")};
    case AppThemeId::LightGreen:
        return {QStringLiteral("#f2faf4"), QStringLiteral("#f8fcf9"), QStringLiteral("#eef8f2"),
                QStringLiteral("#e8f4ec"), QStringLiteral("#2a4030"), QStringLiteral("#5a6a5a"),
                QStringLiteral("#4a7058"), QStringLiteral("#3d8b5a"), QStringLiteral("#ffffff"),
                QStringLiteral("#e0f2e6"), QStringLiteral("#b8dcc4"), QStringLiteral("#d0e8d8"),
                QStringLiteral("#5cb87a"), QStringLiteral("#a8d8b8"), QStringLiteral("#c8e8d4"),
                QStringLiteral("#7ec49a"), QStringLiteral("#f5faf7"), QStringLiteral("#eef8f2")};
    case AppThemeId::Lavender:
        return {QStringLiteral("#f8f5fc"), QStringLiteral("#fcfaff"), QStringLiteral("#f5f0fa"),
                QStringLiteral("#efe8f8"), QStringLiteral("#3a3050"), QStringLiteral("#6a5a7a"),
                QStringLiteral("#5a4870"), QStringLiteral("#7b5eb8"), QStringLiteral("#ffffff"),
                QStringLiteral("#ece6f8"), QStringLiteral("#d4c8e8"), QStringLiteral("#e4d8f0"),
                QStringLiteral("#9b7ed9"), QStringLiteral("#c4b8e8"), QStringLiteral("#d8ccf0"),
                QStringLiteral("#b8a0d8"), QStringLiteral("#faf8fc"), QStringLiteral("#f3effa")};
    case AppThemeId::WarmSand:
        return {QStringLiteral("#faf8f2"), QStringLiteral("#fffcf8"), QStringLiteral("#f8f4ec"),
                QStringLiteral("#f5ece0"), QStringLiteral("#4a4030"), QStringLiteral("#7a6a5a"),
                QStringLiteral("#6a5848"), QStringLiteral("#b87840"), QStringLiteral("#ffffff"),
                QStringLiteral("#f5ece0"), QStringLiteral("#e0d0b8"), QStringLiteral("#ece0d0"),
                QStringLiteral("#d4a574"), QStringLiteral("#e8ccaa"), QStringLiteral("#ecd8c0"),
                QStringLiteral("#d4b890"), QStringLiteral("#faf6f0"), QStringLiteral("#f8f4ec")};
    case AppThemeId::Mint:
        return {QStringLiteral("#f0faf8"), QStringLiteral("#f8fcfb"), QStringLiteral("#eef8f6"),
                QStringLiteral("#e0f5f0"), QStringLiteral("#2a4540"), QStringLiteral("#5a6a68"),
                QStringLiteral("#4a6860"), QStringLiteral("#3a9888"), QStringLiteral("#ffffff"),
                QStringLiteral("#dff5f0"), QStringLiteral("#b8ddd8"), QStringLiteral("#d0e8e4"),
                QStringLiteral("#5ab8aa"), QStringLiteral("#a0d8d0"), QStringLiteral("#c0e8e0"),
                QStringLiteral("#7ecfc4"), QStringLiteral("#f5fafa"), QStringLiteral("#eefaf8")};
    case AppThemeId::Pink:
    default:
        return {QStringLiteral("#fff0f5"), QStringLiteral("#fffafa"), QStringLiteral("#fdf0f4"),
                QStringLiteral("#ffe4e9"), QStringLiteral("#4a2030"), QStringLiteral("#5a4a4a"),
                QStringLiteral("#8b5a6a"), QStringLiteral("#c71585"), QStringLiteral("#ffffff"),
                QStringLiteral("#fce4ec"), QStringLiteral("#f4b8c8"), QStringLiteral("#e8d0d8"),
                QStringLiteral("#ff69b4"), QStringLiteral("#ffb6c1"), QStringLiteral("#f4b8c8"),
                QStringLiteral("#f4b8c8"), QStringLiteral("#fff5f8"), QStringLiteral("#fdf6f0")};
    }
}

QString buildApplicationStyle(const ThemeColors &c, bool careMode) {
    const int widgetFont = scaledPx(10, careMode);
    const int tabFont = scaledPx(11, careMode);
    const int logFont = scaledPx(12, careMode);
    const int btnPadV = careMode ? 8 : 6;
    const int btnPadH = careMode ? 18 : 16;
    return QStringLiteral(R"(
        QWidget { background-color: %1; color: %2; font-size: %13px; }
        QMainWindow { background-color: %1; }
        QMenuBar { background-color: %3; color: %2; font-size: %13px; }
        QMenuBar::item:selected { background-color: %4; color: %2; }
        QMenu { background-color: %1; color: %2; border: 1px solid %5; font-size: %13px; }
        QMenu::item:selected { background-color: %6; color: #ffffff; }
        QMenu::separator { height: 1px; background: %5; margin: 4px 8px; }
        QTreeWidget { background-color: %7; color: %2; border: none; outline: none; font-size: %13px; }
        QTreeWidget::item:selected { background-color: %6; color: #ffffff; }
        QTreeWidget::item:hover { background-color: %4; }
        QHeaderView::section { background-color: %8; color: %2; border: none; padding: 4px 8px; font-size: %13px; }
        QTextEdit { background-color: %1; color: %9; font-family: Consolas; font-size: %14px; border-top: 2px solid %6; }
        QTabWidget::pane { background-color: %1; border: 1px solid %5; }
        QTabBar::tab { background: %8; color: %2; padding: %15px %16px; border: 1px solid %5; font-size: %17px; }
        QTabBar::tab:selected { background: %1; color: %9; border-top: 2px solid %6; }
        QTabBar::tab:hover:!selected { background: %4; }
        QGraphicsView { background-color: %10; border: none; }
        QSplitter::handle { background-color: %5; margin: 0 3px; }
        QSplitter::handle:hover { background-color: %6; }
        QStatusBar { background-color: %11; color: %12; border-top: 1px solid %5; font-weight: normal; font-size: %13px; }
        QStatusBar QLabel { color: %12; background: transparent; }
        QDialog { background-color: %1; }
        QLabel { color: %2; background: transparent; font-size: %13px; }
        QPushButton {
            background-color: %8; color: %2; border: 1px solid %6; border-radius: 4px;
            padding: %18px %19px; min-width: 80px; font-size: %13px;
        }
        QPushButton:hover { background-color: %6; color: #ffffff; }
        QPushButton:pressed { background-color: %6; color: #ffffff; }
        QPushButton:default { background-color: %6; color: #ffffff; border-color: %6; }
        QPushButton:disabled { background-color: %4; color: %12; border-color: %5; }
        QLineEdit {
            background-color: %7; color: %2; border: 1px solid %5; border-radius: 3px; padding: 4px 8px;
            font-size: %13px;
        }
        QLineEdit:focus { border-color: %6; }
        QComboBox {
            background-color: %7; color: %2; border: 1px solid %5; border-radius: 3px; padding: 4px 8px;
            font-size: %13px;
        }
        QComboBox:hover { border-color: %6; }
        QComboBox QAbstractItemView {
            background-color: %1; color: %2; selection-background-color: %6;
            selection-color: #ffffff; border: 1px solid %5; font-size: %13px;
        }
        QSpinBox, QDoubleSpinBox {
            background-color: %7; color: %2; border: 1px solid %5; border-radius: 3px; padding: 4px 6px;
            font-size: %13px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus { border-color: %6; }
        QScrollBar:vertical { background: %1; width: 12px; border: none; }
        QScrollBar::handle:vertical { background: %8; min-height: 30px; border-radius: 6px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: %6; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: %1; height: 12px; border: none; }
        QScrollBar::handle:horizontal { background: %8; min-width: 30px; border-radius: 6px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: %6; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QToolTip { background-color: %1; color: %2; border: 1px solid %6; padding: 4px; font-size: %13px; }
        QCheckBox { font-size: %13px; spacing: 8px; }
    )")
        .arg(c.windowBg, c.text, c.accentSoft, c.menuHover, c.borderLight, c.accent, c.panelBg, c.accentSoft,
             c.logText, c.viewBg, c.statusBarBg, c.textMuted)
        .arg(widgetFont)
        .arg(logFont)
        .arg(careMode ? 10 : 8)
        .arg(careMode ? 22 : 20)
        .arg(tabFont)
        .arg(btnPadV)
        .arg(btnPadH);
}

QString buildMainWindowStyle(const ThemeColors &c, bool careMode) {
    const int menuFont = scaledPx(13, careMode);
    const int tabFont = scaledPx(12, careMode);
    const int treeFont = scaledPx(13, careMode);
    const int logFont = scaledPx(12, careMode);
    const int menuPadV = careMode ? 8 : 6;
    const int menuPadH = careMode ? 18 : 16;
    const int tabPadV = careMode ? 10 : 8;
    const int tabPadH = careMode ? 24 : 20;
    return QStringLiteral(R"(
        QMainWindow { background-color: %1; }
        QMenuBar {
            background-color: %2; color: %3; font-size: %13px; padding: 2px 0;
            border-bottom: 2px solid %4;
        }
        QMenuBar::item { padding: %14px %15px; background: transparent; }
        QMenuBar::item:selected { background-color: %5; border-radius: 4px; color: %6; }
        QWidget#menuWrap { background-color: %2; border-bottom: 2px solid %4; }
        QPushButton#settingsButton {
            background: transparent; color: %3; border: none; border-radius: 4px;
            padding: %14px %15px; font-size: %13px; font-weight: normal;
        }
        QPushButton#settingsButton:hover { background-color: %5; color: %6; }
        QPushButton#settingsButton:pressed { background-color: %5; color: %6; }
        QMenu { background-color: %2; color: %3; border: 1px solid %7; padding: 4px; font-size: %13px; }
        QMenu::item { padding: %14px 24px; border-radius: 3px; }
        QMenu::item:selected { background-color: %5; color: %6; }
        QMenu::separator { height: 1px; background: %7; margin: 4px 8px; }
        QTabWidget::pane { border: 1px solid %7; border-top: 2px solid %4; background-color: %2; }
        QTabBar::tab {
            background-color: %8; color: %9; padding: %16px %17px; border: 1px solid %7;
            border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px;
            margin-right: 2px; font-size: %18px;
        }
        QTabBar::tab:selected { background-color: %2; color: %3; border-bottom: 2px solid %4; font-weight: bold; }
        QTabBar::tab:hover:!selected { background-color: %5; color: %3; }
        QTreeWidget {
            background-color: %10; border: 1px solid %7; border-radius: 4px;
            font-size: %19px; color: %3; alternate-background-color: %1;
        }
        QTreeWidget::item { padding: 4px 0; border-bottom: 1px solid %8; }
        QTreeWidget::item:selected { background-color: %4; color: #ffffff; }
        QTreeWidget::item:hover { background-color: %5; }
        QTextEdit {
            background-color: %10; color: %3; font-family: "Consolas", "Courier New", monospace;
            font-size: %20px; border: 1px solid %7; border-radius: 4px; padding: 4px;
        }
        QSplitter::handle { background-color: %7; width: 3px; }
        QSplitter::handle:hover { background-color: %4; }
        QScrollBar:vertical { background-color: %1; width: 10px; border-radius: 5px; }
        QScrollBar::handle:vertical { background-color: %7; min-height: 20px; border-radius: 5px; }
        QScrollBar::handle:vertical:hover { background-color: %4; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QSplitter { padding: 4px; }
        QStatusBar { font-size: %13px; }
        QStatusBar QLabel { font-size: %13px; }
    )")
        .arg(c.altRow, c.menuBarBg, c.textMuted, c.accentLight, c.menuHover, c.textAccent, c.borderLight, c.tabBg,
             c.textAccent, c.panelBg)
        .arg(menuFont)
        .arg(menuPadV)
        .arg(menuPadH)
        .arg(tabPadV)
        .arg(tabPadH)
        .arg(tabFont)
        .arg(treeFont)
        .arg(logFont);
}

QString storageKeyFor(AppThemeId theme) {
    switch (theme) {
    case AppThemeId::LightBlue:
        return QStringLiteral("light_blue");
    case AppThemeId::LightGreen:
        return QStringLiteral("light_green");
    case AppThemeId::Lavender:
        return QStringLiteral("lavender");
    case AppThemeId::WarmSand:
        return QStringLiteral("warm_sand");
    case AppThemeId::Mint:
        return QStringLiteral("mint");
    case AppThemeId::Pink:
    default:
        return QStringLiteral("pink");
    }
}

AppThemeId themeFromStorageKey(const QString &key) {
    if (key == QStringLiteral("light_blue")) {
        return AppThemeId::LightBlue;
    }
    if (key == QStringLiteral("light_green")) {
        return AppThemeId::LightGreen;
    }
    if (key == QStringLiteral("lavender")) {
        return AppThemeId::Lavender;
    }
    if (key == QStringLiteral("warm_sand")) {
        return AppThemeId::WarmSand;
    }
    if (key == QStringLiteral("mint")) {
        return AppThemeId::Mint;
    }
    return AppThemeId::Pink;
}

} // namespace

AppTheme &AppTheme::instance() {
    static AppTheme self;
    return self;
}

AppTheme::AppTheme(QObject *parent) : QObject(parent) {}

AppThemeId AppTheme::theme() const {
    return theme_;
}

void AppTheme::loadSavedTheme() {
    QSettings settings;
    theme_ = themeFromStorageKey(settings.value(QStringLiteral("ui/theme"), QStringLiteral("pink")).toString());
    careMode_ = settings.value(QStringLiteral("ui/careMode"), false).toBool();
}

bool AppTheme::careMode() const {
    return careMode_;
}

void AppTheme::setCareMode(bool enabled) {
    if (careMode_ == enabled) {
        return;
    }
    careMode_ = enabled;
    saveCareMode();
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        applyToApplication(app);
    }
    emit themeChanged();
}

void AppTheme::saveCareMode() const {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/careMode"), careMode_);
}

void AppTheme::setTheme(AppThemeId theme) {
    if (theme_ == theme) {
        return;
    }
    theme_ = theme;
    saveTheme();
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        applyToApplication(app);
    }
    emit themeChanged();
}

void AppTheme::saveTheme() const {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"), storageKeyFor(theme_));
}

void AppTheme::applyToApplication(QApplication *app) const {
    if (!app) {
        return;
    }

    QFont font = app->font();
    font.setPointSize(careMode_ ? 12 : 9);
    app->setFont(font);
    app->setStyleSheet(applicationStyleSheet());
}

QString AppTheme::applicationStyleSheet() const {
    return buildApplicationStyle(paletteFor(theme_), careMode_);
}

QString AppTheme::mainWindowStyleSheet() const {
    return buildMainWindowStyle(paletteFor(theme_), careMode_);
}

QVector<AppThemeId> AppTheme::availableThemes() const {
    return {AppThemeId::Pink,      AppThemeId::LightBlue, AppThemeId::LightGreen,
            AppThemeId::Lavender,  AppThemeId::WarmSand,  AppThemeId::Mint};
}

QString AppTheme::themeKey(AppThemeId theme) const {
    switch (theme) {
    case AppThemeId::LightBlue:
        return QStringLiteral("theme.light_blue");
    case AppThemeId::LightGreen:
        return QStringLiteral("theme.light_green");
    case AppThemeId::Lavender:
        return QStringLiteral("theme.lavender");
    case AppThemeId::WarmSand:
        return QStringLiteral("theme.warm_sand");
    case AppThemeId::Mint:
        return QStringLiteral("theme.mint");
    case AppThemeId::Pink:
    default:
        return QStringLiteral("theme.pink");
    }
}

} // namespace rs

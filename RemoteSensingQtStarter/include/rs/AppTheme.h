#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QApplication;

namespace rs {

enum class AppThemeId { Pink, LightBlue, LightGreen, Lavender, WarmSand, Mint };

class AppTheme final : public QObject {
    Q_OBJECT

public:
    static AppTheme &instance();

    AppThemeId theme() const;
    void loadSavedTheme();
    void setTheme(AppThemeId theme);
    void applyToApplication(QApplication *app) const;

    QString applicationStyleSheet() const;
    QString mainWindowStyleSheet() const;

    QVector<AppThemeId> availableThemes() const;
    QString themeKey(AppThemeId theme) const;

signals:
    void themeChanged();

private:
    explicit AppTheme(QObject *parent = nullptr);
    void saveTheme() const;

    AppThemeId theme_{AppThemeId::Pink};
};

} // namespace rs

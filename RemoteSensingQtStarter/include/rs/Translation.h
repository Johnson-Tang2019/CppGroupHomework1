#pragma once

#include <QObject>
#include <QString>

namespace rs {

enum class AppLanguage { Chinese, English, Russian, French, ClassicalChinese };

class Translation final : public QObject {
    Q_OBJECT

public:
    static Translation &instance();

    AppLanguage language() const;
    void loadSavedLanguage();
    void setLanguage(AppLanguage language);

    QString tr(const QString &key) const;
    QString helpGuideHtml() const;

signals:
    void languageChanged();

private:
    explicit Translation(QObject *parent = nullptr);
    void saveLanguage() const;

    AppLanguage language_{AppLanguage::Chinese};
};

} // namespace rs

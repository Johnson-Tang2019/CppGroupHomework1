#pragma once

#include <QString>
#include <QVector>

namespace rs {

enum class AppLanguage;

struct TeamMember {
    QString github;
    QString name;
    QString roleKey;
};

class AppInfo final {
public:
    static QString version();
    static QString versionLabel();
    static QString brandName();
    static QString repositoryUrl();

    static const QVector<TeamMember> &teamMembers();
    static QString splashVersionLine();
    static QString splashTeamLine();
    static QString aboutHtml(AppLanguage language);
};

} // namespace rs

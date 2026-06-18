#include "rs/AppInfo.h"
#include "rs/Translation.h"

#include <QtGlobal>

namespace rs {

namespace {

QString roleText(const TeamMember &m, AppLanguage language) {
    struct Entry {
        const char *lead;
        const char *core;
        const char *algo3d;
        const char *uiux;
    };
    static const Entry en{ "Project Lead", "Core Developer", "Algorithms & 3D", "UI & UX" };
    static const Entry zh{ "项目负责人", "核心开发", "算法与三维", "界面与体验" };
    static const Entry ru{ "Руководитель", "Разработчик", "Алгоритмы и 3D", "UI/UX" };
    static const Entry fr{ "Chef de projet", "Développeur", "Algorithmes & 3D", "UI/UX" };
    static const Entry gu{ "项目掌理", "核心开发", "算法三维", "界面体验" };

    const Entry *e = &zh;
    switch (language) {
    case AppLanguage::English:
        e = &en;
        break;
    case AppLanguage::Russian:
        e = &ru;
        break;
    case AppLanguage::French:
        e = &fr;
        break;
    case AppLanguage::ClassicalChinese:
        e = &gu;
        break;
    case AppLanguage::Chinese:
    default:
        break;
    }

    if (m.roleKey == QStringLiteral("role.core")) {
        return QString::fromUtf8(e->core);
    }
    if (m.roleKey == QStringLiteral("role.algo3d")) {
        return QString::fromUtf8(e->algo3d);
    }
    if (m.roleKey == QStringLiteral("role.uiux")) {
        return QString::fromUtf8(e->uiux);
    }
    return QString::fromUtf8(e->lead);
}

QString memberLine(const TeamMember &m, AppLanguage language) {
    return QStringLiteral("<li><b>%1</b> <span style='color:#888'>(%2)</span> — %3<br/>"
                          "<a href='https://github.com/%1'>github.com/%1</a></li>")
        .arg(m.github, m.name, roleText(m, language));
}

QString qtVersionLine() {
    return QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion()));
}

} // namespace

QString AppInfo::version() {
    return QStringLiteral("1.0.4");
}

QString AppInfo::versionLabel() {
    return QStringLiteral("v") + version();
}

QString AppInfo::brandName() {
    return QStringLiteral("CodeFour");
}

QString AppInfo::repositoryUrl() {
    return QStringLiteral("https://github.com/Johnson-Tang2019/CppGroupHomework1");
}

const QVector<TeamMember> &AppInfo::teamMembers() {
    static const QVector<TeamMember> team = {
        {QStringLiteral("Johnson-Tang2019"), QStringLiteral("汤骏"),
         QStringLiteral("role.lead")},
        {QStringLiteral("wangchablis-sys"), QStringLiteral("王宇凡"),
         QStringLiteral("role.core")},
        {QStringLiteral("Austin9633"), QStringLiteral("梁邵臣"),
         QStringLiteral("role.algo3d")},
        {QStringLiteral("LiamSmith4399xyx"), QStringLiteral("王昕竹"),
         QStringLiteral("role.uiux")},
    };
    return team;
}

QString AppInfo::splashVersionLine() {
    return versionLabel() + QStringLiteral(" · ") + brandName();
}

QString AppInfo::splashTeamLine() {
    QStringList names;
    for (const auto &m : teamMembers()) {
        names << m.name;
    }
    return names.join(QStringLiteral(" / "));
}

QString AppInfo::aboutHtml(AppLanguage language) {
    QStringList members;
    for (const auto &m : teamMembers()) {
        members << memberLine(m, language);
    }

    const QString repo = repositoryUrl();
    const QString qtLine = qtVersionLine();

    if (language == AppLanguage::English) {
        return QStringLiteral(
                   R"(<div style='text-align:center;margin-bottom:12px'>
<h2 style='margin:0;color:#7a3f57'>Remote Sensing Image Platform</h2>
<p style='margin:6px 0;color:#b06a84;font-style:italic'>See the Earth clearly — process rasters gracefully.</p>
</div>
<hr/>
<p><b>Version</b> %1 &nbsp;·&nbsp; <b>Team</b> %2</p>
<p>A desktop remote-sensing workstation built for coursework and exploration: multi-band rasters, point clouds, 3D mesh, DEM workflows, indices, and an AI vision assistant.</p>
<h3 style='color:#7a3f57'>Developers</h3>
<ul style='line-height:1.6'>%3</ul>
<h3 style='color:#7a3f57'>Built With</h3>
<ul>
<li>%4, GDAL, OpenCV, OpenGL</li>
<li>Cross-platform Qt 6 desktop UI with i18n (中文 / EN / RU / FR / Classical Chinese)</li>
</ul>
<h3 style='color:#7a3f57'>Links</h3>
<p><a href='%5'>%5</a></p>
<p style='color:#999;font-size:12px;margin-top:16px'>© 2025–2026 %2. Made with curiosity for remote sensing.</p>)")
            .arg(version(), brandName(), members.join(QString()), qtLine, repo);
    }

    if (language == AppLanguage::Russian) {
        return QStringLiteral(
                   R"(<div style='text-align:center;margin-bottom:12px'>
<h2 style='margin:0;color:#7a3f57'>Remote Sensing Qt Starter</h2>
<p style='margin:6px 0;color:#b06a84'>Платформа обработки данных дистанционного зондирования</p>
</div>
<hr/>
<p><b>Версия</b> %1 &nbsp;·&nbsp; <b>Команда</b> %2</p>
<h3>Разработчики</h3>
<ul>%3</ul>
<p><b>Стек:</b> %4, GDAL, OpenCV</p>
<p><a href='%5'>%5</a></p>
<p style='color:#999;font-size:12px'>© 2025–2026 %2</p>)")
            .arg(version(), brandName(), members.join(QString()), qtLine, repo);
    }

    if (language == AppLanguage::French) {
        return QStringLiteral(
                   R"(<div style='text-align:center;margin-bottom:12px'>
<h2 style='margin:0;color:#7a3f57'>Remote Sensing Qt Starter</h2>
<p style='margin:6px 0;color:#b06a84'>Plateforme de télédétection et de traitement d'images</p>
</div>
<hr/>
<p><b>Version</b> %1 &nbsp;·&nbsp; <b>Équipe</b> %2</p>
<h3>Développeurs</h3>
<ul>%3</ul>
<p><b>Stack :</b> %4, GDAL, OpenCV</p>
<p><a href='%5'>%5</a></p>
<p style='color:#999;font-size:12px'>© 2025–2026 %2</p>)")
            .arg(version(), brandName(), members.join(QString()), qtLine, repo);
    }

    if (language == AppLanguage::ClassicalChinese) {
        return QStringLiteral(
                   R"(<div style='text-align:center;margin-bottom:12px'>
<h2 style='margin:0;color:#7a3f57'>遥感像图处置平台</h2>
<p style='margin:6px 0;color:#b06a84;font-style:italic'>览卫星之图，处置若赏画。</p>
</div>
<hr/>
<p><b>版本</b> %1 &nbsp;·&nbsp; <b>出品</b> %2</p>
<h3 style='color:#7a3f57'>开发诸君</h3>
<ul style='line-height:1.6'>%3</ul>
<p><b>所用</b> %4、GDAL、OpenCV、OpenGL</p>
<p><a href='%5'>%5</a></p>
<p style='color:#999;font-size:12px'>© 2025–2026 %2</p>)")
            .arg(version(), brandName(), members.join(QString()), qtLine, repo);
    }

    return QStringLiteral(
               R"(<div style='text-align:center;margin-bottom:12px'>
<h2 style='margin:0;color:#7a3f57'>遥感影像处理平台</h2>
<p style='margin:6px 0;color:#b06a84;font-style:italic'>览卫星之图，处理如赏画——让遥感数据触手可及。</p>
</div>
<hr/>
<p><b>版本</b> %1 &nbsp;·&nbsp; <b>出品团队</b> %2</p>
<p>面向课程实践与自主探索的桌面级遥感工作站：支持多波段栅格、点云、三维 Mesh、DEM 流程、遥感指数计算，以及可附带截图的 AI 视觉助手。</p>
<h3 style='color:#7a3f57'>开发团队</h3>
<ul style='line-height:1.6'>%3</ul>
<h3 style='color:#7a3f57'>技术栈</h3>
<ul>
<li>%4、GDAL、OpenCV、OpenGL</li>
<li>Qt 6 跨平台界面，支持中文 / English / Русский / Français / 古语</li>
<li>会话自动恢复、滑动对比、360° 全景、颐养模式等体验优化</li>
</ul>
<h3 style='color:#7a3f57'>开源与链接</h3>
<p>项目托管于 GitHub：<a href='%5'>%5</a></p>
<p style='color:#999;font-size:12px;margin-top:16px'>© 2025–2026 %2 团队。感谢 GDAL、OpenCV、Qt 开源社区。</p>)")
        .arg(version(), brandName(), members.join(QString()), qtLine, repo);
}

} // namespace rs

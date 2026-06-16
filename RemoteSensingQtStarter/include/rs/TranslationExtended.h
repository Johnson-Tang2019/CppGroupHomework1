#pragma once

#include <QHash>
#include <QString>

namespace rs {

const QHash<QString, QString> &russianCatalog();
const QHash<QString, QString> &frenchCatalog();
const QHash<QString, QString> &classicalChineseCatalog();
QString russianHelpGuideHtml();
QString frenchHelpGuideHtml();
QString classicalChineseHelpGuideHtml();

} // namespace rs

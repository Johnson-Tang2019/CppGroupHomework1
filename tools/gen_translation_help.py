import pathlib
import help_guides as hg

ROOT = pathlib.Path(__file__).resolve().parents[1]

def fn(name, html):
    return f"QString {name}() {{\n    return QStringLiteral(R\"({html})\");\n}}\n"

body = '#include "rs/TranslationHelp.h"\n\nnamespace rs {\n\n'
for name, html in [
    ("chineseHelpGuideHtml", hg.HELP_ZH),
    ("englishHelpGuideHtml", hg.HELP_EN),
    ("russianHelpGuideHtml", hg.HELP_RU),
    ("frenchHelpGuideHtml", hg.HELP_FR),
    ("classicalChineseHelpGuideHtml", hg.HELP_GU),
]:
    body += fn(name, html) + "\n"
body += "} // namespace rs\n"

out = ROOT / "RemoteSensingQtStarter/src/TranslationHelp.cpp"
out.write_text(body, encoding="utf-8")
print("Wrote", out, "bytes", out.stat().st_size)
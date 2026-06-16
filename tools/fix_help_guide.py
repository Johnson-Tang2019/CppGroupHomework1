import pathlib

p = pathlib.Path(__file__).resolve().parents[1] / "RemoteSensingQtStarter/src/Translation.cpp"
text = p.read_text(encoding="utf-8")
start = text.index("    if (language_ == AppLanguage::Japanese) {")
end = text.index("    return QStringLiteral(R\"(\n<h2>遥感影像处理平台")
replacement = """    if (language_ == AppLanguage::Russian) {
        return russianHelpGuideHtml();
    }

    if (language_ == AppLanguage::French) {
        return frenchHelpGuideHtml();
    }

"""
text = text[:start] + replacement + text[end:]
p.write_text(text, encoding="utf-8")
print("done")

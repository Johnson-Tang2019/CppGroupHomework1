import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from help_guides import HELP_EN, HELP_GU, HELP_RU, HELP_FR, HELP_ZH

ROOT = pathlib.Path(__file__).resolve().parents[1]

def cpp_raw(html: str) -> str:
    return 'QStringLiteral(R"(' + html + ')")'

# Patch Translation.cpp
cpp = ROOT / "RemoteSensingQtStarter/src/Translation.cpp"
text = cpp.read_text(encoding="utf-8")
a = text.find("QString Translation::helpGuideHtml() const {")
b = text.find("\n} // namespace rs", a)
body = text[a:b]

en_old_start = body.find("if (language_ == AppLanguage::English)")
en_old_end = body.find("if (language_ == AppLanguage::Russian)")
zh_old_start = body.rfind('return QStringLiteral(R"(\n<h2>遥感')

new_body = (
    body[:en_old_start]
    + "if (language_ == AppLanguage::English) {\n        return "
    + cpp_raw(HELP_EN)
    + ";\n    }\n\n    "
    + body[en_old_end:zh_old_start]
    + "return "
    + cpp_raw(HELP_ZH)
    + ";\n"
)

cpp.write_text(text[:a] + new_body + text[b:], encoding="utf-8")
print("Translation.cpp OK")

# Patch gen_extended_i18n.py HELP blocks
gen = ROOT / "tools/gen_extended_i18n.py"
gtext = gen.read_text(encoding="utf-8")
ru_s = gtext.index("HELP_RU = ")
ru_e = gtext.index("\n\nHELP_FR = ", ru_s)
fr_s = gtext.index("HELP_FR = ", ru_e)
fr_e = gtext.index("\n\n\ndef main", fr_s)
gtext = gtext[:ru_s] + f'HELP_RU = """{HELP_RU}""".strip()\n\n' + gtext[fr_s:fr_e] + f'HELP_FR = """{HELP_FR}""".strip()\n\n' + gtext[fr_e:]
gen.write_text(gtext, encoding="utf-8")
print("gen_extended_i18n.py OK")

# Patch classical_gu.py
gu = ROOT / "tools/classical_gu.py"
gut = gu.read_text(encoding="utf-8")
gs = gut.index("HELP_GU = ")
ge = gut.index('\n\n', gs)
gu.write_text(gut[:gs] + f'HELP_GU = """{HELP_GU}""".strip()' + gut[ge:], encoding="utf-8")
print("classical_gu.py OK")

import gen_extended_i18n
gen_extended_i18n.main()
print("TranslationExtended.cpp regenerated")

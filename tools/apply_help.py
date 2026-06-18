"""Apply help_guides.py content to Translation.cpp and regenerate TranslationExtended.cpp."""
from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
from help_guides import HELP_EN, HELP_GU, HELP_RU, HELP_FR, HELP_ZH  # noqa: E402


def cpp_raw_string(html: str) -> str:
    """Wrap HTML in C++ raw string literal R\"(...)\"."""
    return 'QStringLiteral(R"(' + html + ')")'


def patch_translation_cpp() -> None:
    path = ROOT / "RemoteSensingQtStarter/src/Translation.cpp"
    text = path.read_text(encoding="utf-8")

    en_start = text.index('    if (language_ == AppLanguage::English) {\n        return QStringLiteral(R"(')
    en_end = text.index(')");\n    }\n\n    if (language_ == AppLanguage::Russian)')
    zh_start = text.index('    return QStringLiteral(R"(\n<h2>遥感影像处理平台')
    zh_end = text.rindex(')");\n}\n\n} // namespace rs')

    en_block = (
        "    if (language_ == AppLanguage::English) {\n        return "
        + cpp_raw_string(HELP_EN)
        + ";\n    }"
    )
    zh_block = "    return " + cpp_raw_string(HELP_ZH) + ";"

    text = text[:en_start] + en_block + text[en_end:zh_start] + zh_block + text[zh_end + len(')";\n') :]
    path.write_text(text, encoding="utf-8")
    print(f"Patched {path}")


def patch_gen_extended() -> None:
    path = ROOT / "tools/gen_extended_i18n.py"
    text = path.read_text(encoding="utf-8")
    text = re.sub(
        r"from classical_gu import GU, HELP_GU",
        "from classical_gu import GU\nfrom help_guides import HELP_FR as HELP_FR_GUIDE, HELP_RU as HELP_RU_GUIDE, HELP_GU as HELP_GU_GUIDE",
        text,
        count=1,
    )
    text = re.sub(
        r"HELP_RU = textwrap\.dedent\(\"\"\".*?\"\"\"\)\.strip\(\)",
        f'HELP_RU = """{HELP_RU}""".strip()',
        text,
        count=1,
        flags=re.DOTALL,
    )
    text = re.sub(
        r"HELP_FR = textwrap\.dedent\(\"\"\".*?\"\"\"\)\.strip\(\)",
        f'HELP_FR = """{HELP_FR}""".strip()',
        text,
        count=1,
        flags=re.DOTALL,
    )
    # Update classical_gu.py HELP_GU
    gu_path = ROOT / "tools/classical_gu.py"
    gu_text = gu_path.read_text(encoding="utf-8")
    gu_text = re.sub(
        r'HELP_GU = """.*?""".strip\(\)',
        f'HELP_GU = """\n{HELP_GU}\n""".strip()',
        gu_text,
        count=1,
        flags=re.DOTALL,
    )
    gu_path.write_text(gu_text, encoding="utf-8")

    # Replace HELP_RU/HELP_FR in gen file directly
    ru_marker = "HELP_RU = "
    fr_marker = "HELP_FR = "
    ru_start = text.index(ru_marker)
    ru_end = text.index("\n\nHELP_FR = ", ru_start)
    fr_start = text.index(fr_marker)
    fr_end = text.index("\n\n\ndef main", fr_start)

    text = (
        text[:ru_start]
        + f'HELP_RU = """\n{HELP_RU}\n""".strip()'
        + text[ru_end:fr_start]
        + f'HELP_FR = """\n{HELP_FR}\n""".strip()'
        + text[fr_end:]
    )
    path.write_text(text, encoding="utf-8")
    print(f"Patched {path} and classical_gu.py")


if __name__ == "__main__":
    patch_translation_cpp()
    patch_gen_extended()
    import gen_extended_i18n

    gen_extended_i18n.main()

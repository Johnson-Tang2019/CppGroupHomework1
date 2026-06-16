import pathlib

p = pathlib.Path(__file__).resolve().parents[1] / "RemoteSensingQtStarter/src/Translation.cpp"
text = p.read_text(encoding="utf-8")
start = text.index("const QHash<QString, QString> &japaneseCatalog()")
end = text.index("const QHash<QString, QHash<QString, QString>> &catalog()")
new = text[:start] + text[end:]
if '#include "rs/TranslationExtended.h"' not in new:
    new = new.replace(
        '#include "rs/Translation.h"\n\n#include <QHash>',
        '#include "rs/Translation.h"\n#include "rs/TranslationExtended.h"\n\n#include <QHash>',
    )
p.write_text(new, encoding="utf-8")
print("done")

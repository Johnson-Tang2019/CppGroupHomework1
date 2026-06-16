# Generates TranslationExtended.cpp with Russian, French, and Classical Chinese catalogs.
from __future__ import annotations

import pathlib
import textwrap

from classical_gu import GU, HELP_GU

ROOT = pathlib.Path(__file__).resolve().parents[1]

# key -> (ru, fr)
T: dict[str, tuple[str, str]] = {
    "window_title": ("Remote Sensing Qt Starter", "Remote Sensing Qt Starter"),
    "menu.data": ("Данные", "Données"),
    "menu.raster": ("Обработка изображений", "Traitement d'images"),
    "menu.index": ("Дистанционные индексы", "Indices de télédétection"),
    "menu.photogrammetry": ("Фотограмметрия / 3D", "Photogrammétrie / 3D"),
    "menu.streetview": ("Панорама / Улицы", "Vue panoramique / Rue"),
    "menu.pointcloud": ("Обработка облака точек", "Nuage de points"),
    "menu.ai": ("AI", "AI"),
    "menu.settings": ("Настройки", "Paramètres"),
    "menu.help": ("Справка", "Aide"),
    "action.load_raster": ("Загрузить снимок (GDAL, несколько файлов)", "Charger une image (GDAL, multi-sélection)"),
    "action.load_pointcloud": ("Загрузить облако точек", "Charger un nuage de points"),
    "action.load_mesh": ("Загрузить Mesh", "Charger un maillage"),
    "action.load_dem": ("Загрузить DEM", "Charger un MNT"),
    "action.delete_layer": ("Удалить выбранные слои", "Supprimer les couches sélectionnées"),
    "action.clear_project": ("Очистить проект", "Effacer le projet"),
    "action.settings": ("Язык / Language...", "Langue / Language..."),
    "action.help_guide": ("Руководство", "Guide d'utilisation"),
    "action.show_ai": ("Показать AI-ассистента", "Afficher l'assistant IA"),
    "layer_tree": ("Слои проекта", "Couches du projet"),
    "tab.2d": ("2D изображение", "Image 2D"),
    "tab.3d": ("3D сцена", "Scène 3D"),
    "tab.panorama": ("360° панорама", "Panorama 360°"),
    "tab.log": ("Журнал", "Journal"),
    "tab.ai": ("AI-ассистент", "Assistant IA"),
    "settings.tab.general": ("Общие", "Général"),
    "settings.title": ("Настройки", "Paramètres"),
    "settings.language": ("Язык интерфейса", "Langue de l'interface"),
    "settings.theme": ("Цвет темы", "Couleur du thème"),
    "settings.care_mode": ("Режим заботы (крупный шрифт)", "Mode confort (grande police)"),
    "theme.pink": ("Светло-розовый (по умолчанию)", "Rose clair (par défaut)"),
    "theme.light_blue": ("Светло-голубой", "Bleu clair"),
    "theme.light_green": ("Светло-зелёный", "Vert clair"),
    "theme.lavender": ("Лавандовый", "Lavande"),
    "theme.warm_sand": ("Тёплый песок", "Sable chaud"),
    "theme.mint": ("Мятный", "Menthe"),
    "settings.lang.zh": ("中文", "中文"),
    "settings.lang.en": ("English", "English"),
    "settings.lang.ru": ("Русский", "Russe"),
    "settings.lang.fr": ("Français", "Français"),
    "settings.tab.guide": ("Руководство", "Guide d'utilisation"),
    "settings.button": ("Настройки", "Paramètres"),
    "log.startup": (
        "Starter запущен: эта сборка поддерживает GDAL, параметрические алгоритмы и каркас DEM/ортокоррекции.",
        "Starter lancé : cette version prend en charge GDAL, les algorithmes paramétriques et le flux MNT/orthorectification.",
    ),
    "menu.raster.band": ("Каналы и отображение", "Bandes et rendu"),
    "menu.raster.stat": ("Статистика", "Statistiques"),
    "menu.raster.enhance": ("Улучшение", "Amélioration"),
    "menu.raster.feature": ("Признаки и детекция", "Caractéristiques et détection"),
    "menu.raster.classify": ("Классификация", "Classification"),
    "action.render": ("Комбинация каналов / отображение...", "Combinaison de bandes / rendu..."),
    "action.histogram": ("Гистограмма...", "Histogramme en niveaux de gris..."),
    "action.equalize": ("Эквализация гистограммы...", "Égalisation d'histogramme..."),
    "action.stretch": ("Линейное/процентное растяжение...", "Étirement linéaire / par percentile..."),
    "action.clahe": ("CLAHE...", "Amélioration CLAHE..."),
    "action.gaussian_filter": ("Гауссов фильтр...", "Filtre gaussien..."),
    "action.median_filter": ("Медианный фильтр...", "Filtre médian..."),
    "action.bilateral_filter": ("Билатеральный фильтр...", "Filtre bilatéral..."),
    "action.unsharp": ("Unsharp резкость...", "Accentuation Unsharp..."),
    "action.laplacian_sharpen": ("Лапласиан резкость...", "Accentuation Laplacien..."),
    "action.feature_extract": ("ORB/SIFT/AKAZE извлечение...", "Extraction ORB/SIFT/AKAZE..."),
    "action.canny": ("Canny детекция...", "Détection de contours Canny..."),
    "action.kmeans": ("K-Means классификация...", "Classification K-Means..."),
    "action.svm": ("SVM классификация...", "Classification SVM..."),
    "action.contour": ("Детекция контуров...", "Détection par contours..."),
    "action.connected_components": ("Связные компоненты...", "Composantes connexes..."),
    "action.confusion_matrix": ("Матрица ошибок...", "Matrice de confusion..."),
    "action.index_calc": ("Вычислить NDVI/NDWI/NDBI...", "Calculer NDVI/NDWI/NDBI..."),
    "action.index_temporal": ("Сравнение индексов...", "Comparaison multi-temporelle..."),
    "action.index_export_csv": ("Экспорт CSV индексов...", "Exporter statistiques CSV..."),
    "action.dem_rebuild": ("Построение DEM...", "Reconstruction MNT..."),
    "action.orthorectify": ("Ортокоррекция...", "Orthorectification..."),
    "action.dem_texture": ("3D текстура DEM...", "Texturation 3D MNT..."),
    "action.load_panorama360": ("Загрузить 360° панораму...", "Charger panorama 360°..."),
    "action.voxel_downsample": ("Voxel downsampling...", "Sous-échantillonnage voxel..."),
    "action.statistical_filter": ("Статистический фильтр...", "Filtre statistique..."),
    "action.pointcloud_to_dem": ("Облако точек в DEM...", "Nuage de points vers MNT..."),
    "action.export_ply": ("Экспорт PLY...", "Exporter PLY..."),
    "action.delete_single_layer": ("Удалить слой", "Supprimer la couche"),
    "action.export_layer": ("Экспорт...", "Exporter..."),
    "action.zoom_to_extent": ("Масштаб к экстенту", "Zoom sur l'étendue"),
    "action.properties": ("Свойства", "Propriétés"),
    "action.export_group": ("Экспорт группы...", "Exporter le groupe..."),
    "tree.source_data": ("Исходные данные", "Données sources"),
    "tree.results": ("Результаты обработки", "Résultats de traitement"),
    "tree.raster": ("Дистанционные снимки", "Images de télédétection"),
    "tree.pointcloud": ("Облако точек", "Nuage de points"),
    "tree.panorama360": ("360° панорама", "Panorama 360°"),
    "tree.group.dem_rebuild": ("Построение DEM", "Reconstruction MNT"),
    "tree.group.orthorectify": ("Ортокоррекция", "Orthorectification"),
    "tree.group.histogram": ("Гистограмма", "Histogramme"),
    "tree.group.confusion_matrix": ("Матрица ошибок", "Matrice de confusion"),
    "tree.group.index_temporal": ("Сравнение индексов", "Comparaison multi-temporelle"),
    "geo.title": ("Координаты прочитаны", "Informations de coordonnées lues"),
    "geo.detected": ("В изображении обнаружена координатная информация.", "Des informations de coordonnées ont été détectées dans l'image."),
    "geo.file": ("Файл:", "Fichier :"),
    "geo.projection_snippet": ("Projection (WKT) snippet:", "Extrait Projection (WKT) :"),
    "view.select_layer": ("Выберите слой снимка или канал.", "Veuillez sélectionner une couche d'image ou une bande."),
    "view.no_render": ("Нет результата для отображения.\nТекущий слой: %1", "Aucun résultat affichable.\nCouche actuelle : %1"),
    "dialog.select_export_dir": ("Выберите папку экспорта", "Sélectionner le dossier d'export"),
    "dialog.load_raster": ("Загрузить снимок", "Charger une image"),
    "dialog.load_pointcloud": ("Загрузить облако точек", "Charger un nuage de points"),
    "dialog.load_mesh": ("Загрузить Mesh", "Charger un maillage"),
    "dialog.load_dem": ("Загрузить DEM", "Charger un MNT"),
    "dialog.load_panorama": ("Загрузить 360° панораму", "Charger un panorama 360°"),
    "dialog.export_layer": ("Экспорт слоя", "Exporter la couche"),
    "dialog.export_dem": ("Экспорт DEM", "Exporter le MNT"),
    "prop.title": ("Свойства слоя - %1", "Propriétés de la couche - %1"),
    "prop.name": ("Имя:", "Nom :"),
    "prop.path": ("Путь:", "Chemin :"),
    "prop.type": ("Тип:", "Type :"),
    "prop.visible": ("Видимость:", "Visible :"),
    "prop.yes": ("Да", "Oui"),
    "prop.no": ("Нет", "Non"),
    "prop.bands": ("Каналов:", "Bandes :"),
    "prop.size_pixels": ("Размер: %1 x %2 пикс.", "Taille : %1 x %2 pixels"),
    "prop.projection": ("Проекция:", "Projection :"),
    "prop.unknown": ("(неизвестно)", "(inconnu)"),
    "prop.point_count": ("Точек:", "Points :"),
    "prop.vertices": ("Вершин:", "Sommets :"),
    "prop.triangles": ("Треугольников:", "Triangles :"),
    "prop.size": ("Размер: %1 x %2", "Taille : %1 x %2"),
    "prop.summary": ("Сводка:", "Résumé :"),
    "type.raster": ("Дистанционный снимок", "Image de télédétection"),
    "type.pointcloud": ("Облако точек", "Nuage de points"),
    "type.mesh": ("Mesh модель", "Modèle maillé"),
    "type.dem": ("Цифровая модель рельефа", "Modèle numérique de terrain"),
    "type.panorama360": ("360° панорама", "Panorama 360°"),
    "type.result": ("Результат обработки", "Résultat de traitement"),
    "splash.title": ("Платформа обработки снимков", "Plateforme de traitement d'images"),
    "splash.starting": ("Запуск", "Démarrage"),
    "splash.team": ("Команда: CodeFour", "Équipe : CodeFour"),
    "help.title": ("Руководство", "Guide d'utilisation"),
    "log.no_previous_session": ("Ранее загруженные данные не найдены.", "Aucune donnée précédemment chargée trouvée."),
    "log.restoring_session": ("Восстановление данных (%1 элементов)...", "Restauration des données (%1 éléments)..."),
    "log.restore_file_missing": ("Ошибка: файл не найден [%1]", "Échec : fichier introuvable [%1]"),
    "log.restore_failed": ("Ошибка восстановления [%1]: %2", "Échec de restauration [%1] : %2"),
    "log.restore_done": ("Восстановлено: успешно %1, ошибок %2.", "Restauration : %1 réussis, %2 échoués."),
    "log.raster_loaded": ("Загружен снимок: %1 (%2 каналов, %3x%4)", "Image chargée : %1 (%2 bandes, %3x%4)"),
    "log.load_failed": ("Ошибка загрузки [%1]: %2", "Échec du chargement [%1] : %2"),
    "log.pointcloud_loaded": ("Загружено облако точек: %1 (%2 точек)", "Nuage de points chargé : %1 (%2 points)"),
    "log.pointcloud_load_failed": ("Ошибка загрузки облака [%1]: %2", "Échec chargement nuage [%1] : %2"),
    "log.mesh_loaded_vertices_only": ("Mesh %1 (%2 вершин, без граней)", "Maillage %1 (%2 sommets, sans faces)"),
    "log.mesh_loaded": ("Mesh %1 (%2 вершин, %3 граней)", "Maillage %1 (%2 sommets, %3 faces)"),
    "log.mesh_vbo_hint": ("Mesh отображён в 3D с VBO и сглаживанием нормалей.", "Maillage affiché en 3D avec VBO et normales lissées."),
    "log.mesh_load_failed": ("Ошибка загрузки Mesh [%1]: %2", "Échec chargement maillage [%1] : %2"),
    "log.dem_loaded": ("Загружен DEM: %1 (%2x%3)", "MNT chargé : %1 (%2x%3)"),
    "log.dem_load_failed": ("Ошибка загрузки DEM [%1]: %2", "Échec chargement MNT [%1] : %2"),
    "log.panorama_qt_failed_gdal": ("Qt не прочитал панораму: %1; пробуем GDAL.", "Qt n'a pas lu la panorama : %1 ; essai GDAL."),
    "log.panorama_gdal_failed": ("GDAL также не удался: %1", "Échec du repli GDAL : %1"),
    "log.panorama_ratio_hint": ("Соотношение сторон 360°: %1:1 (не 2:1).", "Ratio 360° : %1:1 (pas 2:1 standard)."),
    "log.panorama_loaded": ("Загружена панорама: %1 (%2x%3)", "Panorama chargée : %1 (%2x%3)"),
    "log.panorama_load_method": ("Способ чтения панорамы: %1", "Méthode de lecture : %1"),
    "log.layers_deleted": ("Удалено слоёв: %1.", "Couches supprimées : %1."),
    "log.project_cleared": ("Проект очищен.", "Projet effacé."),
    "log.render_failed": ("Ошибка рендера: %1", "Échec du rendu : %1"),
    "log.render_done": ("Отображено: %1, %2.", "Rendu : %1, %2."),
    "log.select_raster": ("Сначала выберите слой снимка.", "Sélectionnez d'abord une couche raster."),
    "log.select_raster_one": ("Выберите один слой снимка.", "Sélectionnez une couche raster."),
    "log.select_two_rasters_ref": ("Выберите два растра (прогноз + эталон).", "Sélectionnez deux rasters (prédiction + référence)."),
    "log.select_two_temporal": ("Выберите два растра разных дат.", "Sélectionnez deux rasters temporels."),
    "log.select_layer": ("Сначала выберите слой.", "Sélectionnez d'abord une couche."),
    "log.select_stereo_pair": ("Выберите стереопару (левый и правый).", "Sélectionnez une paire stéréo (gauche et droite)."),
    "log.select_raster_dem": ("Выберите снимок и DEM.", "Sélectionnez une image et un MNT."),
    "log.orthorectify_failed": ("Ортокоррекция не удалась.", "Orthorectification échouée."),
    "log.select_raster_dem_texture": ("Выберите снимок и DEM для 3D текстуры.", "Sélectionnez image et MNT pour texturation 3D."),
    "log.dem_texture_failed": ("3D текстура не удалась: %1", "Texturation 3D échouée : %1"),
    "log.dem_texture_done": ("3D текстура готова: DEM=%1, текстура=%2, источник=%3 %4", "Texturation 3D : MNT=%1, texture=%2, source=%3 %4"),
    "log.export_ply_hint": ("Выберите облако точек или Mesh.", "Sélectionnez un nuage de points ou un maillage."),
    "log.cannot_create_file": ("Не удалось создать файл: %1", "Impossible de créer le fichier : %1"),
    "log.export_pointcloud_ply": ("Экспорт PLY: %1 (%2 точек)", "PLY exporté : %1 (%2 points)"),
    "log.export_mesh_ply": ("Экспорт Mesh PLY: %1 (%2 вершин, %3 граней)", "Maillage PLY exporté : %1 (%2 sommets, %3 faces)"),
    "log.export_not_pointcloud_mesh": ("Слой не является облаком точек или Mesh.", "La couche n'est pas un nuage de points ou un maillage."),
    "log.export_failed": ("Ошибка экспорта: %1", "Échec de l'export : %1"),
    "log.select_pointcloud": ("Выберите облако точек.", "Sélectionnez un nuage de points."),
    "log.layer_shown": ("%1: показан", "%1 : affiché"),
    "log.layer_hidden": ("%1: скрыт", "%1 : masqué"),
    "log.export_group_done": ("Экспортировано %1/%2 слоёв группы.", "Exporté %1/%2 couches du groupe."),
    "log.layer_deleted": ("Слой удалён.", "Couche supprimée."),
    "log.zoom_todo_raster": ("TODO: масштаб к %1", "TODO : zoom sur %1"),
    "log.zoom_extent": ("Масштаб к %1.", "Zoom sur %1."),
    "log.panorama_switched": ("Переключено на панораму: %1.", "Basculé vers panorama : %1."),
    "log.zoom_todo": ("TODO: масштаб к %1", "TODO : zoom sur %1"),
    "log.export_no_raster": ("Экспорт [%1]: нет изображения.", "Export [%1] : pas d'image."),
    "log.export_write_failed": ("Не удалось записать: %1", "Impossible d'écrire : %1"),
    "log.export_raster_done": ("Экспорт: %1 → %2", "Exporté : %1 → %2"),
    "log.export_dem_done": ("DEM экспорт: %1 → %2", "MNT exporté : %1 → %2"),
    "log.export_dem_failed": ("Ошибка экспорта DEM: %1", "Échec export MNT : %1"),
    "log.export_no_panorama": ("Экспорт [%1]: нет панорамы.", "Export [%1] : pas de panorama."),
    "log.export_panorama_done": ("Панорама экспорт: %1 → %2", "Panorama exportée : %1 → %2"),
    "log.export_unsupported": ("Экспорт [%1]: тип не поддерживается.", "Export [%1] : type non pris en charge."),
}


def escape_qstring(val: str) -> str:
    return val.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def emit_catalog(name: str, lang_idx: int) -> str:
    lines = [f"const QHash<QString, QString> &{name}() {{"]
    lines.append("    static const QHash<QString, QString> table = {")
    for key, pair in T.items():
        val = escape_qstring(pair[lang_idx])
        lines.append('        {QStringLiteral("' + key + '"), QStringLiteral("' + val + '")},')
    lines.append("    };")
    lines.append("    return table;")
    lines.append("}")
    return "\n".join(lines)


def emit_gu_catalog() -> str:
    lines = ["const QHash<QString, QString> &classicalChineseCatalog() {"]
    lines.append("    static const QHash<QString, QString> table = {")
    for key in T:
        val = escape_qstring(GU.get(key, key))
        lines.append('        {QStringLiteral("' + key + '"), QStringLiteral("' + val + '")},')
    for key, raw in GU.items():
        if key not in T:
            val = escape_qstring(raw)
            lines.append('        {QStringLiteral("' + key + '"), QStringLiteral("' + val + '")},')
    lines.append("    };")
    lines.append("    return table;")
    lines.append("}")
    return "\n".join(lines)


HELP_RU = textwrap.dedent("""
<h2>Remote Sensing Qt Starter — Руководство</h2>
<h3>1. Быстрый старт</h3>
<ol>
<li>Меню <b>Данные</b>: загрузите снимок, облако точек, Mesh или DEM.</li>
<li>Выберите слой в дереве <b>Слои проекта</b>.</li>
<li>Вкладки <b>2D изображение</b> и <b>3D сцена</b> для просмотра.</li>
<li>Сообщения — в панели <b>Журнал</b>.</li>
</ol>
<h3>2. Обработка изображений</h3>
<ul>
<li><b>Каналы и отображение</b>, <b>Улучшение</b>, <b>Классификация</b>, индексы NDVI/NDWI/NDBI.</li>
</ul>
<h3>3. 3D и облако точек</h3>
<ul>
<li>Загрузка в 3D, downsampling, фильтры, DEM, экспорт PLY.</li>
</ul>
<h3>4. Панорама</h3>
<p>Загрузите 360° изображение через меню <b>Панорама / Улицы</b>.</p>
<h3>5. AI-ассистент</h3>
<p>Откройте вкладку <b>AI-ассистент</b> внизу окна.</p>
<h3>6. Настройки</h3>
<p>Кнопка <b>Настройки</b> справа вверху: язык, тема, режим заботы.</p>
""").strip()

HELP_FR = textwrap.dedent("""
<h2>Remote Sensing Qt Starter — Guide d'utilisation</h2>
<h3>1. Démarrage rapide</h3>
<ol>
<li>Menu <b>Données</b> : chargez une image, un nuage de points, un maillage ou un MNT.</li>
<li>Sélectionnez une couche dans <b>Couches du projet</b>.</li>
<li>Onglets <b>Image 2D</b> et <b>Scène 3D</b> pour la visualisation.</li>
<li>Messages dans le panneau <b>Journal</b>.</li>
</ol>
<h3>2. Traitement d'images</h3>
<ul>
<li><b>Bandes et rendu</b>, <b>Amélioration</b>, <b>Classification</b>, indices NDVI/NDWI/NDBI.</li>
</ul>
<h3>3. 3D et nuage de points</h3>
<ul>
<li>Chargement 3D, sous-échantillonnage, filtres, MNT, export PLY.</li>
</ul>
<h3>4. Panorama</h3>
<p>Chargez une image 360° via <b>Vue panoramique / Rue</b>.</p>
<h3>5. Assistant IA</h3>
<p>Ouvrez l'onglet <b>Assistant IA</b> en bas de la fenêtre.</p>
<h3>6. Paramètres</h3>
<p>Bouton <b>Paramètres</b> en haut à droite : langue, thème, mode confort.</p>
""").strip()


def main() -> None:
    cpp = ROOT / "RemoteSensingQtStarter/src/TranslationExtended.cpp"
    hpp = ROOT / "RemoteSensingQtStarter/include/rs/TranslationExtended.h"

    hpp.write_text(
        """#pragma once

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
""",
        encoding="utf-8",
    )

    body = f"""#include "rs/TranslationExtended.h"

namespace rs {{

{emit_catalog("russianCatalog", 0)}

{emit_catalog("frenchCatalog", 1)}

{emit_gu_catalog()}

QString russianHelpGuideHtml() {{
    return QStringLiteral(R\"({HELP_RU})");
}}

QString frenchHelpGuideHtml() {{
    return QStringLiteral(R\"({HELP_FR})");
}}

QString classicalChineseHelpGuideHtml() {{
    return QStringLiteral(R\"({HELP_GU})");
}}

}} // namespace rs
"""
    cpp.write_text(body, encoding="utf-8")
    print(f"Wrote {cpp} ({len(T)} keys)")


if __name__ == "__main__":
    main()

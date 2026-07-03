#include "rs/TranslationHelp.h"

namespace rs {

QString chineseHelpGuideHtml() {
    return QStringLiteral(R"(<h2>遥感影像处理平台 — 使用指南</h2>
<p><b>Remote Sensing Qt Starter</b> 由 <b>CodeFour</b> 团队开发（v1.0.4），集成 GDAL、OpenCV 等能力，支持遥感栅格、点云、Mesh、DEM 与 360° 全景数据的加载、处理、可视化与成果管理。</p>

<h3>1. 界面概览</h3>
<ul>
<li><b>顶部菜单</b>：数据、影像处理、遥感指数、摄影测量/三维、点云处理。</li>
<li><b>工具栏</b>：快速加载、导出、删除、清空、波段设色、放大/缩小/适应视图、AI 助手。</li>
<li><b>左侧工程图层</b>：按「源数据 / 处理结果」分组；可勾选显示/隐藏；支持 Ctrl/Shift 多选与右键菜单。</li>
<li><b>中央标签页</b>：二维影像、三维场景、360° 全景、滑动对比。</li>
<li><b>底部面板</b>：日志（带时间戳的处理记录）与 AI 助手。</li>
<li><b>状态栏</b>：在二维视图中移动鼠标，可显示像素坐标；若影像含投影信息，同时显示经纬度。</li>
<li><b>设置</b>：窗口右上角，可切换语言、主题色、颐养模式（大字），并编辑个人资料。</li>
</ul>

<h3>2. 快速上手</h3>
<ol>
<li>在 <b>数据</b> 菜单或工具栏加载所需数据（见下文格式说明）。</li>
<li>在左侧图层树中单击选中图层；遥感影像可在子节点中选择具体波段。</li>
<li>在 <b>二维影像</b> 标签查看栅格；点云/Mesh 在 <b>三维场景</b>；全景图在 <b>360° 全景</b>。</li>
<li>运行算法后，结果以新图层加入「处理结果」分组，可在日志面板查看详情。</li>
<li>程序启动后会自动尝试恢复上次会话中加载的数据。</li>
</ol>

<h3>3. 数据加载（数据菜单）</h3>
<ul>
<li><b>加载遥感影像</b>（GDAL，可多选）：GeoTIFF、IMG、DAT、JPEG2000、JPEG、PNG、BMP 等。</li>
<li><b>加载点云</b>：PLY、XYZ、LAS。</li>
<li><b>加载 Mesh</b>：OBJ、PLY。</li>
<li><b>加载 DEM</b>：GeoTIFF、ASC、DEM。</li>
<li><b>加载 360° 全景/街景</b>：JPG、PNG、BMP、TIFF 等（推荐 2:1 宽高比）。</li>
<li><b>删除所选图层 / 清空工程</b>：批量删除或重置整个项目。</li>
</ul>
<p>加载含坐标信息的遥感影像时，会弹出坐标信息提示；可在图层右键「属性」中查看路径、尺寸、波段、投影等。</p>

<h3>4. 影像处理（影像处理菜单）</h3>
<p>请先选中一个遥感影像图层（或指定波段），再执行下列功能。处理结果默认保存为新图层。</p>
<ul>
<li><b>波段与设色</b>：设置 RGB/灰度波段组合与显示配色。</li>
<li><b>统计</b>：灰度直方图。</li>
<li><b>增强</b>：直方图均衡化；线性/百分比拉伸；CLAHE；高斯/中值/双边滤波；Unsharp / 拉普拉斯锐化。</li>
<li><b>特征与检测</b>：ORB/SIFT/AKAZE 特征提取；Canny 边缘检测（可调阈值）。</li>
<li><b>分类与检测</b>：K-Means 无监督分类；SVM 地物分类；轮廓目标检测；连通域检测；混淆矩阵精度评价（需同时选中预测图与参考图，可导出 CSV）。</li>
</ul>

<h3>5. 遥感指数（遥感指数菜单）</h3>
<ul>
<li><b>计算 NDVI / NDWI / NDBI / MNDWI / NDCI / NDTI / TSS / TLI / FUI / PC / CDOM</b>：可选是否应用阈值分割掩膜。</li>
<li><b>多时相指数对比</b>：选中两个不同时相的栅格图层，生成 NDVI 变化对比结果。</li>
<li><b>前后影像滑动对比</b>：选中两期影像，在「滑动对比」标签页拖动中间分隔线对比（亦可在图层树选中两期后自动更新）。</li>
<li><b>导出指数统计 CSV</b>：将当前选中图层的指数统计导出为 CSV 文件。</li>
</ul>

<h3>6. 摄影测量 / 三维</h3>
<ul>
<li><b>DEM 重建</b>：基于立体像对重建数字高程模型。</li>
<li><b>正射影像校正</b>：需选中遥感影像与 DEM，进行正射校正。</li>
<li><b>DEM 三维贴图</b>：将影像纹理映射到 DEM，在三维场景中展示。</li>
</ul>

<h3>7. 点云处理（点云处理菜单）</h3>
<ul>
<li><b>体素降采样</b>：减少点云密度。</li>
<li><b>统计滤波</b>：去除离群噪点。</li>
<li><b>点云转 DEM</b>：由点云生成栅格 DEM 图层。</li>
<li><b>导出 PLY</b>：将点云或 Mesh 导出为 PLY 文件。</li>
</ul>

<h3>8. 视图操作</h3>
<ul>
<li><b>二维影像</b>：Ctrl + 滚轮缩放；拖拽平移；工具栏放大/缩小/适应视图。</li>
<li><b>三维场景</b>：鼠标旋转/缩放浏览点云与 Mesh（OpenGL 渲染）。</li>
<li><b>360° 全景</b>：鼠标拖动环视，滚轮缩放；选中全景图层后自动切换到此标签。</li>
<li><b>滑动对比</b>：在对比区域内拖动竖线查看前后差异；图层名称显示于两侧。</li>
</ul>

<h3>9. 图层树与右键菜单</h3>
<ul>
<li>勾选框控制图层可见性；取消勾选后对应视图不再显示。</li>
<li>右键图层：<b>导出</b>、<b>缩放至范围</b>、<b>属性</b>、<b>删除图层</b>。</li>
<li>右键分组文件夹：<b>导出该分组</b>、<b>删除所选图层</b>。</li>
<li>导出格式依类型而定：影像支持 PNG/JPEG/BMP/TIFF；DEM 支持 GeoTIFF；全景导出为图像文件。</li>
</ul>

<h3>10. AI 助手</h3>
<p>打开底部 <b>AI 助手</b> 标签（或工具栏 AI 按钮）。可配置国产视觉模型 API 密钥与接入点；发送问题时程序会自动附带：</p>
<ul>
<li>已导入图层的名称、类型、尺寸、波段等元数据；</li>
<li>当前二维/三维/全景视图的截图（用于地物、建筑、道路、水体、植被等视觉识别）。</li>
</ul>
<p>也可点击「插入文件信息」仅查看当前图层摘要。默认视觉模型提供有限次数免费体验。</p>

<h3>11. 设置</h3>
<ul>
<li><b>常规</b>：界面语言（中文 / English / Русский / Français / 古语）、主题色（粉、蓝、绿、紫、沙、薄荷）、颐养模式（全局放大字体）。</li>
<li><b>个人资料</b>：头像、昵称、生日、地址、邮箱（本地保存）。</li>
<li><b>使用指南</b>：本说明文档。</li>
</ul>

<h3>12. 常见问题与提示</h3>
<ul>
<li>多数算法需要先选中正确类型的图层，否则日志会给出提示。</li>
<li>处理结果图层位于「处理结果」下，按算法类型分子组（如直方图、混淆矩阵、DEM 重建等）。</li>
<li>混淆矩阵、多时相对比等需 <b>同时选中两个</b> 栅格图层。</li>
<li>日志面板内容随语言切换自动翻译；算法返回的原始消息可能仍为中文。</li>
<li>当前版本 <b>1.0.4</b>；开发团队见 <b>设置 → 关于</b>。</li>
</ul>
<h3>13. 开发团队（CodeFour）</h3>
<ul>
<li><b>Johnson-Tang2019</b>（汤骏）— 项目负责人</li>
<li><b>wangchablis-sys</b>（王宇凡）— 核心开发</li>
<li><b>Austin9633</b>（梁邵臣）— 算法与三维</li>
<li><b>LiamSmith4399xyx</b>（王昕竹）— 界面与体验</li>
</ul>)");
}

QString englishHelpGuideHtml() {
    return QStringLiteral(R"(<h2>Remote Sensing Qt Starter — User Guide</h2>
<p><b>Remote Sensing Qt Starter</b> v1.0.4 by team <b>CodeFour</b>. It integrates GDAL and OpenCV to load, process, visualize, and manage rasters, point clouds, meshes, DEMs, and 360° panoramas.</p>

<h3>1. Interface Overview</h3>
<ul>
<li><b>Top menus</b>: Data, Raster Processing, Remote Sensing Indices, Photogrammetry / 3D, Point Cloud.</li>
<li><b>Toolbar</b>: quick load, export, delete, clear, band rendering, zoom in/out/fit, AI assistant.</li>
<li><b>Project Layers</b> (left): grouped as Source Data / Processing Results; visibility toggles; Ctrl/Shift multi-select; context menu.</li>
<li><b>Center tabs</b>: 2D Image, 3D Scene, 360° Panorama, Swipe Compare.</li>
<li><b>Bottom panel</b>: Log (timestamped messages) and AI Assistant.</li>
<li><b>Status bar</b>: pixel coordinates while moving the mouse in 2D view; lon/lat when projection is available.</li>
<li><b>Settings</b> (top-right): language, theme, care mode (larger fonts), profile.</li>
</ul>

<h3>2. Quick Start</h3>
<ol>
<li>Load data from the <b>Data</b> menu or toolbar (see supported formats below).</li>
<li>Select a layer in the tree; for rasters you may pick a band under the layer node.</li>
<li>View rasters in <b>2D Image</b>; point clouds/meshes in <b>3D Scene</b>; panoramas in <b>360° Panorama</b>.</li>
<li>Algorithm outputs appear as new layers under Processing Results; check the Log panel for details.</li>
<li>The app attempts to restore layers from your last session on startup.</li>
</ol>

<h3>3. Data Loading (Data Menu)</h3>
<ul>
<li><b>Load raster</b> (GDAL, multi-select): GeoTIFF, IMG, DAT, JPEG2000, JPEG, PNG, BMP, etc.</li>
<li><b>Load point cloud</b>: PLY, XYZ, LAS.</li>
<li><b>Load mesh</b>: OBJ, PLY.</li>
<li><b>Load DEM</b>: GeoTIFF, ASC, DEM.</li>
<li><b>Load 360° panorama</b>: JPG, PNG, BMP, TIFF (2:1 aspect ratio recommended).</li>
<li><b>Delete selected layers / Clear project</b>.</li>
</ul>
<p>When georeferencing is detected, a coordinate info dialog appears. Use layer Properties for path, size, bands, and projection.</p>

<h3>4. Raster Processing</h3>
<p>Select a raster layer (or band) first. Results are added as new layers.</p>
<ul>
<li><b>Band &amp; Rendering</b>: RGB/gray band combination and color mapping.</li>
<li><b>Statistics</b>: grayscale histogram.</li>
<li><b>Enhancement</b>: histogram equalization; linear/percent stretch; CLAHE; Gaussian/median/bilateral filters; Unsharp/Laplacian sharpening.</li>
<li><b>Features &amp; Detection</b>: ORB/SIFT/AKAZE extraction; Canny edge detection.</li>
<li><b>Classification &amp; Detection</b>: K-Means; SVM; contour detection; connected components; confusion matrix (select prediction + reference rasters, optional CSV export).</li>
</ul>

<h3>5. Remote Sensing Indices</h3>
<ul>
<li><b>NDVI / NDWI / NDBI / MNDWI / NDCI / NDTI / TSS / TLI / FUI / PC / CDOM</b> with optional threshold mask.</li>
<li><b>Multi-temporal index compare</b>: select two rasters from different dates.</li>
<li><b>Before/After swipe compare</b>: drag the divider in the Swipe Compare tab.</li>
<li><b>Export index statistics CSV</b>.</li>
</ul>

<h3>6. Photogrammetry / 3D</h3>
<ul>
<li><b>DEM reconstruction</b> from stereo pairs.</li>
<li><b>Orthorectification</b> (raster + DEM required).</li>
<li><b>DEM 3D texturing</b> for textured terrain in the 3D tab.</li>
</ul>

<h3>7. Point Cloud Processing</h3>
<ul>
<li><b>Voxel downsample</b>, <b>statistical filter</b>, <b>point cloud to DEM</b>, <b>export PLY</b>.</li>
</ul>

<h3>8. View Controls</h3>
<ul>
<li><b>2D</b>: Ctrl + mouse wheel zoom; drag to pan; toolbar zoom/fit.</li>
<li><b>3D</b>: rotate/zoom point clouds and meshes (OpenGL).</li>
<li><b>360° Panorama</b>: drag to look around; wheel to zoom.</li>
<li><b>Swipe Compare</b>: drag the vertical slider between two temporal images.</li>
</ul>

<h3>9. Layer Tree &amp; Context Menu</h3>
<ul>
<li>Checkbox toggles visibility.</li>
<li>Right-click layer: Export, Zoom to Extent, Properties, Delete.</li>
<li>Right-click group: Export Group, Delete selected layers.</li>
</ul>

<h3>10. AI Assistant</h3>
<p>Configure a vision-model API key. Questions automatically include layer metadata and a screenshot of the current view for land-cover / object analysis. Use <b>Insert file info</b> to preview context only.</p>

<h3>11. Settings</h3>
<ul>
<li><b>General</b>: language (Chinese / English / Russian / French / Classical Chinese), theme colors, care mode.</li>
<li><b>Profile</b>: avatar, nickname, birthday, address, email (stored locally).</li>
<li><b>User Guide</b>: this manual.</li>
</ul>

<h3>12. Tips</h3>
<ul>
<li>Many tools require the correct layer type to be selected first.</li>
<li>Some workflows need <b>two rasters</b> selected (confusion matrix, temporal compare, swipe compare).</li>
<li>Version <b>1.0.4</b> — see <b>Settings → About</b> for the full team list.</li>
</ul>
<h3>13. Team CodeFour</h3>
<ul>
<li><b>Johnson-Tang2019</b> (Tang Jun) — Project Lead</li>
<li><b>wangchablis-sys</b> (Wang Yufan) — Core Developer</li>
<li><b>Austin9633</b> (Liang Shaochen) — Algorithms &amp; 3D</li>
<li><b>LiamSmith4399xyx</b> (Wang Xinzhu) — UI &amp; UX</li>
</ul>)");
}

QString russianHelpGuideHtml() {
    return QStringLiteral(R"(<h2>Remote Sensing Qt Starter — Руководство</h2>
<p>Платформа от команды <b>CodeFour</b>. Поддержка GDAL/OpenCV: растры, облака точек, mesh, DEM, панорамы 360°.</p>

<h3>1. Интерфейс</h3>
<ul>
<li>Меню: <b>Данные</b>, обработка изображений, индексы, фотограмметрия/3D, облако точек.</li>
<li>Слева — <b>Слои проекта</b> (исходные / результаты); вкладки: 2D, 3D, панорама, сравнение.</li>
<li>Внизу — <b>Журнал</b> и <b>AI-ассистент</b>; справа вверху — <b>Настройки</b>.</li>
</ul>

<h3>2. Загрузка данных</h3>
<ul>
<li>Растр (GDAL): TIF, IMG, JP2, JPG, PNG и др.</li>
<li>Облако точек: PLY, XYZ, LAS; Mesh: OBJ, PLY; DEM: TIF, ASC.</li>
<li>Панорама 360°; удаление слоёв; очистка проекта.</li>
</ul>

<h3>3. Обработка растра</h3>
<ul>
<li>Каналы/отображение, гистограмма, улучшение (эквализация, растяжение, CLAHE, фильтры, резкость).</li>
<li>ORB/SIFT/AKAZE, Canny; K-Means, SVM, контуры, связные компоненты, матрица ошибок.</li>
</ul>

<h3>4. Индексы и сравнение</h3>
<ul>
<li>NDVI, NDWI, NDBI, MNDWI, NDCI, NDTI, TSS, TLI, FUI, PC, CDOM; сравнение по времени; <b>сравнение с ползунком</b>; экспорт CSV.</li>
</ul>

<h3>5. 3D и облако точек</h3>
<ul>
<li>DEM, ортокоррекция, 3D-текстура; downsampling, фильтр, облако→DEM, экспорт PLY.</li>
</ul>

<h3>6. AI и настройки</h3>
<p>AI-ассистент прикрепляет метаданные слоёв и снимок экрана. Настройки: язык (中文/EN/RU/FR/古语), тема, режим заботы, профиль.</p>

<h3>7. Подсказки</h3>
<ul>
<li>Ctrl + колёсико — масштаб в 2D; результаты — новые слои; восстановление сессии при запуске.</li>
<li>Команда: <b>CodeFour</b> v1.0.4 — см. <b>Настройки → О программе</b></li>
</ul>)");
}

QString frenchHelpGuideHtml() {
    return QStringLiteral(R"(<h2>Remote Sensing Qt Starter — Guide d'utilisation</h2>
<p>Plateforme développée par l'équipe <b>CodeFour</b>. GDAL/OpenCV : images, nuages de points, maillages, MNT, panoramas 360°.</p>

<h3>1. Interface</h3>
<ul>
<li>Menus : <b>Données</b>, traitement d'images, indices, photogrammétrie/3D, nuage de points.</li>
<li>Arbre <b>Couches du projet</b> ; onglets 2D, 3D, panorama, comparaison par glissement.</li>
<li>Panneau <b>Journal</b> et <b>Assistant IA</b> ; bouton <b>Paramètres</b> en haut à droite.</li>
</ul>

<h3>2. Chargement</h3>
<ul>
<li>Raster (GDAL) : TIF, IMG, JP2, JPG, PNG… ; nuage PLY/XYZ/LAS ; maillage OBJ/PLY ; MNT ; panorama 360°.</li>
</ul>

<h3>3. Traitement raster</h3>
<ul>
<li>Bandes/rendu, histogramme, amélioration (égalisation, étirement, CLAHE, filtres, netteté).</li>
<li>ORB/SIFT/AKAZE, Canny ; K-Means, SVM, contours, composantes connexes, matrice de confusion.</li>
</ul>

<h3>4. Indices</h3>
<ul>
<li>NDVI, NDWI, NDBI, MNDWI, NDCI, NDTI, TSS, TLI, FUI, PC, CDOM ; comparaison multi-temporelle ; <b>comparaison avant/après</b> ; export CSV.</li>
</ul>

<h3>5. 3D et nuage de points</h3>
<ul>
<li>Reconstruction MNT, orthorectification, texturation 3D ; sous-échantillonnage, filtre, nuage→MNT, export PLY.</li>
</ul>

<h3>6. IA et paramètres</h3>
<p>L'assistant IA joint les métadonnées et une capture d'écran. Langues, thème, mode confort, profil utilisateur.</p>

<h3>7. Astuces</h3>
<ul>
<li>Ctrl + molette pour zoomer en 2D ; résultats ajoutés comme nouvelles couches.</li>
<li>Équipe : <b>CodeFour</b> v1.0.4 — voir <b>Paramètres → À propos</b></li>
</ul>)");
}

QString classicalChineseHelpGuideHtml() {
    return QStringLiteral(R"(<h2>遥感像图处置平台 — 用式指南</h2>
<p><b>Remote Sensing Qt Starter</b> 乃 <b>CodeFour</b> 所制，集 GDAL、OpenCV 诸能，可载栅格、点云、网格、DEM、360° 全景，以处置、示之、管其成果。</p>

<h3>一、界面概要</h3>
<ul>
<li>顶栏：<b>数籍</b>、像图处置、遥感指数、测绘/三维、点云处置。</li>
<li>左：<b>工图层级</b>（源数 / 处置成果）；中：二维像图、三维场景、360° 全景、滑动对比；下：录事、AI 佐。</li>
<li>右上 <b>设署</b>：换语言（中文/English/俄/法/古语）、主题色、颐养模式、个人资料。</li>
</ul>

<h3>二、数籍载入</h3>
<ul>
<li>遥感像图（GDAL，可多择）：TIF、IMG、JP2、JPG、PNG 等。</li>
<li>点云：PLY、XYZ、LAS；网格：OBJ、PLY；DEM：TIF、ASC；360° 全景。</li>
<li>删图层、清工部；启时复旧前次所载入者。</li>
</ul>

<h3>三、像图处置</h3>
<ul>
<li>波段设色、直方图、增强（均衡、拉伸、CLAHE、滤波、锐化）。</li>
<li>ORB/SIFT/AKAZE、Canny；K-Means、SVM、轮廓、连通域、混淆矩阵。</li>
</ul>

<h3>四、遥感指数</h3>
<ul>
<li>NDVI/NDWI/NDBI/MNDWI/NDCI/NDTI/TSS/TLI/FUI/PC/CDOM；多时相对比；前后滑动对比；导出 CSV。</li>
</ul>

<h3>五、三维与点云</h3>
<ul>
<li>DEM 重建、正射校正、三维贴图；降采样、统计滤波、点云转 DEM、导出 PLY。</li>
</ul>

<h3>六、AI 佐与要诀</h3>
<p>AI 佐可附图层元数据与当前画面截图以问。二维 Ctrl+滚轮缩放；成果添为新图层。版本 <b>1.0.4</b>，开发诸君见 <b>设署 → 关于</b>。制作团队：<b>CodeFour</b></p>)");
}

} // namespace rs

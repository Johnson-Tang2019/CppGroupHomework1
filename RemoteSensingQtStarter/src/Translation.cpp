#include "rs/Translation.h"

#include <QHash>
#include <QSettings>

namespace rs {

namespace {

QString langSuffix(AppLanguage language) {
    return language == AppLanguage::English ? QStringLiteral("en") : QStringLiteral("zh");
}

const QHash<QString, QHash<QString, QString>> &catalog() {
    static const QHash<QString, QHash<QString, QString>> table = {
        {QStringLiteral("window_title"),
         {{QStringLiteral("zh"), QStringLiteral("Remote Sensing Qt Starter")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Qt Starter")}}},
        {QStringLiteral("menu.data"),
         {{QStringLiteral("zh"), QStringLiteral("数据")}, {QStringLiteral("en"), QStringLiteral("Data")}}},
        {QStringLiteral("menu.raster"),
         {{QStringLiteral("zh"), QStringLiteral("影像处理")},
          {QStringLiteral("en"), QStringLiteral("Raster Processing")}}},
        {QStringLiteral("menu.index"),
         {{QStringLiteral("zh"), QStringLiteral("遥感指数")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Indices")}}},
        {QStringLiteral("menu.photogrammetry"),
         {{QStringLiteral("zh"), QStringLiteral("摄影测量/三维")},
          {QStringLiteral("en"), QStringLiteral("Photogrammetry / 3D")}}},
        {QStringLiteral("menu.streetview"),
         {{QStringLiteral("zh"), QStringLiteral("街景/全景")},
          {QStringLiteral("en"), QStringLiteral("Street View / Panorama")}}},
        {QStringLiteral("menu.pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("点云处理")},
          {QStringLiteral("en"), QStringLiteral("Point Cloud")}}},
        {QStringLiteral("menu.ai"),
         {{QStringLiteral("zh"), QStringLiteral("AI")}, {QStringLiteral("en"), QStringLiteral("AI")}}},
        {QStringLiteral("menu.settings"),
         {{QStringLiteral("zh"), QStringLiteral("设置")}, {QStringLiteral("en"), QStringLiteral("Settings")}}},
        {QStringLiteral("menu.help"),
         {{QStringLiteral("zh"), QStringLiteral("帮助")}, {QStringLiteral("en"), QStringLiteral("Help")}}},
        {QStringLiteral("action.load_raster"),
         {{QStringLiteral("zh"), QStringLiteral("加载遥感影像(GDAL，可多选)")},
          {QStringLiteral("en"), QStringLiteral("Load Raster (GDAL, multi-select)")}}},
        {QStringLiteral("action.load_pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("加载点云")},
          {QStringLiteral("en"), QStringLiteral("Load Point Cloud")}}},
        {QStringLiteral("action.load_mesh"),
         {{QStringLiteral("zh"), QStringLiteral("加载 Mesh")},
          {QStringLiteral("en"), QStringLiteral("Load Mesh")}}},
        {QStringLiteral("action.load_dem"),
         {{QStringLiteral("zh"), QStringLiteral("加载 DEM")},
          {QStringLiteral("en"), QStringLiteral("Load DEM")}}},
        {QStringLiteral("action.delete_layer"),
         {{QStringLiteral("zh"), QStringLiteral("删除选中图层")},
          {QStringLiteral("en"), QStringLiteral("Delete Selected Layers")}}},
        {QStringLiteral("action.clear_project"),
         {{QStringLiteral("zh"), QStringLiteral("初始化/清空工程")},
          {QStringLiteral("en"), QStringLiteral("Clear Project")}}},
        {QStringLiteral("action.settings"),
         {{QStringLiteral("zh"), QStringLiteral("语言 / Language...")},
          {QStringLiteral("en"), QStringLiteral("Language / 语言...")}}},
        {QStringLiteral("action.help_guide"),
         {{QStringLiteral("zh"), QStringLiteral("使用指南")},
          {QStringLiteral("en"), QStringLiteral("User Guide")}}},
        {QStringLiteral("action.show_ai"),
         {{QStringLiteral("zh"), QStringLiteral("显示 AI 助手")},
          {QStringLiteral("en"), QStringLiteral("Show AI Assistant")}}},
        {QStringLiteral("layer_tree"),
         {{QStringLiteral("zh"), QStringLiteral("工程图层")},
          {QStringLiteral("en"), QStringLiteral("Project Layers")}}},
        {QStringLiteral("tab.2d"),
         {{QStringLiteral("zh"), QStringLiteral("二维影像")},
          {QStringLiteral("en"), QStringLiteral("2D Image")}}},
        {QStringLiteral("tab.3d"),
         {{QStringLiteral("zh"), QStringLiteral("三维场景")},
          {QStringLiteral("en"), QStringLiteral("3D Scene")}}},
        {QStringLiteral("tab.panorama"),
         {{QStringLiteral("zh"), QStringLiteral("360街景")},
          {QStringLiteral("en"), QStringLiteral("360 Panorama")}}},
        {QStringLiteral("tab.log"),
         {{QStringLiteral("zh"), QStringLiteral("日志")}, {QStringLiteral("en"), QStringLiteral("Log")}}},
        {QStringLiteral("tab.ai"),
         {{QStringLiteral("zh"), QStringLiteral("AI 助手")},
          {QStringLiteral("en"), QStringLiteral("AI Assistant")}}},
        {QStringLiteral("settings.tab.general"),
         {{QStringLiteral("zh"), QStringLiteral("常规")},
          {QStringLiteral("en"), QStringLiteral("General")}}},
        {QStringLiteral("settings.title"),
         {{QStringLiteral("zh"), QStringLiteral("设置")}, {QStringLiteral("en"), QStringLiteral("Settings")}}},
        {QStringLiteral("settings.language"),
         {{QStringLiteral("zh"), QStringLiteral("界面语言")},
          {QStringLiteral("en"), QStringLiteral("Interface Language")}}},
        {QStringLiteral("settings.theme"),
         {{QStringLiteral("zh"), QStringLiteral("主题颜色")},
          {QStringLiteral("en"), QStringLiteral("Theme Color")}}},
        {QStringLiteral("theme.pink"),
         {{QStringLiteral("zh"), QStringLiteral("淡粉色（默认）")},
          {QStringLiteral("en"), QStringLiteral("Light Pink (Default)")}}},
        {QStringLiteral("theme.light_blue"),
         {{QStringLiteral("zh"), QStringLiteral("淡蓝色")},
          {QStringLiteral("en"), QStringLiteral("Light Blue")}}},
        {QStringLiteral("theme.light_green"),
         {{QStringLiteral("zh"), QStringLiteral("淡绿色")},
          {QStringLiteral("en"), QStringLiteral("Light Green")}}},
        {QStringLiteral("theme.lavender"),
         {{QStringLiteral("zh"), QStringLiteral("淡紫色")},
          {QStringLiteral("en"), QStringLiteral("Lavender")}}},
        {QStringLiteral("theme.warm_sand"),
         {{QStringLiteral("zh"), QStringLiteral("暖沙色")},
          {QStringLiteral("en"), QStringLiteral("Warm Sand")}}},
        {QStringLiteral("theme.mint"),
         {{QStringLiteral("zh"), QStringLiteral("薄荷绿")},
          {QStringLiteral("en"), QStringLiteral("Mint")}}},
        {QStringLiteral("settings.lang.zh"),
         {{QStringLiteral("zh"), QStringLiteral("中文")}, {QStringLiteral("en"), QStringLiteral("Chinese")}}},
        {QStringLiteral("settings.lang.en"),
         {{QStringLiteral("zh"), QStringLiteral("English")},
          {QStringLiteral("en"), QStringLiteral("English")}}},
        {QStringLiteral("settings.tab.guide"),
         {{QStringLiteral("zh"), QStringLiteral("使用指南")},
          {QStringLiteral("en"), QStringLiteral("User Guide")}}},
        {QStringLiteral("settings.button"),
         {{QStringLiteral("zh"), QStringLiteral("设置")},
          {QStringLiteral("en"), QStringLiteral("Settings")}}},
        {QStringLiteral("log.startup"),
         {{QStringLiteral("zh"),
           QStringLiteral("Starter 已启动：当前版本提供 GDAL 多波段、参数化算法、DEM/正射流程的工程骨架。")},
          {QStringLiteral("en"),
           QStringLiteral(
               "Starter launched: this build provides GDAL multi-band workflows, parametric algorithms, "
               "and DEM/orthorectification scaffolding.")}}},
        {QStringLiteral("menu.raster.band"),
         {{QStringLiteral("zh"), QStringLiteral("波段与设色")},
          {QStringLiteral("en"), QStringLiteral("Band & Rendering")}}},
        {QStringLiteral("menu.raster.stat"),
         {{QStringLiteral("zh"), QStringLiteral("统计")}, {QStringLiteral("en"), QStringLiteral("Statistics")}}},
        {QStringLiteral("menu.raster.enhance"),
         {{QStringLiteral("zh"), QStringLiteral("增强")}, {QStringLiteral("en"), QStringLiteral("Enhancement")}}},
        {QStringLiteral("menu.raster.feature"),
         {{QStringLiteral("zh"), QStringLiteral("特征与检测")},
          {QStringLiteral("en"), QStringLiteral("Features & Detection")}}},
        {QStringLiteral("menu.raster.classify"),
         {{QStringLiteral("zh"), QStringLiteral("分类与检测")},
          {QStringLiteral("en"), QStringLiteral("Classification & Detection")}}},
        {QStringLiteral("action.render"),
         {{QStringLiteral("zh"), QStringLiteral("波段组合/设色...")},
          {QStringLiteral("en"), QStringLiteral("Band Combination / Rendering...")}}},
        {QStringLiteral("action.histogram"),
         {{QStringLiteral("zh"), QStringLiteral("灰度直方图...")},
          {QStringLiteral("en"), QStringLiteral("Grayscale Histogram...")}}},
        {QStringLiteral("action.equalize"),
         {{QStringLiteral("zh"), QStringLiteral("直方图均衡化...")},
          {QStringLiteral("en"), QStringLiteral("Histogram Equalization...")}}},
        {QStringLiteral("action.stretch"),
         {{QStringLiteral("zh"), QStringLiteral("线性/百分比拉伸...")},
          {QStringLiteral("en"), QStringLiteral("Linear / Percent Stretch...")}}},
        {QStringLiteral("action.clahe"),
         {{QStringLiteral("zh"), QStringLiteral("CLAHE 增强...")},
          {QStringLiteral("en"), QStringLiteral("CLAHE Enhancement...")}}},
        {QStringLiteral("action.gaussian_filter"),
         {{QStringLiteral("zh"), QStringLiteral("高斯滤波...")},
          {QStringLiteral("en"), QStringLiteral("Gaussian Filter...")}}},
        {QStringLiteral("action.median_filter"),
         {{QStringLiteral("zh"), QStringLiteral("中值滤波...")},
          {QStringLiteral("en"), QStringLiteral("Median Filter...")}}},
        {QStringLiteral("action.bilateral_filter"),
         {{QStringLiteral("zh"), QStringLiteral("双边滤波...")},
          {QStringLiteral("en"), QStringLiteral("Bilateral Filter...")}}},
        {QStringLiteral("action.unsharp"),
         {{QStringLiteral("zh"), QStringLiteral("Unsharp 锐化...")},
          {QStringLiteral("en"), QStringLiteral("Unsharp Sharpen...")}}},
        {QStringLiteral("action.laplacian_sharpen"),
         {{QStringLiteral("zh"), QStringLiteral("拉普拉斯锐化...")},
          {QStringLiteral("en"), QStringLiteral("Laplacian Sharpen...")}}},
        {QStringLiteral("action.feature_extract"),
         {{QStringLiteral("zh"), QStringLiteral("ORB/SIFT/AKAZE 特征提取...")},
          {QStringLiteral("en"), QStringLiteral("ORB/SIFT/AKAZE Feature Extraction...")}}},
        {QStringLiteral("action.canny"),
         {{QStringLiteral("zh"), QStringLiteral("Canny 边缘检测...")},
          {QStringLiteral("en"), QStringLiteral("Canny Edge Detection...")}}},
        {QStringLiteral("action.kmeans"),
         {{QStringLiteral("zh"), QStringLiteral("K-Means 无监督分类...")},
          {QStringLiteral("en"), QStringLiteral("K-Means Unsupervised Classification...")}}},
        {QStringLiteral("action.svm"),
         {{QStringLiteral("zh"), QStringLiteral("SVM 地物分类...")},
          {QStringLiteral("en"), QStringLiteral("SVM Land Cover Classification...")}}},
        {QStringLiteral("action.contour"),
         {{QStringLiteral("zh"), QStringLiteral("轮廓目标检测...")},
          {QStringLiteral("en"), QStringLiteral("Contour Object Detection...")}}},
        {QStringLiteral("action.connected_components"),
         {{QStringLiteral("zh"), QStringLiteral("连通域目标检测...")},
          {QStringLiteral("en"), QStringLiteral("Connected Components Detection...")}}},
        {QStringLiteral("action.confusion_matrix"),
         {{QStringLiteral("zh"), QStringLiteral("混淆矩阵精度评价...")},
          {QStringLiteral("en"), QStringLiteral("Confusion Matrix Accuracy...")}}},
        {QStringLiteral("action.index_calc"),
         {{QStringLiteral("zh"), QStringLiteral("计算 NDVI/NDWI/NDBI...")},
          {QStringLiteral("en"), QStringLiteral("Compute NDVI/NDWI/NDBI...")}}},
        {QStringLiteral("action.index_temporal"),
         {{QStringLiteral("zh"), QStringLiteral("多时相指数对比...")},
          {QStringLiteral("en"), QStringLiteral("Multi-temporal Index Compare...")}}},
        {QStringLiteral("action.index_export_csv"),
         {{QStringLiteral("zh"), QStringLiteral("导出指数统计 CSV...")},
          {QStringLiteral("en"), QStringLiteral("Export Index Statistics CSV...")}}},
        {QStringLiteral("action.dem_rebuild"),
         {{QStringLiteral("zh"), QStringLiteral("DEM 重建...")},
          {QStringLiteral("en"), QStringLiteral("DEM Reconstruction...")}}},
        {QStringLiteral("action.orthorectify"),
         {{QStringLiteral("zh"), QStringLiteral("正射影像校正...")},
          {QStringLiteral("en"), QStringLiteral("Orthorectification...")}}},
        {QStringLiteral("action.dem_texture"),
         {{QStringLiteral("zh"), QStringLiteral("DEM 三维贴图...")},
          {QStringLiteral("en"), QStringLiteral("DEM 3D Texturing...")}}},
        {QStringLiteral("action.load_panorama360"),
         {{QStringLiteral("zh"), QStringLiteral("加载 360 街景图...")},
          {QStringLiteral("en"), QStringLiteral("Load 360 Panorama...")}}},
        {QStringLiteral("action.voxel_downsample"),
         {{QStringLiteral("zh"), QStringLiteral("体素降采样...")},
          {QStringLiteral("en"), QStringLiteral("Voxel Downsample...")}}},
        {QStringLiteral("action.statistical_filter"),
         {{QStringLiteral("zh"), QStringLiteral("统计滤波...")},
          {QStringLiteral("en"), QStringLiteral("Statistical Filter...")}}},
        {QStringLiteral("action.pointcloud_to_dem"),
         {{QStringLiteral("zh"), QStringLiteral("点云转 DEM...")},
          {QStringLiteral("en"), QStringLiteral("Point Cloud to DEM...")}}},
        {QStringLiteral("action.export_ply"),
         {{QStringLiteral("zh"), QStringLiteral("导出 PLY...")},
          {QStringLiteral("en"), QStringLiteral("Export PLY...")}}},
        {QStringLiteral("action.delete_single_layer"),
         {{QStringLiteral("zh"), QStringLiteral("删除图层")},
          {QStringLiteral("en"), QStringLiteral("Delete Layer")}}},
        {QStringLiteral("action.export_layer"),
         {{QStringLiteral("zh"), QStringLiteral("导出...")}, {QStringLiteral("en"), QStringLiteral("Export...")}}},
        {QStringLiteral("action.zoom_to_extent"),
         {{QStringLiteral("zh"), QStringLiteral("缩放至范围")},
          {QStringLiteral("en"), QStringLiteral("Zoom to Extent")}}},
        {QStringLiteral("action.properties"),
         {{QStringLiteral("zh"), QStringLiteral("属性")}, {QStringLiteral("en"), QStringLiteral("Properties")}}},
        {QStringLiteral("action.export_group"),
         {{QStringLiteral("zh"), QStringLiteral("导出该分组...")},
          {QStringLiteral("en"), QStringLiteral("Export Group...")}}},
        {QStringLiteral("tree.source_data"),
         {{QStringLiteral("zh"), QStringLiteral("源数据")}, {QStringLiteral("en"), QStringLiteral("Source Data")}}},
        {QStringLiteral("tree.results"),
         {{QStringLiteral("zh"), QStringLiteral("处理结果")},
          {QStringLiteral("en"), QStringLiteral("Processing Results")}}},
        {QStringLiteral("tree.raster"),
         {{QStringLiteral("zh"), QStringLiteral("遥感影像")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Images")}}},
        {QStringLiteral("tree.pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("点云")}, {QStringLiteral("en"), QStringLiteral("Point Cloud")}}},
        {QStringLiteral("tree.panorama360"),
         {{QStringLiteral("zh"), QStringLiteral("360街景")},
          {QStringLiteral("en"), QStringLiteral("360 Panorama")}}},
        {QStringLiteral("tree.group.dem_rebuild"),
         {{QStringLiteral("zh"), QStringLiteral("DEM 重建")},
          {QStringLiteral("en"), QStringLiteral("DEM Reconstruction")}}},
        {QStringLiteral("tree.group.orthorectify"),
         {{QStringLiteral("zh"), QStringLiteral("正射影像校正")},
          {QStringLiteral("en"), QStringLiteral("Orthorectification")}}},
        {QStringLiteral("tree.group.histogram"),
         {{QStringLiteral("zh"), QStringLiteral("灰度直方图")},
          {QStringLiteral("en"), QStringLiteral("Grayscale Histogram")}}},
        {QStringLiteral("tree.group.confusion_matrix"),
         {{QStringLiteral("zh"), QStringLiteral("混淆矩阵精度评价")},
          {QStringLiteral("en"), QStringLiteral("Confusion Matrix Accuracy")}}},
        {QStringLiteral("tree.group.index_temporal"),
         {{QStringLiteral("zh"), QStringLiteral("多时相指数对比")},
          {QStringLiteral("en"), QStringLiteral("Multi-temporal Index Compare")}}},
        {QStringLiteral("geo.title"),
         {{QStringLiteral("zh"), QStringLiteral("坐标信息已读取")},
          {QStringLiteral("en"), QStringLiteral("Coordinate Information Read")}}},
        {QStringLiteral("geo.detected"),
         {{QStringLiteral("zh"), QStringLiteral("已检测到影像包含坐标信息。")},
          {QStringLiteral("en"), QStringLiteral("Coordinate information has been detected in the image.")}}},
        {QStringLiteral("geo.file"),
         {{QStringLiteral("zh"), QStringLiteral("文件:")}, {QStringLiteral("en"), QStringLiteral("File:")}}},
        {QStringLiteral("geo.projection_snippet"),
         {{QStringLiteral("zh"), QStringLiteral("Projection (WKT) snippet:")},
          {QStringLiteral("en"), QStringLiteral("Projection (WKT) snippet:")}}},
        {QStringLiteral("view.select_layer"),
         {{QStringLiteral("zh"), QStringLiteral("请选择一个遥感影像图层或波段。")},
          {QStringLiteral("en"), QStringLiteral("Please select a remote sensing image layer or band.")}}},
        {QStringLiteral("view.no_render"),
         {{QStringLiteral("zh"), QStringLiteral("当前影像没有可显示的渲染结果。\n当前图层：%1")},
          {QStringLiteral("en"),
           QStringLiteral("The current image has no renderable result.\nCurrent layer: %1")}}},
        {QStringLiteral("dialog.select_export_dir"),
         {{QStringLiteral("zh"), QStringLiteral("选择导出文件夹")},
          {QStringLiteral("en"), QStringLiteral("Select Export Folder")}}},
        {QStringLiteral("dialog.load_raster"),
         {{QStringLiteral("zh"), QStringLiteral("加载遥感影像")},
          {QStringLiteral("en"), QStringLiteral("Load Remote Sensing Image")}}},
        {QStringLiteral("dialog.load_pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("加载点云")},
          {QStringLiteral("en"), QStringLiteral("Load Point Cloud")}}},
        {QStringLiteral("dialog.load_mesh"),
         {{QStringLiteral("zh"), QStringLiteral("加载 Mesh")},
          {QStringLiteral("en"), QStringLiteral("Load Mesh")}}},
        {QStringLiteral("dialog.load_dem"),
         {{QStringLiteral("zh"), QStringLiteral("加载 DEM")},
          {QStringLiteral("en"), QStringLiteral("Load DEM")}}},
        {QStringLiteral("dialog.load_panorama"),
         {{QStringLiteral("zh"), QStringLiteral("加载 360 街景图")},
          {QStringLiteral("en"), QStringLiteral("Load 360 Panorama")}}},
        {QStringLiteral("dialog.export_layer"),
         {{QStringLiteral("zh"), QStringLiteral("导出图层")},
          {QStringLiteral("en"), QStringLiteral("Export Layer")}}},
        {QStringLiteral("dialog.export_dem"),
         {{QStringLiteral("zh"), QStringLiteral("导出 DEM")},
          {QStringLiteral("en"), QStringLiteral("Export DEM")}}},
        {QStringLiteral("prop.title"),
         {{QStringLiteral("zh"), QStringLiteral("图层属性 - %1")},
          {QStringLiteral("en"), QStringLiteral("Layer Properties - %1")}}},
        {QStringLiteral("prop.name"),
         {{QStringLiteral("zh"), QStringLiteral("名称:")}, {QStringLiteral("en"), QStringLiteral("Name:")}}},
        {QStringLiteral("prop.path"),
         {{QStringLiteral("zh"), QStringLiteral("路径:")}, {QStringLiteral("en"), QStringLiteral("Path:")}}},
        {QStringLiteral("prop.type"),
         {{QStringLiteral("zh"), QStringLiteral("类型:")}, {QStringLiteral("en"), QStringLiteral("Type:")}}},
        {QStringLiteral("prop.visible"),
         {{QStringLiteral("zh"), QStringLiteral("可见:")}, {QStringLiteral("en"), QStringLiteral("Visible:")}}},
        {QStringLiteral("prop.yes"),
         {{QStringLiteral("zh"), QStringLiteral("是")}, {QStringLiteral("en"), QStringLiteral("Yes")}}},
        {QStringLiteral("prop.no"),
         {{QStringLiteral("zh"), QStringLiteral("否")}, {QStringLiteral("en"), QStringLiteral("No")}}},
        {QStringLiteral("prop.bands"),
         {{QStringLiteral("zh"), QStringLiteral("波段数:")}, {QStringLiteral("en"), QStringLiteral("Band count:")}}},
        {QStringLiteral("prop.size_pixels"),
         {{QStringLiteral("zh"), QStringLiteral("尺寸: %1 x %2 像素")},
          {QStringLiteral("en"), QStringLiteral("Size: %1 x %2 pixels")}}},
        {QStringLiteral("prop.projection"),
         {{QStringLiteral("zh"), QStringLiteral("投影:")}, {QStringLiteral("en"), QStringLiteral("Projection:")}}},
        {QStringLiteral("prop.unknown"),
         {{QStringLiteral("zh"), QStringLiteral("(未知)")}, {QStringLiteral("en"), QStringLiteral("(unknown)")}}},
        {QStringLiteral("prop.point_count"),
         {{QStringLiteral("zh"), QStringLiteral("点数:")}, {QStringLiteral("en"), QStringLiteral("Point count:")}}},
        {QStringLiteral("prop.vertices"),
         {{QStringLiteral("zh"), QStringLiteral("顶点数:")}, {QStringLiteral("en"), QStringLiteral("Vertices:")}}},
        {QStringLiteral("prop.triangles"),
         {{QStringLiteral("zh"), QStringLiteral("三角面:")}, {QStringLiteral("en"), QStringLiteral("Triangles:")}}},
        {QStringLiteral("prop.size"),
         {{QStringLiteral("zh"), QStringLiteral("尺寸: %1 x %2")},
          {QStringLiteral("en"), QStringLiteral("Size: %1 x %2")}}},
        {QStringLiteral("prop.summary"),
         {{QStringLiteral("zh"), QStringLiteral("摘要:")}, {QStringLiteral("en"), QStringLiteral("Summary:")}}},
        {QStringLiteral("type.raster"),
         {{QStringLiteral("zh"), QStringLiteral("遥感影像")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Image")}}},
        {QStringLiteral("type.pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("点云")}, {QStringLiteral("en"), QStringLiteral("Point Cloud")}}},
        {QStringLiteral("type.mesh"),
         {{QStringLiteral("zh"), QStringLiteral("网格模型")},
          {QStringLiteral("en"), QStringLiteral("Mesh Model")}}},
        {QStringLiteral("type.dem"),
         {{QStringLiteral("zh"), QStringLiteral("数字高程模型")},
          {QStringLiteral("en"), QStringLiteral("Digital Elevation Model")}}},
        {QStringLiteral("type.panorama360"),
         {{QStringLiteral("zh"), QStringLiteral("360 街景图")},
          {QStringLiteral("en"), QStringLiteral("360 Panorama")}}},
        {QStringLiteral("type.result"),
         {{QStringLiteral("zh"), QStringLiteral("处理结果")},
          {QStringLiteral("en"), QStringLiteral("Processing Result")}}},
        {QStringLiteral("splash.title"),
         {{QStringLiteral("zh"), QStringLiteral("遥感影像处理平台")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Image Platform")}}},
        {QStringLiteral("splash.starting"),
         {{QStringLiteral("zh"), QStringLiteral("正在启动")},
          {QStringLiteral("en"), QStringLiteral("Starting")}}},
        {QStringLiteral("help.title"),
         {{QStringLiteral("zh"), QStringLiteral("使用指南")},
          {QStringLiteral("en"), QStringLiteral("User Guide")}}},
    };
    return table;
}

} // namespace

Translation &Translation::instance() {
    static Translation self;
    return self;
}

Translation::Translation(QObject *parent) : QObject(parent) {}

AppLanguage Translation::language() const {
    return language_;
}

void Translation::loadSavedLanguage() {
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh")).toString();
    language_ = saved == QStringLiteral("en") ? AppLanguage::English : AppLanguage::Chinese;
}

void Translation::setLanguage(AppLanguage language) {
    if (language_ == language) {
        return;
    }
    language_ = language;
    saveLanguage();
    emit languageChanged();
}

void Translation::saveLanguage() const {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/language"),
                      language_ == AppLanguage::English ? QStringLiteral("en") : QStringLiteral("zh"));
}

QString Translation::tr(const QString &key) const {
    const auto &table = catalog();
    if (!table.contains(key)) {
        return key;
    }
    const auto suffix = langSuffix(language_);
    return table.value(key).value(suffix, key);
}

QString Translation::helpGuideHtml() const {
    if (language_ == AppLanguage::English) {
        return QStringLiteral(R"(
<h2>Remote Sensing Qt Starter — User Guide</h2>
<h3>1. Quick Start</h3>
<ol>
<li>Use <b>Data</b> menu to load raster, point cloud, mesh, or DEM files.</li>
<li>Select a layer in the left <b>Project Layers</b> tree.</li>
<li>View rasters in the <b>2D Image</b> tab; view point clouds/mesh in <b>3D Scene</b>.</li>
<li>Check processing messages in the bottom <b>Log</b> panel.</li>
</ol>
<h3>2. Raster Processing</h3>
<ul>
<li><b>Band &amp; Rendering</b>: configure band combination and color mapping.</li>
<li><b>Enhancement</b>: histogram equalization, stretch, filters, sharpening.</li>
<li><b>Features / Classification</b>: ORB/SIFT, edge detection, K-Means, SVM, etc.</li>
<li><b>Remote Sensing Indices</b>: NDVI, NDWI, NDBI and temporal comparison.</li>
</ul>
<h3>3. 3D &amp; Point Cloud</h3>
<ul>
<li>Load point cloud or mesh into the 3D tab.</li>
<li>Use <b>Point Cloud</b> menu for downsampling, filtering, DEM conversion, PLY export.</li>
<li>Use <b>Photogrammetry / 3D</b> for DEM reconstruction and orthorectification.</li>
</ul>
<h3>4. Panorama / Street View</h3>
<p>Load a 360° panorama image from <b>Street View / Panorama</b> and browse it in the dedicated tab.</p>
<h3>5. AI Assistant</h3>
<p>Open <b>AI → Show AI Assistant</b> to attach layer information and ask questions about imported data.</p>
<h3>6. Settings</h3>
<p>Click the <b>Settings</b> button at the top-right corner. Use the <b>General</b> tab to switch language or theme color, and the <b>User Guide</b> tab for this manual.</p>
<h3>7. Tips</h3>
<ul>
<li>Ctrl + mouse wheel on the 2D view zooms the image.</li>
<li>Multiple layers can be selected with Ctrl/Shift in the layer tree.</li>
<li>Results are added as new layers under the project tree.</li>
</ul>
)");
    }

    return QStringLiteral(R"(
<h2>遥感影像处理平台 — 使用指南</h2>
<h3>1. 快速上手</h3>
<ol>
<li>在 <b>数据</b> 菜单中加载遥感影像、点云、Mesh 或 DEM。</li>
<li>在左侧 <b>工程图层</b> 树中选择图层。</li>
<li>在 <b>二维影像</b> 标签页查看栅格；在 <b>三维场景</b> 查看点云/Mesh。</li>
<li>在底部 <b>日志</b> 面板查看处理过程与提示信息。</li>
</ol>
<h3>2. 影像处理</h3>
<ul>
<li><b>波段与设色</b>：设置波段组合与显示配色。</li>
<li><b>增强</b>：直方图均衡、拉伸、滤波、锐化等。</li>
<li><b>特征与检测 / 分类</b>：ORB/SIFT、边缘检测、K-Means、SVM 等。</li>
<li><b>遥感指数</b>：NDVI、NDWI、NDBI 及多时相对比。</li>
</ul>
<h3>3. 三维与点云</h3>
<ul>
<li>加载点云或 Mesh 后在三维标签页浏览。</li>
<li><b>点云处理</b>：降采样、滤波、转 DEM、导出 PLY。</li>
<li><b>摄影测量/三维</b>：DEM 重建、正射校正、DEM 贴图等。</li>
</ul>
<h3>4. 街景 / 全景</h3>
<p>在 <b>街景/全景</b> 菜单加载 360° 全景图，并在对应标签页中查看。</p>
<h3>5. AI 助手</h3>
<p>通过 <b>AI → 显示 AI 助手</b> 打开面板，可附带图层信息并进行问答。</p>
<h3>6. 设置</h3>
<p>点击窗口右上角的 <b>设置</b> 按钮。在 <b>常规</b> 标签页切换语言或主题颜色，在 <b>使用指南</b> 标签页查看本说明。</p>
<h3>7. 小技巧</h3>
<ul>
<li>二维视图中按住 Ctrl 并滚动鼠标滚轮可缩放影像。</li>
<li>图层树中可用 Ctrl/Shift 多选图层。</li>
<li>处理结果会以新图层形式加入工程。</li>
</ul>
)");
}

} // namespace rs

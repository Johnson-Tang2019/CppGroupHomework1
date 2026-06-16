#include "rs/Translation.h"

#include <QHash>
#include <QSettings>

namespace rs {

namespace {

QString langSuffix(AppLanguage language) {
    return language == AppLanguage::English ? QStringLiteral("en") : QStringLiteral("zh");
}

using LangMap = QHash<QString, QString>;
using Catalog = QHash<QString, LangMap>;

const Catalog &catalog() {
    static const Catalog table = {
        {QStringLiteral("window_title"),
         {{QStringLiteral("zh"), QStringLiteral("Remote Sensing Qt Starter")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Qt Starter")}}},

        {QStringLiteral("menu.data"),
         {{QStringLiteral("zh"), QStringLiteral("数据")},
          {QStringLiteral("en"), QStringLiteral("Data")}}},
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
         {{QStringLiteral("zh"), QStringLiteral("AI")},
          {QStringLiteral("en"), QStringLiteral("AI")}}},
        {QStringLiteral("menu.settings"),
         {{QStringLiteral("zh"), QStringLiteral("设置")},
          {QStringLiteral("en"), QStringLiteral("Settings")}}},
        {QStringLiteral("menu.help"),
         {{QStringLiteral("zh"), QStringLiteral("帮助")},
          {QStringLiteral("en"), QStringLiteral("Help")}}},
        {QStringLiteral("menu.raster.band"),
         {{QStringLiteral("zh"), QStringLiteral("波段与设色")},
          {QStringLiteral("en"), QStringLiteral("Band & Rendering")}}},
        {QStringLiteral("menu.raster.stat"),
         {{QStringLiteral("zh"), QStringLiteral("统计")},
          {QStringLiteral("en"), QStringLiteral("Statistics")}}},
        {QStringLiteral("menu.raster.enhance"),
         {{QStringLiteral("zh"), QStringLiteral("增强")},
          {QStringLiteral("en"), QStringLiteral("Enhancement")}}},
        {QStringLiteral("menu.raster.feature"),
         {{QStringLiteral("zh"), QStringLiteral("特征与检测")},
          {QStringLiteral("en"), QStringLiteral("Features & Detection")}}},
        {QStringLiteral("menu.raster.classify"),
         {{QStringLiteral("zh"), QStringLiteral("分类与检测")},
          {QStringLiteral("en"), QStringLiteral("Classification & Detection")}}},

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
        {QStringLiteral("action.load_panorama360"),
         {{QStringLiteral("zh"), QStringLiteral("加载 360 街景图...")},
          {QStringLiteral("en"), QStringLiteral("Load 360 Panorama...")}}},
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
         {{QStringLiteral("zh"), QStringLiteral("导出...")},
          {QStringLiteral("en"), QStringLiteral("Export...")}}},
        {QStringLiteral("action.zoom_to_extent"),
         {{QStringLiteral("zh"), QStringLiteral("缩放至范围")},
          {QStringLiteral("en"), QStringLiteral("Zoom to Extent")}}},
        {QStringLiteral("action.properties"),
         {{QStringLiteral("zh"), QStringLiteral("属性")},
          {QStringLiteral("en"), QStringLiteral("Properties")}}},
        {QStringLiteral("action.export_group"),
         {{QStringLiteral("zh"), QStringLiteral("导出该分组...")},
          {QStringLiteral("en"), QStringLiteral("Export Group...")}}},

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
         {{QStringLiteral("zh"), QStringLiteral("日志")},
          {QStringLiteral("en"), QStringLiteral("Log")}}},
        {QStringLiteral("tab.ai"),
         {{QStringLiteral("zh"), QStringLiteral("AI 助手")},
          {QStringLiteral("en"), QStringLiteral("AI Assistant")}}},

        {QStringLiteral("settings.tab.general"),
         {{QStringLiteral("zh"), QStringLiteral("常规")},
          {QStringLiteral("en"), QStringLiteral("General")}}},
        {QStringLiteral("settings.tab.guide"),
         {{QStringLiteral("zh"), QStringLiteral("使用指南")},
          {QStringLiteral("en"), QStringLiteral("User Guide")}}},
        {QStringLiteral("settings.title"),
         {{QStringLiteral("zh"), QStringLiteral("设置")},
          {QStringLiteral("en"), QStringLiteral("Settings")}}},
        {QStringLiteral("settings.language"),
         {{QStringLiteral("zh"), QStringLiteral("界面语言")},
          {QStringLiteral("en"), QStringLiteral("Interface Language")}}},
        {QStringLiteral("settings.theme"),
         {{QStringLiteral("zh"), QStringLiteral("主题颜色")},
          {QStringLiteral("en"), QStringLiteral("Theme Color")}}},
        {QStringLiteral("settings.button"),
         {{QStringLiteral("zh"), QStringLiteral("设置")},
          {QStringLiteral("en"), QStringLiteral("Settings")}}},
        {QStringLiteral("settings.lang.zh"),
         {{QStringLiteral("zh"), QStringLiteral("中文")},
          {QStringLiteral("en"), QStringLiteral("Chinese")}}},
        {QStringLiteral("settings.lang.en"),
         {{QStringLiteral("zh"), QStringLiteral("English")},
          {QStringLiteral("en"), QStringLiteral("English")}}},
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

        {QStringLiteral("tree.source_data"),
         {{QStringLiteral("zh"), QStringLiteral("源数据")},
          {QStringLiteral("en"), QStringLiteral("Source Data")}}},
        {QStringLiteral("tree.results"),
         {{QStringLiteral("zh"), QStringLiteral("处理结果")},
          {QStringLiteral("en"), QStringLiteral("Processing Results")}}},
        {QStringLiteral("tree.raster"),
         {{QStringLiteral("zh"), QStringLiteral("遥感影像")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Images")}}},
        {QStringLiteral("tree.pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("点云")},
          {QStringLiteral("en"), QStringLiteral("Point Cloud")}}},
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
         {{QStringLiteral("zh"), QStringLiteral("文件:")},
          {QStringLiteral("en"), QStringLiteral("File:")}}},
        {QStringLiteral("geo.projection_snippet"),
         {{QStringLiteral("zh"), QStringLiteral("Projection (WKT) snippet:")},
          {QStringLiteral("en"), QStringLiteral("Projection (WKT) snippet:")}}},
        {QStringLiteral("view.select_layer"),
         {{QStringLiteral("zh"), QStringLiteral("请选择一个遥感影像图层或波段。")},
          {QStringLiteral("en"), QStringLiteral("Please select a remote sensing image layer or band.")}}},
        {QStringLiteral("view.no_render"),
         {{QStringLiteral("zh"), QStringLiteral("当前影像没有可显示的渲染结果。\n当前图层：%1")},
          {QStringLiteral("en"), QStringLiteral("The current image has no renderable result.\nCurrent layer: %1")}}},
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
         {{QStringLiteral("zh"), QStringLiteral("名称:")},
          {QStringLiteral("en"), QStringLiteral("Name:")}}},
        {QStringLiteral("prop.path"),
         {{QStringLiteral("zh"), QStringLiteral("路径:")},
          {QStringLiteral("en"), QStringLiteral("Path:")}}},
        {QStringLiteral("prop.type"),
         {{QStringLiteral("zh"), QStringLiteral("类型:")},
          {QStringLiteral("en"), QStringLiteral("Type:")}}},
        {QStringLiteral("prop.visible"),
         {{QStringLiteral("zh"), QStringLiteral("可见:")},
          {QStringLiteral("en"), QStringLiteral("Visible:")}}},
        {QStringLiteral("prop.yes"),
         {{QStringLiteral("zh"), QStringLiteral("是")},
          {QStringLiteral("en"), QStringLiteral("Yes")}}},
        {QStringLiteral("prop.no"),
         {{QStringLiteral("zh"), QStringLiteral("否")},
          {QStringLiteral("en"), QStringLiteral("No")}}},
        {QStringLiteral("prop.bands"),
         {{QStringLiteral("zh"), QStringLiteral("波段数:")},
          {QStringLiteral("en"), QStringLiteral("Band count:")}}},
        {QStringLiteral("prop.size_pixels"),
         {{QStringLiteral("zh"), QStringLiteral("尺寸: %1 x %2 像素")},
          {QStringLiteral("en"), QStringLiteral("Size: %1 x %2 pixels")}}},
        {QStringLiteral("prop.projection"),
         {{QStringLiteral("zh"), QStringLiteral("投影:")},
          {QStringLiteral("en"), QStringLiteral("Projection:")}}},
        {QStringLiteral("prop.unknown"),
         {{QStringLiteral("zh"), QStringLiteral("(未知)")},
          {QStringLiteral("en"), QStringLiteral("(unknown)")}}},
        {QStringLiteral("prop.point_count"),
         {{QStringLiteral("zh"), QStringLiteral("点数:")},
          {QStringLiteral("en"), QStringLiteral("Point count:")}}},
        {QStringLiteral("prop.vertices"),
         {{QStringLiteral("zh"), QStringLiteral("顶点数:")},
          {QStringLiteral("en"), QStringLiteral("Vertices:")}}},
        {QStringLiteral("prop.triangles"),
         {{QStringLiteral("zh"), QStringLiteral("三角面:")},
          {QStringLiteral("en"), QStringLiteral("Triangles:")}}},
        {QStringLiteral("prop.size"),
         {{QStringLiteral("zh"), QStringLiteral("尺寸: %1 x %2")},
          {QStringLiteral("en"), QStringLiteral("Size: %1 x %2")}}},
        {QStringLiteral("prop.summary"),
         {{QStringLiteral("zh"), QStringLiteral("摘要:")},
          {QStringLiteral("en"), QStringLiteral("Summary:")}}},

        {QStringLiteral("type.raster"),
         {{QStringLiteral("zh"), QStringLiteral("遥感影像")},
          {QStringLiteral("en"), QStringLiteral("Remote Sensing Image")}}},
        {QStringLiteral("type.pointcloud"),
         {{QStringLiteral("zh"), QStringLiteral("点云")},
          {QStringLiteral("en"), QStringLiteral("Point Cloud")}}},
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
        {QStringLiteral("log.startup"),
         {{QStringLiteral("zh"), QStringLiteral("Starter 已启动：当前版本提供 GDAL 多波段、参数化算法、DEM/正射流程的工程框架。")},
          {QStringLiteral("en"), QStringLiteral("Starter launched: this build provides GDAL multi-band workflows, parametric algorithms, and DEM/orthorectification scaffolding.")}}},
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
<h2>Remote Sensing Qt Starter - User Guide</h2>
<ol>
<li>Use the Data menu to load raster, point cloud, mesh, DEM, or panorama files.</li>
<li>Select layers in the Project Layers tree.</li>
<li>Use Raster Processing and Remote Sensing Indices for rendering and analysis.</li>
<li>Use the AI Assistant tab to analyze the currently displayed image or scene.</li>
</ol>
)");
    }

    return QStringLiteral(R"(
<h2>遥感影像处理平台 - 使用指南</h2>
<ol>
<li>使用“数据”菜单加载遥感影像、点云、Mesh、DEM 或 360 街景图。</li>
<li>在左侧“工程图层”树中选择和管理图层。</li>
<li>使用“影像处理”和“遥感指数”菜单进行波段渲染、增强、分类和指数计算。</li>
<li>在底部“AI 助手”标签页中分析当前显示的二维影像、三维场景或街景图。</li>
</ol>
)");
}

} // namespace rs

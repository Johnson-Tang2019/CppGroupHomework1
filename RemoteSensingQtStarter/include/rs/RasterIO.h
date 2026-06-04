#pragma once // 防止头文件被重复包含

#include "rs/Geometry.h" // RasterLayer、DemLayer 等图层类型定义

#include <QByteArray>
#include <QFileInfo>
#include <QString> // Qt 字符串，表示文件路径
#include <algorithm>
#include <cmath>
#ifdef RS_WITH_GDAL
#include <cpl_string.h>
#include <cstring>
#include <gdal_priv.h>
#endif
#include <limits>
#include <memory> // std::shared_ptr，智能指针管理图层对象
#include <stdexcept>
#include <vector>

// 遥感栅格数据 IO 命名空间
// 包含所有 GDAL 读取、写入和渲染相关的函数
namespace rs::io {

// 栅格影像读取时的选项参数
struct RasterReadOptions {
    bool readSamples{true};   // 是否读取像素数据（false=只读元数据，加速预览）
    int previewMaxSize{2048}; // 预览最大尺寸（超过此尺寸的影像会自动降采样读入，单位：像素）
};

// 栅格影像写入时的选项参数
struct RasterWriteOptions {
    QString driverName{QStringLiteral("GTiff")}; // GDAL 驱动名称，默认 GeoTIFF 格式
    QString creationOptions;                     // 驱动创建选项（如压缩方式 "COMPRESS=LZW"）
};

// 使用 GDAL 从文件路径加载栅格影像数据集
// path    - 影像文件路径（支持 .tif/.img/.jp2 等 GDAL 支持的格式）
// options - 读取选项（是否读像素数据、预览尺寸等）
// 返回：RasterLayer 智能指针，包含波段信息、投影和地理变换
std::shared_ptr<RasterLayer> loadRasterDataset(const QString &path,
                                               const RasterReadOptions &options = {});

// 将指定波段渲染为灰度图（QImage）
// raster            - 输入的栅格影像图层
// zeroBasedBandIndex - 波段索引（从 0 开始）
// 返回：灰度 QImage，每个像素为 8 位灰度值
QImage renderSingleBandGray(const RasterLayer &raster, int zeroBasedBandIndex);

// 将三个波段组合渲染为 RGB 彩色图（QImage）
// raster     - 输入的栅格影像图层
// redBand    - 红色波段的索引
// greenBand  - 绿色波段的索引
// blueBand   - 蓝色波段的索引
// 返回：RGB 彩色 QImage
QImage renderRgbComposite(const RasterLayer &raster, int redBand, int greenBand, int blueBand);

// 将 DEM 高程数据导出为 GeoTIFF 文件
// dem     - 待导出的数字高程模型图层
// path    - 输出文件路径（通常后缀为 .tif）
// options - 写入选项（驱动名称、创建选项等）
void exportDemAsGeoTiff(const DemLayer &dem, const QString &path,
                        const RasterWriteOptions &options = {});

} // namespace rs::io

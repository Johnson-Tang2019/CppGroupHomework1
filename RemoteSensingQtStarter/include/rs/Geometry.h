#pragma once  // 防止头文件被重复包含

#include "rs/DataObject.h"  // 基类 DataObject 定义，所有图层类都继承自它

#include <array>       // std::array，用于存储地理变换的 6 参数数组
#include <QImage>      // Qt 图像类，用于存储影像的显示位图（RGB 合成图）
#include <QStringList> // Qt 字符串列表，用于存储多个字符串
#include <QVector>     // Qt 动态数组，用于存储波段数据、点云点、顶点等
#include <QVector3D>   // Qt 三维向量，表示点云或网格中的三维点坐标

namespace rs {  // 遥感（Remote Sensing）命名空间

// 三维网格的三角面片，由三个顶点索引构成
struct Face {
    int a {};  // 第一个顶点的索引（0-based）
    int b {};  // 第二个顶点的索引
    int c {};  // 第三个顶点的索引
};

// 栅格影像的单个波段信息
struct RasterBand {
    QString name;           // 波段名称（如 "Red", "Green", "Blue" 或 "Band 1"）
    int width {};           // 该波段的像素宽度（列数）
    int height {};          // 该波段的像素高度（行数）
    double noDataValue {};  // 无效像元值（用于标识无数据区域）
    bool hasNoDataValue {}; // 是否定义了 NoData 值（true=有无效值）
    float minValue {};      // 该波段像素最小值（用于直方图统计和拉伸显示）
    float maxValue {};      // 该波段像素最大值
    QVector<float> samples; // 原始像素数据（一维数组，行优先存储，float 精度）

    // 检查是否已加载实际的像素数据
    bool hasSamples() const { return width > 0 && height > 0 && !samples.isEmpty(); }
};

// ============================================================================
// RasterLayer：遥感栅格影像图层
// 支持多波段数据、投影信息、地理变换、显示图像缓存
// ============================================================================
class RasterLayer final : public DataObject {  // final 禁止进一步继承
public:
    // 构造函数
    // name - 图层显示名，path - 文件路径
    // bands - 波段数据列表（可选，GDAL 读取后填充）
    // displayImage - 当前显示的 RGB 合成图像（可选）
    RasterLayer(QString name, QString path, QVector<RasterBand> bands = {}, QImage displayImage = {})
        : DataObject(std::move(name), std::move(path), DataType::Raster),  // 调用基类构造，类型为 Raster
          bands_(std::move(bands)),
          displayImage_(std::move(displayImage)) {}

    // ---- 波段信息查询 ----
    int bandCount() const { return bands_.size(); }              // 获取波段总数
    bool hasRasterBands() const { return !bands_.isEmpty(); }    // 是否已有波段数据
    const QVector<RasterBand>& bands() const { return bands_; }  // 获取所有波段的列表引用
    const RasterBand& band(int zeroBasedIndex) const { return bands_.at(zeroBasedIndex); }  // 按索引获取单个波段（从 0 开始）

    // ---- 显示图像管理 ----
    const QImage& currentDisplayImage() const { return displayImage_; }  // 获取当前显示的 RGB 合成图
    void setCurrentDisplayImage(QImage image) { displayImage_ = std::move(image); }  // 设置当前显示的图像

    // ---- 投影信息 ----
    QString projection() const { return projection_; }              // 获取投影坐标系描述（如 "EPSG:4326"）
    void setProjection(QString projection) { projection_ = std::move(projection); }  // 设置投影

    // ---- 地理变换参数 ----
    // 返回 6 参数数组 [X_origin, X_res, X_skew, Y_origin, Y_skew, Y_res]
    // 用于将像素坐标 (col, row) 转换为地理坐标 (X, Y)
    std::array<double, 6> geoTransform() const { return geoTransform_; }
    void setGeoTransform(const std::array<double, 6>& transform) { geoTransform_ = transform; }

    // ---- 渲染描述 ----
    QString renderDescription() const { return renderDescription_; }  // 获取当前波段组合/设色方式的描述
    void setRenderDescription(QString description) { renderDescription_ = std::move(description); }  // 设置渲染描述

    // 实现基类的纯虚函数：返回图层的摘要信息
    QString summary() const override {
        if (!bands_.isEmpty()) {  // 如果已有波段数据
            return QStringLiteral("%1 x %2, %3 bands")  // 返回 "宽 x 高, N bands"
                .arg(bands_.front().width)
                .arg(bands_.front().height)
                .arg(bands_.size());
        }
        return QStringLiteral("GDAL dataset TODO");  // 未读取 GDAL 数据时的占位描述
    }

private:
    QVector<RasterBand> bands_;      // 所有波段的像素数据列表
    QImage displayImage_;            // 当前显示的 RGB 合成图像（缓存）
    QString projection_;             // 投影坐标系描述字符串
    std::array<double, 6> geoTransform_ {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};  // 地理变换参数（默认单位矩阵）
    QString renderDescription_ {QStringLiteral("未设色")};  // 当前渲染方式描述，默认"未设色"
};

// ============================================================================
// PointCloudLayer：三维点云图层
// 存储点云的所有三维点坐标
// ============================================================================
class PointCloudLayer final : public DataObject {
public:
    // 构造函数：name-名称，path-路径，points-三维点坐标列表
    PointCloudLayer(QString name, QString path, QVector<QVector3D> points)
        : DataObject(std::move(name), std::move(path), DataType::PointCloud),  // 类型为 PointCloud
          points_(std::move(points)) {}

    const QVector<QVector3D>& points() const { return points_; }  // 获取所有点坐标的列表
    QString summary() const override { return QStringLiteral("%1 points").arg(points_.size()); }  // 返回点数摘要

private:
    QVector<QVector3D> points_;  // 存储所有三维点坐标 (x, y, z)
};

// ============================================================================
// MeshLayer：三维网格图层（三角面片模型）
// 存储顶点列表和三角面片索引
// ============================================================================
class MeshLayer final : public DataObject {
public:
    // 构造函数：name-名称，path-路径，vertices-顶点列表，faces-三角面片列表
    MeshLayer(QString name, QString path, QVector<QVector3D> vertices, QVector<Face> faces)
        : DataObject(std::move(name), std::move(path), DataType::Mesh),  // 类型为 Mesh
          vertices_(std::move(vertices)),
          faces_(std::move(faces)) {}

    const QVector<QVector3D>& vertices() const { return vertices_; }  // 获取所有顶点坐标
    const QVector<Face>& faces() const { return faces_; }              // 获取所有三角面片
    QString summary() const override {
        return QStringLiteral("%1 vertices, %2 faces").arg(vertices_.size()).arg(faces_.size());  // 返回顶点数和面数
    }

private:
    QVector<QVector3D> vertices_;  // 所有顶点的三维坐标列表
    QVector<Face> faces_;          // 所有三角面片的顶点索引列表
};

// ============================================================================
// DemLayer：数字高程模型图层
// 存储高程网格数据（宽度 x 高度 的浮点数组）
// ============================================================================
class DemLayer final : public DataObject {
public:
    // 构造函数：name-名称，path-路径，width-格网宽度，height-格网高度，elevations-高程值数组
    DemLayer(QString name, QString path, int width, int height, QVector<float> elevations)
        : DataObject(std::move(name), std::move(path), DataType::Dem),  // 类型为 Dem
          width_(width),
          height_(height),
          elevations_(std::move(elevations)) {}

    int width() const { return width_; }                       // 获取 DEM 格网的宽度（列数）
    int height() const { return height_; }                     // 获取 DEM 格网的高度（行数）
    const QVector<float>& elevations() const { return elevations_; }  // 获取高程数据数组（行优先）
    QString sourceRasterPath() const { return sourceRasterPath_; }    // 获取生成此 DEM 的原始影像路径
    void setSourceRasterPath(QString path) { sourceRasterPath_ = std::move(path); }  // 设置原始影像路径
    std::array<double, 6> geoTransform() const { return geoTransform_; }  // 获取地理变换参数
    void setGeoTransform(const std::array<double, 6>& transform) { geoTransform_ = transform; }  // 设置地理变换
    QString summary() const override { return QStringLiteral("%1 x %2 DEM").arg(width_).arg(height_); }  // 返回格网大小摘要

private:
    int width_ {};                    // DEM 格网宽度（列数）
    int height_ {};                   // DEM 格网高度（行数）
    QVector<float> elevations_;       // 高程值数组（长度 = width * height，行优先排列）
    QString sourceRasterPath_;        // 生成此 DEM 的源栅格影像路径（用于追溯数据来源）
    std::array<double, 6> geoTransform_ {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};  // 地理变换（默认单位矩阵）
};

} // namespace rs

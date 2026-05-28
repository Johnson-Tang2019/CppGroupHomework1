#pragma once  // 防止头文件被多次包含

#include <QString>    // Qt 字符串类，用于存储文件名和路径等文本信息

#include <utility>    // std::move，用于高效转移资源所有权

namespace rs {        // 遥感（Remote Sensing）命名空间

// 图层数据类型枚举，用于区分不同种类的图层
enum class DataType {
    Raster,      // 遥感栅格影像（多波段 GeoTIFF/IMG 等）
    PointCloud,  // 点云数据（PLY/LAS/XYZ 等格式的三维点集）
    Mesh,        // 三维网格模型（OBJ/PLY 等格式的三角网）
    Dem,         // 数字高程模型（GeoTIFF/ASCII Grid 等格式）
    Result       // 算法处理结果（直方图、均衡化影像等）
};

// 数据对象基类，所有图层类型的共同父类
// 存储每个图层的基本信息：名称、路径、类型、可见性
class DataObject {
public:
    // 构造函数：需要提供名称、路径和类型
    // std::move 避免不必要的字符串拷贝，提高性能
    DataObject(QString name, QString path, DataType type)
        : name_(std::move(name)), path_(std::move(path)), type_(type) {}

    virtual ~DataObject() = default;  // 虚析构函数，确保子类对象能正确析构

    // ---- 只读访问器（getter） ----
    const QString& name() const { return name_; }     // 获取图层显示名称
    const QString& path() const { return path_; }     // 获取文件路径（或来源标识）
    DataType type() const { return type_; }           // 获取图层数据类型
    bool visible() const { return visible_; }         // 获取图层可见状态（true=显示）

    // ---- 可写访问器（setter） ----
    void setVisible(bool visible) { visible_ = visible; }  // 设置图层可见性（用于图层树勾选切换）

    // 纯虚函数：返回图层的简要描述信息
    // 子类必须实现，例如 "3 Bands, 1024x1024" 或 "1.2M Points"
    virtual QString summary() const = 0;

private:
    QString name_;       // 图层显示名称（如 "IMG_2024.tif"）
    QString path_;       // 文件路径（如 "C:/data/IMG_2024.tif"）
    DataType type_;      // 数据类型（Raster/PointCloud/Mesh/Dem/Result）
    bool visible_ {true};  // 可见性标志，默认为 true（显示状态）
};

} // namespace rs

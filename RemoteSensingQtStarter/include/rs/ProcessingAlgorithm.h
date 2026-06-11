#pragma once  // 防止头文件被重复包含

#include "rs/Geometry.h"  // RasterLayer 定义，算法处理的输入数据类型

#include <QImage>        // Qt 图像类，用于存储算法输出的结果图像
#include <QString>       // Qt 字符串，用于参数名称和描述文本
#include <QVariantMap>   // Qt 泛型映射表，存储参数名到参数值的键值对
#include <QVector3D>     // Qt 三维向量，用于点云数据传递
#include <memory>        // std::shared_ptr，用于 DemLayer 等共享指针
#include <vector>        // std::vector，用于存储参数定义列表

namespace rs {  // 遥感（Remote Sensing）命名空间

// 算法参数的描述结构体
// 用于定义算法需要哪些参数，每个参数的名称、默认值和说明
struct AlgorithmParameter {
    QString key;             // 参数内部标识名（如 "bins", "method"），用于代码中引用
    QString displayName;     // 参数在界面上显示的标签（如 "分箱数", "方法"）
    QString defaultValue;    // 参数默认值（字符串形式，如 "256", "ORB"）
    QString description;     // 参数的工具提示说明（如 "直方图统计区间数量"）
};

// 算法执行时的上下文信息
// 包含当前选中的波段索引和用户设置的参数值
struct ProcessingContext {
    int bandIndex {-1};     // 当前选中的波段索引（-1 表示未选中具体波段或处理 RGB 合成图）
    QVariantMap parameters;  // 用户通过对话框设置的参数值（键=参数key，值=用户输入的值）

    // ── 多输入算法所需的辅助数据 ──
    // 算法按需读取；不需要时为 nullptr / 空
    const class RasterLayer* auxiliaryRaster {nullptr}; // 第二个栅格（如立体像对的右影像）
    const class DemLayer*     auxiliaryDem    {nullptr}; // DEM 输入（正射校正 / DEM 重建等）
    const QVector<QVector3D>* pointCloudData  {nullptr}; // 点云数据（点云算法需要）
};

// 算法处理的结果
// 包含输出图像和结果消息（成功描述或错误提示）
struct ProcessingResult {
    QImage image;    // 算法输出的结果图像（如均衡化后的影像、特征提取结果图等）
    QString message; // 处理结果的消息文本（如 "处理完成" 或错误说明）

    // ── 非图像输出（算法按需填充） ──
    std::shared_ptr<class DemLayer> demResult;       // DEM 输出（DEM 重建 / 点云转 DEM）
    QVector<QVector3D>              pointCloudResult; // 点云输出（体素降采样 / 统计滤波）
};

// 处理算法的抽象基类
// 所有遥感处理算法（直方图、均衡化、特征提取等）都必须继承此类
// 并实现 name()、category()、execute() 三个纯虚函数
class ProcessingAlgorithm {
public:
    virtual ~ProcessingAlgorithm() = default;  // 虚析构函数，确保子类对象正确析构

    // 算法显示名称（如 "灰度直方图"），会出现在 UI 菜单中
    virtual QString name() const = 0;

    // 算法所属分类（如 "影像统计"、"影像增强"），用于菜单分组
    virtual QString category() const = 0;

    // 算法需要的参数定义列表
    // 默认返回空列表（表示该算法不需要额外的参数设置）
    // 子类可重写此函数来定义自己的参数
    virtual std::vector<AlgorithmParameter> parameterSchema() const { return {}; }

    // 执行算法处理
    // input   - 输入的栅格影像数据
    // context - 执行上下文（包含波段索引和用户参数）
    // 返回：处理结果（输出图像 + 消息文本）
    // 纯虚函数，子类必须实现具体的处理逻辑
    virtual ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const = 0;
};

} // namespace rs

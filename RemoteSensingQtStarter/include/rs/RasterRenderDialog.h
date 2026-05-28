#pragma once  // 防止头文件被重复包含

#include "rs/Geometry.h"  // RasterLayer 类型定义，用于获取波段信息

#include <QWidget>        // Qt 窗口基类，对话框的父窗口类型

#include <optional>       // std::optional，表示对话框返回值（用户确认或取消）

namespace rs {  // 遥感（Remote Sensing）命名空间

// 栅格影像渲染模式枚举
// 定义如何将影像数据显示到屏幕上（不同的波段组合方式）
enum class RasterRenderMode {
    AutoRgb,            // 自动 RGB：默认用前三个波段合成（R=Band1, G=Band2, B=Band3）
    RgbBands,           // 手动 RGB：用户指定 R/G/B 各用哪个波段
    SingleBandGray,     // 单波段灰度：只显示一个波段的灰度图
    PseudoColor         // 伪彩色：对单波段数据应用颜色映射表（如热力图）
};

// 用户通过对话框设置的渲染参数
struct RasterRenderRequest {
    RasterRenderMode mode {RasterRenderMode::AutoRgb};  // 渲染模式，默认自动 RGB
    int redBand {0};    // 红色波段索引（仅在 RgbBands 模式下使用）
    int greenBand {1};  // 绿色波段索引
    int blueBand {2};   // 蓝色波段索引
    int grayBand {0};   // 灰度波段索引（仅在 SingleBandGray 模式下使用）
    bool stretchToByte {true};  // 是否将像素值拉伸到 0-255 范围（改善显示效果）
};

// 弹出波段组合/设色设置对话框
// parent - 父窗口指针，用于模态对话框的模态挂载
// raster - 当前选中的影像图层，用于获取波段数量和名称
// 返回：如果用户点击确定，返回 RasterRenderRequest 包含用户的设置
//       如果用户点击取消，返回 std::nullopt
std::optional<RasterRenderRequest> askRasterRenderRequest(QWidget* parent, const RasterLayer& raster);

} // namespace rs

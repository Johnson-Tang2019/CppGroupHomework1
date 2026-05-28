#include "rs/RasterRenderDialog.h"

namespace rs {

// 弹出波段组合/设色设置对话框，返回用户选择的渲染参数
// 参数：parent - 父窗口指针，用于模态对话框
//       raster - 当前选中的影像图层，用于获取波段信息
// 返回：std::optional，有值表示用户确认了设置，无值表示用户取消
// 当前为骨架实现，直接返回默认的 RGB 组合参数（第0、1、2波段）
// TODO: 实现实际的 UI 对话框，让用户选择波段组合和拉伸方式
std::optional<RasterRenderRequest> askRasterRenderRequest(QWidget* parent, const RasterLayer& raster) {
    Q_UNUSED(parent)    // 未使用参数：父窗口指针，留待后续实现对话框时使用
    Q_UNUSED(raster)    // 未使用参数：影像图层，留待后续显示波段列表时使用
    return RasterRenderRequest {};  // 返回默认渲染参数（自动 RGB，0/1/2 波段）
}

} // namespace rs

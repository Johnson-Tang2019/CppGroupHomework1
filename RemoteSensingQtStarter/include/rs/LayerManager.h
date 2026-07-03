#pragma once  // 防止头文件被重复包含

#include <algorithm>  // std::sort、std::unique，用于索引排序和去重
#include <memory>     // std::shared_ptr，管理图层的共享所有权
#include <stdexcept>  // std::invalid_argument、std::out_of_range，用于异常抛出
#include <vector>     // std::vector，动态数组存储图层列表

namespace rs {  // 遥感（Remote Sensing）命名空间

// 图层管理器模板类
// 负责统一管理所有同类型图层的增删查改操作
// 使用 shared_ptr 管理图层生命周期，支持多选删除和索引越界保护
// 模板参数 LayerT 必须是 DataObject 或其子类
template <typename LayerT>
class LayerManager {
public:
    // 智能指针别名：std::shared_ptr<LayerT>，简化书写
    using Ptr = std::shared_ptr<LayerT>;

    // 添加一个图层到管理器末尾
    // 参数 layer：待添加的图层智能指针（不能为空）
    // 返回：新添加图层在管理器中的索引位置
    // 异常：如果 layer 为空指针，抛出 std::invalid_argument
    int add(Ptr layer) {
        if (!layer) {                                    // 检查空指针
            throw std::invalid_argument("layer is null");  // 抛出参数无效异常
        }
        layers_.push_back(std::move(layer));             // 将图层移到容器末尾（避免拷贝）
        return static_cast<int>(layers_.size() - 1);     // 返回刚添加的位置索引
    }

    // 通过索引获取图层
    // 参数 index：图层在管理器中的位置（从 0 开始）
    // 返回：指向该图层的 shared_ptr
    // 异常：如果索引越界，抛出 std::out_of_range
    Ptr at(int index) const {
        if (index < 0 || index >= static_cast<int>(layers_.size())) {  // 检查越界
            throw std::out_of_range("layer index out of range");  // 抛出越界异常
        }
        return layers_.at(static_cast<std::size_t>(index));  // 返回该位置的图层指针
    }

    // 获取所有图层的只读引用（用于遍历）
    const std::vector<Ptr>& all() const { return layers_; }

    // 获取当前图层总数
    int size() const { return static_cast<int>(layers_.size()); }

    // 检查是否没有任何图层
    bool empty() const { return layers_.empty(); }

    // 删除指定索引位置的图层
    // 参数 index：要删除的图层索引
    // 异常：如果索引越界，抛出 std::out_of_range
    void removeAt(int index) {
        if (index < 0 || index >= static_cast<int>(layers_.size())) {  // 检查越界
            throw std::out_of_range("layer index out of range");  // 抛出越界异常
        }
        layers_.erase(layers_.begin() + index);  // 从 vector 中擦除该位置的元素
    }

    // 批量删除多个索引位置的图层
    // 参数 indices：待删除的索引列表（可乱序、可重复，函数内部会排序去重）
    // 使用逆序遍历（rbegin/rend），确保删除靠后的索引不会影响前面索引的位置
    void removeMany(std::vector<int> indices) {
        std::sort(indices.begin(), indices.end());                     // 升序排序
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());  // 去重
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {  // 从大到小逆序遍历
            removeAt(*it);  // 逐个删除（先删末尾再删前面，确保索引不失效）
        }
    }

    // 清空所有图层
    void clear() { layers_.clear(); }

    bool move(int from, int to) {
        if (from < 0 || from >= static_cast<int>(layers_.size()) ||
            to < 0 || to >= static_cast<int>(layers_.size()) || from == to) {
            return false;
        }
        auto layer = std::move(layers_[static_cast<std::size_t>(from)]);
        layers_.erase(layers_.begin() + from);
        layers_.insert(layers_.begin() + to, std::move(layer));
        return true;
    }

    void replaceAll(std::vector<Ptr> layers) { layers_ = std::move(layers); }

private:
    std::vector<Ptr> layers_;  // 图层智能指针的动态数组，实际存储所有图层
};

} // namespace rs

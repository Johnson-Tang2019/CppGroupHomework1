#pragma once

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace rs {

template <typename LayerT>
class LayerManager {
public:
    using Ptr = std::shared_ptr<LayerT>;

    int add(Ptr layer) {
        if (!layer) {
            throw std::invalid_argument("layer is null");
        }
        layers_.push_back(std::move(layer));
        return static_cast<int>(layers_.size() - 1);
    }

    Ptr at(int index) const {
        if (index < 0 || index >= static_cast<int>(layers_.size())) {
            throw std::out_of_range("layer index out of range");
        }
        return layers_.at(static_cast<std::size_t>(index));
    }

    const std::vector<Ptr>& all() const { return layers_; }
    int size() const { return static_cast<int>(layers_.size()); }
    bool empty() const { return layers_.empty(); }

    void removeAt(int index) {
        if (index < 0 || index >= static_cast<int>(layers_.size())) {
            throw std::out_of_range("layer index out of range");
        }
        layers_.erase(layers_.begin() + index);
    }

    void removeMany(std::vector<int> indices) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            removeAt(*it);
        }
    }

    void clear() { layers_.clear(); }

private:
    std::vector<Ptr> layers_;
};

} // namespace rs

#pragma once

#include <QString>

#include <utility>

namespace rs {

enum class DataType {
    Raster,
    PointCloud,
    Mesh,
    Dem,
    Result
};

class DataObject {
public:
    DataObject(QString name, QString path, DataType type)
        : name_(std::move(name)), path_(std::move(path)), type_(type) {}
    virtual ~DataObject() = default;

    const QString& name() const { return name_; }
    const QString& path() const { return path_; }
    DataType type() const { return type_; }
    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

    virtual QString summary() const = 0;

private:
    QString name_;
    QString path_;
    DataType type_;
    bool visible_ {true};
};

} // namespace rs

#pragma once

#include "rs/Geometry.h"

#include <QImage>
#include <QString>
#include <QVariantMap>
#include <vector>

namespace rs {

struct AlgorithmParameter {
    QString key;
    QString displayName;
    QString defaultValue;
    QString description;
};

struct ProcessingContext {
    int bandIndex {-1};
    QVariantMap parameters;
};

struct ProcessingResult {
    QImage image;
    QString message;
};

class ProcessingAlgorithm {
public:
    virtual ~ProcessingAlgorithm() = default;

    virtual QString name() const = 0;
    virtual QString category() const = 0;
    virtual std::vector<AlgorithmParameter> parameterSchema() const { return {}; }
    virtual ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const = 0;
};

} // namespace rs

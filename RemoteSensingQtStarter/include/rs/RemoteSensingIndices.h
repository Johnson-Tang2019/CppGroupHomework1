#pragma once

#include "rs/ProcessingAlgorithm.h"

namespace rs {

class RemoteSensingIndexAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class IndexTemporalCompareAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

} // namespace rs

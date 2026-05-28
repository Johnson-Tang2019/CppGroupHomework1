#pragma once

#include "rs/ProcessingAlgorithm.h"

#include <memory>

namespace rs {

class HistogramAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

class HistogramEqualizationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

class FeatureExtractionAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

class DemReconstructionPipeline {
public:
    struct Inputs {
        QString leftImagePath;
        QString rightImagePath;
        QString cameraFilePath;
        QString controlPointFilePath;
        QString outputDirectory;
    };

    std::shared_ptr<DemLayer> reconstruct(const Inputs& inputs) const;
};

class OrthorectificationPipeline {
public:
    ProcessingResult rectify(const RasterLayer& image, const DemLayer& dem) const;
};

} // namespace rs

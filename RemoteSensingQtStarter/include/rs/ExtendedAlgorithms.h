#pragma once

#include "rs/ProcessingAlgorithm.h"

namespace rs {

class StretchEnhancementAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class ClaheEnhancementAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class DenoiseFilterAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class SharpenEnhancementAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class CannyEdgeAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class KMeansClassificationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class SvmClassificationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class ContourDetectionAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class ConnectedComponentsAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

class ConfusionMatrixAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer &input, const ProcessingContext &context) const override;
};

} // namespace rs

#pragma once

#include "rs/DataObject.h"
#include "rs/Geometry.h"
#include "rs/LayerManager.h"

#include <QAction>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMainWindow>
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>

#include <memory>
#include <vector>

namespace rs {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openRasterDatasets();
    void openPointCloud();
    void openMesh();
    void openDem();
    void deleteSelectedLayers();
    void clearProject();
    void configureRasterRendering();
    void runHistogram();
    void runHistogramEqualization();
    void runFeatureExtraction();
    void runDemReconstruction();
    void runOrthorectification();
    void onSelectionChanged();
    void onLayerItemChanged(QTreeWidgetItem* item, int column);
    void showLayerContextMenu(const QPoint& position);

private:
    void createUi();
    void createMenus();
    void refreshLayerTree();
    void displayRaster(const std::shared_ptr<RasterLayer>& raster, int bandIndex);
    void appendLog(const QString& text);
    void updateActionStates();

    std::vector<int> selectedLayerIndices() const;
    std::shared_ptr<RasterLayer> selectedRaster() const;
    int selectedBandIndex() const;

    QTreeWidget* layerTree_ {};
    QGraphicsView* imageView_ {};
    QGraphicsScene* imageScene_ {};
    QWidget* scene3DPlaceholder_ {};
    QTabWidget* tabs_ {};
    QTextEdit* logEdit_ {};

    QAction* deleteLayerAction_ {};
    QAction* clearProjectAction_ {};
    QAction* renderAction_ {};
    QAction* histogramAction_ {};
    QAction* equalizeAction_ {};
    QAction* featureAction_ {};
    QAction* demAction_ {};
    QAction* orthoAction_ {};

    bool rebuildingTree_ {};
    LayerManager<DataObject> layers_;
};

} // namespace rs

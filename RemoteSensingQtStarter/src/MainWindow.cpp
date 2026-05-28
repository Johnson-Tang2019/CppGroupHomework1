#include "rs/MainWindow.h"

#include "rs/Algorithms.h"
#include "rs/RasterIO.h"
#include "rs/RasterRenderDialog.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsTextItem>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QSet>
#include <QSplitter>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace rs {
namespace {

constexpr int kLayerIndexRole = Qt::UserRole + 1;
constexpr int kBandIndexRole = Qt::UserRole + 2;
constexpr int kNodeKindRole = Qt::UserRole + 3;

enum class NodeKind {
    Folder,
    Layer,
    Band
};

QString itemKey(const QTreeWidgetItem* item) {
    QStringList parts;
    const auto* current = item;
    while (current) {
        parts.prepend(current->text(0));
        current = current->parent();
    }
    return parts.join(QLatin1Char('/'));
}

void collectExpandedKeys(QTreeWidgetItem* item, QSet<QString>& keys) {
    if (!item) {
        return;
    }
    if (item->isExpanded()) {
        keys.insert(itemKey(item));
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectExpandedKeys(item->child(i), keys);
    }
}

QTreeWidgetItem* ensureChildFolder(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name) {
            return parent->child(i);
        }
    }
    auto* folder = new QTreeWidgetItem(parent);
    folder->setText(0, name);
    folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));
    folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
    return folder;
}

QTreeWidgetItem* ensureTopFolder(QTreeWidget* tree, const QString& name) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == name) {
            return tree->topLevelItem(i);
        }
    }
    auto* folder = new QTreeWidgetItem(tree);
    folder->setText(0, name);
    folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));
    folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
    return folder;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Remote Sensing Qt Starter"));
    createMenus();
    createUi();
    appendLog(QStringLiteral("Starter 已启动：当前版本提供 GDAL 多波段、参数化算法、DEM/正射流程的工程骨架。"));
    updateActionStates();
}

void MainWindow::createMenus() {
    auto* dataMenu = menuBar()->addMenu(QStringLiteral("数据"));
    connect(dataMenu->addAction(QStringLiteral("加载遥感影像(GDAL，可多选)")), &QAction::triggered, this, &MainWindow::openRasterDatasets);
    connect(dataMenu->addAction(QStringLiteral("加载点云")), &QAction::triggered, this, &MainWindow::openPointCloud);
    connect(dataMenu->addAction(QStringLiteral("加载 Mesh")), &QAction::triggered, this, &MainWindow::openMesh);
    connect(dataMenu->addAction(QStringLiteral("加载 DEM")), &QAction::triggered, this, &MainWindow::openDem);
    dataMenu->addSeparator();
    deleteLayerAction_ = dataMenu->addAction(QStringLiteral("删除选中图层"));
    connect(deleteLayerAction_, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
    clearProjectAction_ = dataMenu->addAction(QStringLiteral("初始化/清空工程"));
    connect(clearProjectAction_, &QAction::triggered, this, &MainWindow::clearProject);

    auto* rasterMenu = menuBar()->addMenu(QStringLiteral("影像处理"));
    auto* bandMenu = rasterMenu->addMenu(QStringLiteral("波段与设色"));
    renderAction_ = bandMenu->addAction(QStringLiteral("波段组合/设色..."));
    connect(renderAction_, &QAction::triggered, this, &MainWindow::configureRasterRendering);

    auto* statMenu = rasterMenu->addMenu(QStringLiteral("统计"));
    histogramAction_ = statMenu->addAction(QStringLiteral("灰度直方图..."));
    connect(histogramAction_, &QAction::triggered, this, &MainWindow::runHistogram);

    auto* enhanceMenu = rasterMenu->addMenu(QStringLiteral("增强"));
    equalizeAction_ = enhanceMenu->addAction(QStringLiteral("直方图均衡化..."));
    connect(equalizeAction_, &QAction::triggered, this, &MainWindow::runHistogramEqualization);

    auto* featureMenu = rasterMenu->addMenu(QStringLiteral("特征"));
    featureAction_ = featureMenu->addAction(QStringLiteral("ORB/SIFT 特征提取..."));
    connect(featureAction_, &QAction::triggered, this, &MainWindow::runFeatureExtraction);

    auto* photogrammetryMenu = menuBar()->addMenu(QStringLiteral("摄影测量/三维"));
    demAction_ = photogrammetryMenu->addAction(QStringLiteral("DEM 重建..."));
    connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
    orthoAction_ = photogrammetryMenu->addAction(QStringLiteral("正射影像校正..."));
    connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);
}

void MainWindow::createUi() {
    auto* root = new QSplitter(Qt::Horizontal, this);
    layerTree_ = new QTreeWidget(root);
    layerTree_->setHeaderLabel(QStringLiteral("工程图层"));
    layerTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layerTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(layerTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);
    connect(layerTree_, &QTreeWidget::itemChanged, this, &MainWindow::onLayerItemChanged);
    connect(layerTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showLayerContextMenu);

    auto* right = new QSplitter(Qt::Vertical, root);
    tabs_ = new QTabWidget(right);
    imageScene_ = new QGraphicsScene(this);
    imageView_ = new QGraphicsView(imageScene_, tabs_);
    imageView_->setDragMode(QGraphicsView::ScrollHandDrag);
    imageView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    scene3DPlaceholder_ = new QWidget(tabs_);
    auto* layout = new QVBoxLayout(scene3DPlaceholder_);
    layout->addWidget(new QLabel(QStringLiteral("TODO: 在这里实现 QOpenGLWidget / Qt3D / VTK 三维窗口。\n建议支持：左键旋转、右键平移、滚轮缩放、双击设置旋转中心。")));

    tabs_->addTab(imageView_, QStringLiteral("二维影像"));
    tabs_->addTab(scene3DPlaceholder_, QStringLiteral("三维场景"));

    logEdit_ = new QTextEdit(right);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(210);

    root->setStretchFactor(0, 1);
    root->setStretchFactor(1, 5);
    right->setStretchFactor(0, 5);
    right->setStretchFactor(1, 1);
    setCentralWidget(root);
}

void MainWindow::openRasterDatasets() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("加载遥感影像"),
        QString(),
        QStringLiteral("Remote sensing rasters (*.tif *.tiff *.img *.dat *.jp2);;All Files (*.*)"));
    if (paths.isEmpty()) {
        return;
    }

    for (const QString& path : paths) {
        const QFileInfo info(path);
        // 学生作业应在这里替换为：auto raster = rs::io::loadRasterDataset(path);
        auto raster = std::make_shared<RasterLayer>(info.fileName(), path);
        layers_.add(raster);
        appendLog(QStringLiteral("已登记影像路径：%1。TODO: 调用 RasterIO/GDAL 读取波段、投影和地理变换。").arg(path));
    }
    refreshLayerTree();
    updateActionStates();
}

void MainWindow::openPointCloud() {
    appendLog(QStringLiteral("TODO: 实现 PLY/XYZ/LAS 点云读取，并加入“源数据/点云”。"));
}

void MainWindow::openMesh() {
    appendLog(QStringLiteral("TODO: 实现 OBJ/PLY Mesh 读取，并加入“源数据/Mesh”。"));
}

void MainWindow::openDem() {
    appendLog(QStringLiteral("TODO: 使用 GDAL 读取 DEM GeoTIFF/ASCII Grid，并加入“源数据/DEM”。"));
}

void MainWindow::deleteSelectedLayers() {
    const auto indices = selectedLayerIndices();
    if (indices.empty()) {
        return;
    }
    layers_.removeMany(indices);
    imageScene_->clear();
    refreshLayerTree();
    appendLog(QStringLiteral("已删除 %1 个选中图层。").arg(indices.size()));
    updateActionStates();
}

void MainWindow::clearProject() {
    layers_.clear();
    imageScene_->clear();
    refreshLayerTree();
    appendLog(QStringLiteral("工程已初始化。"));
    updateActionStates();
}

void MainWindow::configureRasterRendering() {
    const auto raster = selectedRaster();
    if (!raster) {
        return;
    }
    const auto request = askRasterRenderRequest(this, *raster);
    if (!request.has_value()) {
        return;
    }
    appendLog(QStringLiteral("TODO: 根据波段组合/设色参数渲染影像：%1。").arg(raster->name()));
}

void MainWindow::runHistogram() {
    HistogramAlgorithm algorithm;
    appendLog(QStringLiteral("TODO: 打开参数对话框并执行：%1，当前波段索引=%2。")
                  .arg(algorithm.name())
                  .arg(selectedBandIndex()));
}

void MainWindow::runHistogramEqualization() {
    HistogramEqualizationAlgorithm algorithm;
    appendLog(QStringLiteral("TODO: 打开参数对话框并执行：%1，结果应加入“处理结果/直方图均衡化”。").arg(algorithm.name()));
}

void MainWindow::runFeatureExtraction() {
    FeatureExtractionAlgorithm algorithm;
    appendLog(QStringLiteral("TODO: 打开参数对话框并执行：%1，可选 ORB/SIFT/AKAZE。").arg(algorithm.name()));
}

void MainWindow::runDemReconstruction() {
    appendLog(QStringLiteral("TODO: 选择两张影像后弹出 DEM 重建对话框，补充相机参数/控制点/输出目录，再自动完成匹配、校正、视差和 DEM 输出。"));
}

void MainWindow::runOrthorectification() {
    appendLog(QStringLiteral("TODO: 选择影像和对应 DEM，使用 DEM 地理变换与影像模型重采样生成正射影像。"));
}

void MainWindow::onSelectionChanged() {
    displayRaster(selectedRaster(), selectedBandIndex());
    updateActionStates();
}

void MainWindow::onLayerItemChanged(QTreeWidgetItem* item, int column) {
    if (rebuildingTree_ || !item || column != 0) {
        return;
    }
    if (static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt()) != NodeKind::Layer) {
        return;
    }
    const QVariant value = item->data(0, kLayerIndexRole);
    if (!value.isValid()) {
        return;
    }
    try {
        layers_.at(value.toInt())->setVisible(item->checkState(0) == Qt::Checked);
        appendLog(QStringLiteral("%1：%2").arg(item->text(0), item->checkState(0) == Qt::Checked ? QStringLiteral("显示") : QStringLiteral("隐藏")));
    } catch (const std::exception&) {
    }
}

void MainWindow::showLayerContextMenu(const QPoint& position) {
    QTreeWidgetItem* item = layerTree_->itemAt(position);
    if (!item) {
        return;
    }
    QMenu menu(this);
    const auto indices = selectedLayerIndices();
    QAction* deleteAction = menu.addAction(QStringLiteral("删除选中图层"));
    deleteAction->setEnabled(!indices.empty());
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);

    const QVariant layerIndex = item->data(0, kLayerIndexRole);
    if (layerIndex.isValid()) {
        try {
            if (layers_.at(layerIndex.toInt())->type() == DataType::Dem) {
                QAction* exportDem = menu.addAction(QStringLiteral("导出 DEM..."));
                connect(exportDem, &QAction::triggered, this, [this]() {
                    appendLog(QStringLiteral("TODO: 调用 rs::io::exportDemAsGeoTiff 导出 DEM。"));
                });
            }
        } catch (const std::exception&) {
        }
    }
    menu.exec(layerTree_->viewport()->mapToGlobal(position));
}

void MainWindow::refreshLayerTree() {
    QSet<QString> expandedKeys;
    for (int i = 0; i < layerTree_->topLevelItemCount(); ++i) {
        collectExpandedKeys(layerTree_->topLevelItem(i), expandedKeys);
    }

    rebuildingTree_ = true;
    layerTree_->clear();

    auto* sourceRoot = ensureTopFolder(layerTree_, QStringLiteral("源数据"));
    auto* resultRoot = ensureTopFolder(layerTree_, QStringLiteral("处理结果"));
    auto* rasterFolder = ensureChildFolder(sourceRoot, QStringLiteral("遥感影像"));
    auto* pointFolder = ensureChildFolder(sourceRoot, QStringLiteral("点云"));
    auto* meshFolder = ensureChildFolder(sourceRoot, QStringLiteral("Mesh"));
    auto* demFolder = ensureChildFolder(sourceRoot, QStringLiteral("DEM"));
    auto* histogramFolder = ensureChildFolder(resultRoot, QStringLiteral("直方图"));
    auto* equalizeFolder = ensureChildFolder(resultRoot, QStringLiteral("直方图均衡化"));
    Q_UNUSED(histogramFolder)
    Q_UNUSED(equalizeFolder)

    for (int i = 0; i < layers_.size(); ++i) {
        const auto layer = layers_.at(i);
        QTreeWidgetItem* parent = nullptr;
        switch (layer->type()) {
        case DataType::Raster:
            parent = rasterFolder;
            break;
        case DataType::PointCloud:
            parent = pointFolder;
            break;
        case DataType::Mesh:
            parent = meshFolder;
            break;
        case DataType::Dem:
            parent = demFolder;
            break;
        case DataType::Result:
            parent = resultRoot;
            break;
        }

        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, QStringLiteral("%1  [%2]").arg(layer->name(), layer->summary()));
        item->setData(0, kLayerIndexRole, i);
        item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Layer));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(0, layer->visible() ? Qt::Checked : Qt::Unchecked);

        if (const auto raster = std::dynamic_pointer_cast<RasterLayer>(layer)) {
            if (raster->bandCount() == 0) {
                auto* child = new QTreeWidgetItem(item);
                child->setText(0, QStringLiteral("TODO: GDAL 读取后显示 Band 1..N"));
                child->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Band));
                child->setFlags((child->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
            } else {
                for (int band = 0; band < raster->bandCount(); ++band) {
                    const auto& bandInfo = raster->band(band);
                    auto* child = new QTreeWidgetItem(item);
                    child->setText(0, QStringLiteral("Band %1  %2 x %3").arg(band + 1).arg(bandInfo.width).arg(bandInfo.height));
                    child->setData(0, kLayerIndexRole, i);
                    child->setData(0, kBandIndexRole, band);
                    child->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Band));
                }
            }
        }
    }

    const bool firstBuild = expandedKeys.isEmpty();
    for (int i = 0; i < layerTree_->topLevelItemCount(); ++i) {
        auto* top = layerTree_->topLevelItem(i);
        top->setExpanded(firstBuild || expandedKeys.contains(itemKey(top)));
        for (int j = 0; j < top->childCount(); ++j) {
            auto* child = top->child(j);
            child->setExpanded(firstBuild || expandedKeys.contains(itemKey(child)));
            for (int k = 0; k < child->childCount(); ++k) {
                auto* layer = child->child(k);
                layer->setExpanded(firstBuild || expandedKeys.contains(itemKey(layer)));
            }
        }
    }

    rebuildingTree_ = false;
}

void MainWindow::displayRaster(const std::shared_ptr<RasterLayer>& raster, int bandIndex) {
    imageScene_->clear();
    if (!raster) {
        imageScene_->addText(QStringLiteral("请选择一个遥感影像图层或波段。"));
        return;
    }

    QImage image;
    if (bandIndex >= 0 && bandIndex < raster->bandCount()) {
        image = io::renderSingleBandGray(*raster, bandIndex);
    } else {
        image = raster->currentDisplayImage();
    }

    if (image.isNull()) {
        imageScene_->addText(QStringLiteral("TODO: 实现 GDAL 读取与波段渲染后在这里显示影像。\n当前图层：%1").arg(raster->name()));
        return;
    }

    imageScene_->addPixmap(QPixmap::fromImage(image));
    imageScene_->setSceneRect(image.rect());
    imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio);
}

std::vector<int> MainWindow::selectedLayerIndices() const {
    std::vector<int> indices;
    for (const auto* item : layerTree_->selectedItems()) {
        const QVariant value = item->data(0, kLayerIndexRole);
        if (!value.isValid()) {
            continue;
        }
        const int index = value.toInt();
        if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
            indices.push_back(index);
        }
    }
    return indices;
}

std::shared_ptr<RasterLayer> MainWindow::selectedRaster() const {
    auto* item = layerTree_->currentItem();
    if (!item) {
        return {};
    }
    const QVariant value = item->data(0, kLayerIndexRole);
    if (!value.isValid()) {
        return {};
    }
    try {
        return std::dynamic_pointer_cast<RasterLayer>(layers_.at(value.toInt()));
    } catch (const std::exception&) {
        return {};
    }
}

int MainWindow::selectedBandIndex() const {
    auto* item = layerTree_->currentItem();
    if (!item) {
        return -1;
    }
    const QVariant value = item->data(0, kBandIndexRole);
    return value.isValid() ? value.toInt() : -1;
}

void MainWindow::updateActionStates() {
    int selectedRasters = 0;
    int selectedDems = 0;
    for (const int index : selectedLayerIndices()) {
        try {
            const auto layer = layers_.at(index);
            if (std::dynamic_pointer_cast<RasterLayer>(layer)) {
                ++selectedRasters;
            } else if (layer->type() == DataType::Dem) {
                ++selectedDems;
            }
        } catch (const std::exception&) {
        }
    }

    const bool hasOneRaster = selectedRasters == 1;
    if (deleteLayerAction_) {
        deleteLayerAction_->setEnabled(!selectedLayerIndices().empty());
    }
    if (clearProjectAction_) {
        clearProjectAction_->setEnabled(!layers_.empty());
    }
    if (renderAction_) {
        renderAction_->setEnabled(hasOneRaster);
    }
    if (histogramAction_) {
        histogramAction_->setEnabled(hasOneRaster);
    }
    if (equalizeAction_) {
        equalizeAction_->setEnabled(hasOneRaster);
    }
    if (featureAction_) {
        featureAction_->setEnabled(hasOneRaster);
    }
    if (demAction_) {
        demAction_->setEnabled(selectedRasters == 2);
    }
    if (orthoAction_) {
        orthoAction_->setEnabled(selectedRasters >= 1 && selectedDems >= 1);
    }
}

void MainWindow::appendLog(const QString& text) {
    logEdit_->append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

} // namespace rs

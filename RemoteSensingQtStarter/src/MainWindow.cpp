#include "rs/MainWindow.h"
//2026.6.6
#include "rs/Algorithms.h"
#include "rs/RasterIO.h"
#include "rs/RasterRenderDialog.h"
#include "rs/Scene3DWidget.h"

#ifdef RS_WITH_GDAL
#include <gdal_priv.h>
#endif

#include <QAbstractItemView>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
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
#include <QTextStream>
#include <QVBoxLayout>
#include <QStatusBar> // 【新增】引入底部状态栏支持

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace rs {
namespace {

// 自定义角色，用于在 QTreeWidgetItem 中存储额外数据
constexpr int kLayerIndexRole = Qt::UserRole + 1;  // 存储图层在 LayerManager 中的索引
constexpr int kBandIndexRole = Qt::UserRole + 2;   // 存储波段索引（用于波段子节点）
constexpr int kNodeKindRole = Qt::UserRole + 3;    // 存储节点类型（文件夹/图层/波段）

// 图层树节点的类型枚举
enum class NodeKind {
    Folder,  // 文件夹节点（如"源数据/遥感影像"），不可选中
    Layer,   // 图层节点，可选中/勾选
    Band     // 波段子节点，仅信息展示
};

quint8 readUInt8At(QFile& file, qint64 offset) {
    if (!file.seek(offset)) throw std::runtime_error("LAS 文件头不完整");
    char value = 0;
    if (file.read(&value, 1) != 1) throw std::runtime_error("LAS 文件头不完整");
    return static_cast<quint8>(value);
}

quint16 readUInt16LeAt(QFile& file, qint64 offset) {
    if (!file.seek(offset)) throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint16 value = 0;
    stream >> value;
    if (stream.status() != QDataStream::Ok) throw std::runtime_error("LAS 文件头不完整");
    return value;
}

quint32 readUInt32LeAt(QFile& file, qint64 offset) {
    if (!file.seek(offset)) throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 value = 0;
    stream >> value;
    if (stream.status() != QDataStream::Ok) throw std::runtime_error("LAS 文件头不完整");
    return value;
}

quint64 readUInt64LeAt(QFile& file, qint64 offset) {
    if (!file.seek(offset)) throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint64 value = 0;
    stream >> value;
    if (stream.status() != QDataStream::Ok) throw std::runtime_error("LAS 文件头不完整");
    return value;
}

double readDoubleLeAt(QFile& file, qint64 offset) {
    if (!file.seek(offset)) throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    double value = 0.0;
    stream >> value;
    if (stream.status() != QDataStream::Ok) throw std::runtime_error("LAS 文件头不完整");
    return value;
}

qint32 readInt32Le(const char* data) {
    const auto* b = reinterpret_cast<const unsigned char*>(data);
    const quint32 value = (static_cast<quint32>(b[0])      ) |
                          (static_cast<quint32>(b[1]) <<  8) |
                          (static_cast<quint32>(b[2]) << 16) |
                          (static_cast<quint32>(b[3]) << 24);
    return static_cast<qint32>(value);
}

QVector<QVector3D> readLasPoints(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("无法打开 LAS 文件");
    }
    if (file.read(4) != QByteArray("LASF", 4)) {
        throw std::runtime_error("不是有效的 LAS 文件");
    }

    const quint32 pointDataOffset = readUInt32LeAt(file, 96);
    const quint8 pointFormat = readUInt8At(file, 104) & 0x3f;
    const quint16 recordLength = readUInt16LeAt(file, 105);
    quint64 pointCount = readUInt32LeAt(file, 107);
    if (pointCount == 0 && file.size() >= 255) {
        pointCount = readUInt64LeAt(file, 247);
    }
    const double xScale = readDoubleLeAt(file, 131);
    const double yScale = readDoubleLeAt(file, 139);
    const double zScale = readDoubleLeAt(file, 147);
    const double xOffset = readDoubleLeAt(file, 155);
    const double yOffset = readDoubleLeAt(file, 163);
    const double zOffset = readDoubleLeAt(file, 171);

    if (pointDataOffset == 0 || recordLength < 12 || pointFormat > 10) {
        throw std::runtime_error("LAS 点记录格式无效");
    }

    const qint64 availableRecords = (file.size() - static_cast<qint64>(pointDataOffset)) / recordLength;
    pointCount = std::min<quint64>(pointCount, static_cast<quint64>(std::max<qint64>(0, availableRecords)));
    pointCount = std::min<quint64>(pointCount, static_cast<quint64>(std::numeric_limits<int>::max()));
    const int reserveCount = static_cast<int>(pointCount);

    QVector<QVector3D> points;
    points.reserve(reserveCount);
    QByteArray record(recordLength, Qt::Uninitialized);
    if (!file.seek(pointDataOffset)) {
        throw std::runtime_error("无法定位 LAS 点数据");
    }
    for (quint64 i = 0; i < pointCount; ++i) {
        if (file.read(record.data(), recordLength) != recordLength) break;
        const double x = readInt32Le(record.constData()) * xScale + xOffset;
        const double y = readInt32Le(record.constData() + 4) * yScale + yOffset;
        const double z = readInt32Le(record.constData() + 8) * zScale + zOffset;
        points.append(QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
    }
    return points;
}

// 生成节点的唯一键
QString itemKey(const QTreeWidgetItem* item) {
    QStringList parts;
    const auto* current = item;
    while (current) {
        parts.prepend(current->text(0));
        current = current->parent();
    }
    return parts.join(QLatin1Char('/'));
}

// 递归收集所有展开节点的键
void collectExpandedKeys(QTreeWidgetItem* item, QSet<QString>& keys) {
    if (!item) return;
    if (item->isExpanded()) {
        keys.insert(itemKey(item));
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectExpandedKeys(item->child(i), keys);
    }
}

// 在指定父节点下查找或创建子文件夹
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

// 在顶层节点中查找或创建文件夹
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

// 构造函数
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Remote Sensing Qt Starter"));
    createMenus();
    createUi();
    appendLog(QStringLiteral("Starter 已启动：当前版本提供 GDAL 多波段、参数化算法、DEM/正射流程的工程骨架。"));
    updateActionStates();
}

// 构建菜单栏
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
    renderAction_->setStatusTip(QStringLiteral("将单波段设色或RGB三波段合成显示")); // 【增强】状态栏提示
    connect(renderAction_, &QAction::triggered, this, &MainWindow::configureRasterRendering);

    auto* statMenu = rasterMenu->addMenu(QStringLiteral("统计"));
    histogramAction_ = statMenu->addAction(QStringLiteral("灰度直方图..."));
    histogramAction_->setStatusTip(QStringLiteral("使用 OpenCV 提取当前影像灰度直方图分布")); // 【增强】状态栏提示
    connect(histogramAction_, &QAction::triggered, this, &MainWindow::runHistogram);

    auto* enhanceMenu = rasterMenu->addMenu(QStringLiteral("增强"));
    equalizeAction_ = enhanceMenu->addAction(QStringLiteral("直方图均衡化..."));
    equalizeAction_->setStatusTip(QStringLiteral("使用 CLAHE 算法对影像进行自适应对比度增强")); // 【增强】状态栏提示
    connect(equalizeAction_, &QAction::triggered, this, &MainWindow::runHistogramEqualization);

    auto* featureMenu = rasterMenu->addMenu(QStringLiteral("特征"));
    featureAction_ = featureMenu->addAction(QStringLiteral("ORB/SIFT 特征提取..."));
    featureAction_->setStatusTip(QStringLiteral("提取影像 SIFT 关键点，用于配准及空三加密")); // 【增强】状态栏提示
    connect(featureAction_, &QAction::triggered, this, &MainWindow::runFeatureExtraction);

        auto* photogrammetryMenu = menuBar()->addMenu(QStringLiteral("摄影测量/三维"));
    demAction_ = photogrammetryMenu->addAction(QStringLiteral("DEM 重建..."));
    demAction_->setStatusTip(QStringLiteral("立体像对 SGBM 稠密匹配与地形重建")); // 【增强】状态栏提示
    connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
    orthoAction_ = photogrammetryMenu->addAction(QStringLiteral("正射影像校正..."));
    orthoAction_->setStatusTip(QStringLiteral("利用 DEM 纠正影像几何畸变")); // 【增强】状态栏提示
    connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);

    // ── 三维点云/Mesh 功能菜单 ──
    auto* pointMenu = menuBar()->addMenu(QStringLiteral("点云处理"));
    downsampleAction_ = pointMenu->addAction(QStringLiteral("体素降采样..."));
    downsampleAction_->setStatusTip(QStringLiteral("对点云进行体素网格降采样"));
    connect(downsampleAction_, &QAction::triggered, this, &MainWindow::runPointCloudDownsample);

    filterAction_ = pointMenu->addAction(QStringLiteral("统计滤波（去离群点）..."));
    filterAction_->setStatusTip(QStringLiteral("基于距离统计去除离群点"));
    connect(filterAction_, &QAction::triggered, this, &MainWindow::runPointCloudFilter);

    pcToDemAction_ = pointMenu->addAction(QStringLiteral("点云转 DEM..."));
    pcToDemAction_->setStatusTip(QStringLiteral("将点云网格化为 DEM/DSM"));
    connect(pcToDemAction_, &QAction::triggered, this, &MainWindow::runPointCloudToDem);

    pointMenu->addSeparator();
    exportPlyAction_ = pointMenu->addAction(QStringLiteral("导出为 PLY..."));
    exportPlyAction_->setStatusTip(QStringLiteral("将点云或 Mesh 保存为 PLY 文件"));
    connect(exportPlyAction_, &QAction::triggered, this, &MainWindow::exportPly);

    clearPointAction_ = pointMenu->addAction(QStringLiteral("清空三维场景"));
    clearPointAction_->setStatusTip(QStringLiteral("清除 3D 视窗中显示的点云/Mesh"));
    connect(clearPointAction_, &QAction::triggered, this, [this]() {
        scene3DWidget_->setPoints({});
        scene3DWidget_->setMesh({}, {});
        appendLog(QStringLiteral("三维场景已清空。"));
    });
}

// 构建界面布局
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

    scene3DWidget_ = new Scene3DWidget(tabs_);

    tabs_->addTab(imageView_, QStringLiteral("二维影像"));
    tabs_->addTab(scene3DWidget_, QStringLiteral("三维场景"));

    logEdit_ = new QTextEdit(right);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(210);

    root->setStretchFactor(0, 1);
    root->setStretchFactor(1, 5);
    right->setStretchFactor(0, 5);
    right->setStretchFactor(1, 1);

    setCentralWidget(root);
}

// 打开遥感影像
void MainWindow::openRasterDatasets() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("加载遥感影像"),
        QString(),
        QStringLiteral("Remote sensing rasters (*.tif *.tiff *.img *.dat *.jp2 *.jpg *.jpeg *.png *.bmp);;All Files (*.*)"));
    if (paths.isEmpty()) return;

    for (const QString& path : paths) {
        const QFileInfo info(path);
        try {
            auto raster = rs::io::loadRasterDataset(path);
            if (raster) {
                layers_.add(raster);
                appendLog(QStringLiteral("已加载影像：%1（%2 波段，%3x%4）")
                    .arg(info.fileName())
                    .arg(raster->bandCount())
                    .arg(raster->bandCount() > 0 ? raster->band(0).width : 0)
                    .arg(raster->bandCount() > 0 ? raster->band(0).height : 0));
            }
        } catch (const std::exception& e) {
            appendLog(QStringLiteral("加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
        }
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载点云
void MainWindow::openPointCloud() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载点云"),
        QString(),
        QStringLiteral("Point Cloud (*.ply *.xyz *.las);;All Files (*.*)"));
    if (path.isEmpty()) return;

    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();
    QVector<QVector3D> points;

    try {
        if (ext == QStringLiteral("xyz")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("无法打开文件");
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    points.append(QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
                }
            }
            file.close();
        } else if (ext == QStringLiteral("ply")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("无法打开 PLY 文件");
            QByteArray allData = file.readAll();
            file.close();

            int headerEndPos = allData.indexOf("end_header");
            if (headerEndPos < 0) throw std::runtime_error("PLY 缺少 end_header");
            int nlPos = allData.indexOf('\n', headerEndPos);
            int headerBytes = (nlPos >= 0) ? nlPos + 1 : allData.size();

            QByteArray headerData = allData.left(headerBytes);
            QTextStream headerStream(headerData);
            bool isAscii = false;
            int vertexCount = 0;
            int propCount = 0;
            bool inVertex = false;

            while (!headerStream.atEnd()) {
                QString line = headerStream.readLine().trimmed();
                if (line.startsWith(QLatin1String("element vertex"))) {
                    vertexCount = line.section(QLatin1Char(' '), 2, 2).toInt();
                    inVertex = true;
                    continue;
                }
                if (inVertex && line.startsWith(QLatin1String("element "))) inVertex = false;
                if (inVertex && line.startsWith(QLatin1String("property "))) propCount++;
                if (line.contains(QLatin1String("format ascii"))) isAscii = true;
            }

            if (vertexCount <= 0) throw std::runtime_error("PLY 顶点数量无效");

            if (isAscii) {
                QTextStream in(allData);
                while (!in.atEnd()) {
                    if (in.readLine().trimmed() == QLatin1String("end_header")) break;
                }
                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (line.isEmpty()) continue;
                    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (parts.size() < 3) continue;
                    bool xOk, yOk, zOk;
                    float x = parts[0].toFloat(&xOk);
                    float y = parts[1].toFloat(&yOk);
                    float z = parts[2].toFloat(&zOk);
                    if (xOk && yOk && zOk) points.append(QVector3D(x, y, z));
                }
            } else {
                if (propCount < 3) throw std::runtime_error("PLY 顶点属性数不足");
                int vertexSize = propCount * sizeof(float);
                const char* data = allData.constData() + headerBytes;
                int remaining = allData.size() - headerBytes;
                int maxVerts = remaining / vertexSize;
                int n = std::min(vertexCount, maxVerts);
                points.reserve(n);
                for (int i = 0; i < n; ++i) {
                    const float* f = reinterpret_cast<const float*>(data + i * vertexSize);
                    points.append(QVector3D(f[0], f[1], f[2]));
                }
            }
        } else if (ext == QStringLiteral("las")) {
            QFile lasFile(path);
            if (!lasFile.open(QIODevice::ReadOnly)) throw std::runtime_error("无法打开 LAS 文件");
            QByteArray lasData = lasFile.readAll();
            lasFile.close();
            if (lasData.size() < 227) throw std::runtime_error("LAS 文件头不完整");
            const unsigned char* hdr = reinterpret_cast<const unsigned char*>(lasData.constData());
            if (hdr[0] != 'L' || hdr[1] != 'A' || hdr[2] != 'S' || hdr[3] != 'F') throw std::runtime_error("无效的 LAS 签名");
            quint32 offset = *reinterpret_cast<const quint32*>(hdr + 96);
            quint16 recLen = *reinterpret_cast<const quint16*>(hdr + 105);
            quint32 ptCount = *reinterpret_cast<const quint32*>(hdr + 107);
            double xScale = *reinterpret_cast<const double*>(hdr + 131);
            double yScale = *reinterpret_cast<const double*>(hdr + 139);
            double zScale = *reinterpret_cast<const double*>(hdr + 147);
            double xOff = *reinterpret_cast<const double*>(hdr + 155);
            double yOff = *reinterpret_cast<const double*>(hdr + 163);
            double zOff = *reinterpret_cast<const double*>(hdr + 171);
            if (recLen < 12) throw std::runtime_error("LAS 记录长度无效");
            quint64 totalPoints = ptCount;
            if (totalPoints == 0 && lasData.size() >= 255) {
                totalPoints = *reinterpret_cast<const quint64*>(hdr + 247);
            }
            quint64 maxRead = (lasData.size() - offset) / recLen;
            quint64 n = std::min(totalPoints, maxRead);
            if (n > 10000000) n = 10000000;
            points.reserve(static_cast<int>(n));
            for (quint64 i = 0; i < n; ++i) {
                const char* rec = lasData.constData() + offset + i * recLen;
                qint32 ix = *reinterpret_cast<const qint32*>(rec);
                qint32 iy = *reinterpret_cast<const qint32*>(rec + 4);
                qint32 iz = *reinterpret_cast<const qint32*>(rec + 8);
                points.append(QVector3D(static_cast<float>(ix * xScale + xOff),
                                        static_cast<float>(iy * yScale + yOff),
                                        static_cast<float>(iz * zScale + zOff)));
            }
        } else {
            throw std::runtime_error("不支持的格式");
        }

        if (points.isEmpty()) throw std::runtime_error("未能读取到任何点数据");

        auto layer = std::make_shared<PointCloudLayer>(info.fileName(), path, points);
        layers_.add(layer);
        appendLog(QStringLiteral("已加载点云：%1（%2 个点）").arg(info.fileName()).arg(points.size()));

        scene3DWidget_->setPoints(points);
        tabs_->setCurrentWidget(scene3DWidget_);
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("点云加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载 Mesh
void MainWindow::openMesh() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载 Mesh"),
        QString(),
        QStringLiteral("Mesh (*.obj *.ply);;All Files (*.*)"));
    if (path.isEmpty()) return;

    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();

    try {
        QVector<QVector3D> vertices;
        QVector<Face> faces;

        if (ext == QStringLiteral("obj")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) throw std::runtime_error("无法打开 OBJ 文件");
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.isEmpty()) continue;

                if (parts[0] == QStringLiteral("v") && parts.size() >= 4) {
                    vertices.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
                } else if (parts[0] == QStringLiteral("f") && parts.size() >= 4) {
                    Face face;
                    face.a = parts[1].section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    face.b = parts[2].section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    face.c = parts[3].section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    faces.append(face);
                }
            }
            file.close();
                } else if (ext == QStringLiteral("ply")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("无法打开 PLY 文件");
            QByteArray allData = file.readAll();
            file.close();

            // 找到 header 结束位置
            int headerEndPos = allData.indexOf("end_header");
            if (headerEndPos < 0) throw std::runtime_error("PLY 缺少 end_header");
            int nlPos = allData.indexOf('\n', headerEndPos);
            int headerBytes = (nlPos >= 0) ? nlPos + 1 : allData.size();

                        // 解析 header
            QByteArray headerData = allData.left(headerBytes);
            QTextStream headerStream(headerData);
            int vertexCount = 0, faceCount = 0;
            bool isBinary = false;
            int vertProps = 0;
            bool parsingVert = false, parsingFace = false;

            while (!headerStream.atEnd()) {
                QString line = headerStream.readLine().trimmed();
                if (line.startsWith(QStringLiteral("element vertex"))) {
                    vertexCount = line.section(QLatin1Char(' '), 2, 2).toInt();
                    parsingVert = true; parsingFace = false;
                    vertProps = 0; // 重置，只统计 vertex 的属性
                    continue;
                }
                if (line.startsWith(QStringLiteral("element face"))) {
                    faceCount = line.section(QLatin1Char(' '), 2, 2).toInt();
                    parsingVert = false; parsingFace = true;
                    continue;
                }
                if (line.startsWith(QStringLiteral("element "))) {
                    parsingVert = false; parsingFace = false;
                }
                if (parsingVert && line.startsWith(QStringLiteral("property "))) vertProps++;
                if (line.startsWith(QStringLiteral("format binary"))) isBinary = true;
                if (line.startsWith(QStringLiteral("format ascii"))) isBinary = false;
            }

            if (vertexCount <= 0) throw std::runtime_error("PLY 顶点数量无效");

            vertices.reserve(vertexCount);
            faces.reserve(faceCount);

            if (isBinary) {
                // ── 二进制 PLY 解析 ──
                if (vertProps < 3) throw std::runtime_error("PLY 顶点属性不足");
                int vertexSize = vertProps * sizeof(float);
                const char* data = allData.constData() + headerBytes;
                int remaining = allData.size() - headerBytes;

                // 读顶点
                for (int i = 0; i < vertexCount && i * vertexSize + 3 * sizeof(float) <= remaining; ++i) {
                    const float* f = reinterpret_cast<const float*>(data + i * vertexSize);
                    vertices.append(QVector3D(f[0], f[1], f[2]));
                }

                // 读三角面 - 二进制格式中 face 通常是"uchar n_vertices int i0 int i1 int i2"
                int faceDataOffset = vertexCount * vertexSize;
                int faceIdx = 0;
                while (faceIdx < faceCount && faceDataOffset + 1 <= remaining) {
                    const unsigned char* buf = reinterpret_cast<const unsigned char*>(data + faceDataOffset);
                    int nVerts = buf[0]; // 每个面的顶点数
                    faceDataOffset += 1;
                    if (nVerts == 3 && faceDataOffset + 3 * sizeof(int) <= remaining) {
                        const int* indices = reinterpret_cast<const int*>(data + faceDataOffset);
                        Face f;
                        f.a = indices[0];
                        f.b = indices[1];
                        f.c = indices[2];
                        faces.append(f);
                        faceDataOffset += 3 * sizeof(int);
                    } else if (nVerts > 0) {
                        // 跳过非三角面
                        faceDataOffset += nVerts * sizeof(int);
                    }
                    faceIdx++;
                }
            } else {
                // ── ASCII PLY 解析 ──
                QTextStream in(allData);
                int readVerts = 0, readFaces = 0;
                bool readingVerts = true;

                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

                    if (line == QStringLiteral("end_header")) {
                        readingVerts = true;
                        continue;
                    }

                    // 跳过 header 中的其他行（element/property/format 等）
                    if (line.startsWith(QLatin1String("ply")) ||
                        line.startsWith(QLatin1String("format")) ||
                        line.startsWith(QLatin1String("element")) ||
                        line.startsWith(QLatin1String("property")) ||
                        line.startsWith(QLatin1String("comment")) ||
                        line.startsWith(QLatin1String("obj_info"))) continue;

                    if (readingVerts && readVerts < vertexCount) {
                        QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        if (parts.size() >= 3) {
                            vertices.append(QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
                        }
                        ++readVerts;
                        if (readVerts >= vertexCount) readingVerts = false;
                    } else if (!readingVerts && readFaces < faceCount) {
                        QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        if (parts.size() >= 4) {
                            Face face;
                            face.a = parts[1].toInt();
                            face.b = parts[2].toInt();
                            face.c = parts[3].toInt();
                            faces.append(face);
                        }
                        ++readFaces;
                    }
                }
            }
        } else {
            throw std::runtime_error("不支持的格式: " + ext.toStdString() + "，仅支持 OBJ/PLY");
        }

        if (vertices.isEmpty()) throw std::runtime_error("未能读取到任何顶点数据");

        auto layer = std::make_shared<MeshLayer>(info.fileName(), path, vertices, faces);
        layers_.add(layer);
        
        // 【核心修复】将读取的 Mesh 顶点和三角面片传入给 3D 渲染引擎
        scene3DWidget_->setMesh(vertices, faces);
        tabs_->setCurrentWidget(scene3DWidget_);

        appendLog(QStringLiteral("已加载 Mesh：%1（%2 个顶点，%3 个三角面）")
            .arg(info.fileName()).arg(vertices.size()).arg(faces.size()));
            
        if (statusBar()) {
            statusBar()->showMessage(QStringLiteral("Mesh 加载成功: %1").arg(info.fileName()), 3000);
        }
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("Mesh 加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载 DEM
void MainWindow::openDem() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载 DEM"),
        QString(),
        QStringLiteral("Digital Elevation Model (*.tif *.tiff *.asc *.dem);;All Files (*.*)"));
    if (path.isEmpty()) return;

    const QFileInfo info(path);
#ifndef RS_WITH_GDAL
    appendLog(QStringLiteral("DEM 加载失败 [%1]：当前构建未启用 GDAL。").arg(info.fileName()));
#else
    try {
        GDALAllRegister();
        GDALDataset* dataset = static_cast<GDALDataset*>(
            GDALOpenEx(path.toUtf8().constData(), GA_ReadOnly, nullptr, nullptr, nullptr));
        if (!dataset) throw std::runtime_error("无法打开 DEM 文件");

        const int width = dataset->GetRasterXSize();
        const int height = dataset->GetRasterYSize();
        const int bandCount = dataset->GetRasterCount();

        if (width <= 0 || height <= 0 || bandCount < 1) {
            GDALClose(dataset);
            throw std::runtime_error("DEM 尺寸或波段数无效");
        }

        std::array<double, 6> geoTransform = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
        if (dataset->GetGeoTransform(geoTransform.data()) != CE_None) {
            geoTransform = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
        }

        const int pixelCount = width * height;
        QVector<float> elevations(pixelCount);
        GDALRasterBand* gdalBand = dataset->GetRasterBand(1);
        CPLErr err = gdalBand->RasterIO(GF_Read, 0, 0, width, height,
                                         elevations.data(), width, height,
                                         GDT_Float32, 0, 0, nullptr);
        GDALClose(dataset);

        if (err != CE_None) throw std::runtime_error("读取 DEM 像素数据失败");

        auto dem = std::make_shared<DemLayer>(info.fileName(), path, width, height, elevations);
        dem->setGeoTransform(geoTransform);
        dem->setSourceRasterPath(path);
        layers_.add(dem);
        appendLog(QStringLiteral("已加载 DEM：%1（%2x%3）").arg(info.fileName()).arg(width).arg(height));
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("DEM 加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
    }
#endif
    refreshLayerTree();
    updateActionStates();
}

void MainWindow::deleteSelectedLayers() {
    const auto indices = selectedLayerIndices();
    if (indices.empty()) return;
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
    if (!raster) return;
    const auto request = askRasterRenderRequest(this, *raster);
    if (!request.has_value()) return;

    QImage image;
    QString description;
    switch (request->mode) {
    case RasterRenderMode::AutoRgb:
        image = io::renderRgbComposite(*raster, 0, 1, 2);
        description = QStringLiteral("Auto RGB (Band 1/2/3)");
        break;
    case RasterRenderMode::RgbBands:
        image = io::renderRgbComposite(*raster, request->redBand, request->greenBand, request->blueBand);
        description = QStringLiteral("RGB (Band %1/%2/%3)")
                          .arg(request->redBand + 1)
                          .arg(request->greenBand + 1)
                          .arg(request->blueBand + 1);
        break;
    case RasterRenderMode::SingleBandGray:
    case RasterRenderMode::PseudoColor:
        image = io::renderSingleBandGray(*raster, request->grayBand);
        description = QStringLiteral("Gray (Band %1)").arg(request->grayBand + 1);
        break;
    }

    if (image.isNull()) {
        appendLog(QStringLiteral("渲染失败：%1，请确认加载时已读取像素样本且波段索引有效。").arg(raster->name()));
        return;
    }
    raster->setCurrentDisplayImage(image);
    raster->setRenderDescription(description);
    displayRaster(raster, -1);
    appendLog(QStringLiteral("已渲染影像：%1，%2。").arg(raster->name(), description));
}

void MainWindow::runHistogram() {
    auto raster = selectedRaster();
    if (!raster) return;
    HistogramAlgorithm algorithm;
    ProcessingContext context;
    algorithm.execute(*raster, context);
    appendLog(QStringLiteral("已执行：%1，结果已在独立窗口显示。").arg(algorithm.name()));
}

void MainWindow::runHistogramEqualization() {
    auto raster = selectedRaster();
    if (!raster) return;
    HistogramEqualizationAlgorithm algorithm;
    ProcessingContext context;
    algorithm.execute(*raster, context);
    appendLog(QStringLiteral("已执行：%1，结果已在独立窗口显示。").arg(algorithm.name()));
}

void MainWindow::runFeatureExtraction() {
    auto raster = selectedRaster();
    if (!raster) return;
    FeatureExtractionAlgorithm algorithm;
    ProcessingContext context;
    algorithm.execute(*raster, context);
    appendLog(QStringLiteral("已执行：%1，特征点已在独立窗口显示。").arg(algorithm.name()));
}

void MainWindow::runDemReconstruction() {
    DemReconstructionPipeline pipeline;
    DemReconstructionPipeline::Inputs inputs;

    // 从选中的图层中获取左右影像路径
    const auto indices = selectedLayerIndices();
    int rasterCount = 0;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            if (std::dynamic_pointer_cast<RasterLayer>(layer)) {
                if (rasterCount == 0) {
                    inputs.leftImagePath = layer->path();
                } else if (rasterCount == 1) {
                    inputs.rightImagePath = layer->path();
                }
                rasterCount++;
            }
        } catch (...) {}
    }

    if (rasterCount < 2) {
        appendLog(QStringLiteral("DEM 重建提示：请先加载并在图层树中选中两个遥感影像作为立体像对（左右各一）。"));
        return;
    }

    // 设置输出目录
    inputs.outputDirectory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 DEM 输出目录"), QString());
    if (inputs.outputDirectory.isEmpty()) {
        appendLog(QStringLiteral("DEM 重建已取消：未选择输出目录。"));
        return;
    }

    try {
        auto dem = pipeline.reconstruct(inputs);
        if (dem) {
            layers_.add(dem);
            refreshLayerTree();
            appendLog(QStringLiteral("DEM 重建完成：%1（%2x%3）").arg(dem->name()).arg(dem->width()).arg(dem->height()));
        }
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("DEM 重建提示：%1").arg(QString::fromUtf8(e.what())));
    }
}

void MainWindow::runOrthorectification() {
    auto raster = selectedRaster();
    if (!raster) {
        appendLog(QStringLiteral("正射校正提示：请先选中一个遥感影像图层。"));
        return;
    }

        // 从图层树中找第一个 DEM
    std::shared_ptr<DemLayer> demLayer;
    const auto orthoIndices = selectedLayerIndices();
    for (int idx : orthoIndices) {
        try {
            auto layer = layers_.at(idx);
            if (layer->type() == DataType::Dem) {
                demLayer = std::dynamic_pointer_cast<DemLayer>(layer);
                if (demLayer) break;
            }
        } catch (...) {}
    }

    // 如果选中的里面没有 DEM，就从所有图层里找
    if (!demLayer) {
        for (int i = 0; i < layers_.size(); ++i) {
            try {
                auto layer = layers_.at(i);
                if (layer->type() == DataType::Dem) {
                    demLayer = std::dynamic_pointer_cast<DemLayer>(layer);
                    if (demLayer) break;
                }
            } catch (...) {}
        }
    }

    if (!demLayer) {
        appendLog(QStringLiteral("正射校正提示：请先加载并选中 DEM 图层。"));
        return;
    }

    OrthorectificationPipeline pipeline;
    try {
        auto result = pipeline.rectify(*raster, *demLayer);
        appendLog(QStringLiteral("正射校正完成：%1").arg(result.message));
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("正射校正失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

void MainWindow::onSelectionChanged() {
    displayRaster(selectedRaster(), selectedBandIndex());
    updateActionStates();
}

void MainWindow::onLayerItemChanged(QTreeWidgetItem* item, int column) {
    if (rebuildingTree_ || !item || column != 0) return;
    if (static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt()) != NodeKind::Layer) return;
    
    const QVariant value = item->data(0, kLayerIndexRole);
    if (!value.isValid()) return;
    
    try {
        const bool visible = item->checkState(0) == Qt::Checked;
        layers_.at(value.toInt())->setVisible(visible);
        const auto layer = layers_.at(value.toInt());
        
        // 【核心修复】完美控制点云与 Mesh 3D 网格的可见性切换
        if (layer->type() == DataType::PointCloud) {
            if (visible) {
                const auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer);
                if (pc) scene3DWidget_->setPoints(pc->points());
            } else {
                scene3DWidget_->setPoints({});
            }
        } else if (layer->type() == DataType::Mesh) {
            if (visible) {
                const auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer);
                if (mesh) scene3DWidget_->setMesh(mesh->vertices(), mesh->faces());
            } else {
                scene3DWidget_->setMesh({}, {});
            }
        }
        appendLog(QStringLiteral("%1：%2").arg(item->text(0), visible ? QStringLiteral("显示") : QStringLiteral("隐藏")));
    } catch (const std::exception&) {}
}

void MainWindow::showLayerContextMenu(const QPoint& position) {
    QTreeWidgetItem* item = layerTree_->itemAt(position);
    if (!item) return;
    
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
                const int demLayerIndex = layerIndex.toInt();
                connect(exportDem, &QAction::triggered, this, [this, demLayerIndex]() {
                    try {
                        const auto dem = std::dynamic_pointer_cast<DemLayer>(layers_.at(demLayerIndex));
                        if (!dem) throw std::runtime_error("当前图层不是 DEM");
                        
                        const QString path = QFileDialog::getSaveFileName(
                            this,
                            QStringLiteral("导出 DEM GeoTIFF"),
                            dem->name() + QStringLiteral(".tif"),
                            QStringLiteral("GeoTIFF (*.tif *.tiff);;All Files (*.*)"));
                        if (path.isEmpty()) return;
                        
                        rs::io::RasterWriteOptions options;
                        options.driverName = QStringLiteral("GTiff");
                        options.creationOptions = QStringLiteral("COMPRESS=LZW");
                        rs::io::exportDemAsGeoTiff(*dem, path, options);
                        appendLog(QStringLiteral("已导出 DEM：%1").arg(path));
                    } catch (const std::exception& e) {
                        appendLog(QStringLiteral("DEM 导出失败：%1").arg(QString::fromUtf8(e.what())));
                    }
                });
            }
        } catch (const std::exception&) {}
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
        case DataType::Raster: parent = rasterFolder; break;
        case DataType::PointCloud: parent = pointFolder; break;
        case DataType::Mesh: parent = meshFolder; break;
        case DataType::Dem: parent = demFolder; break;
        case DataType::Result: parent = resultRoot; break;
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
        imageScene_->addText(QStringLiteral("当前影像没有可显示的渲染结果。\n当前图层：%1").arg(raster->name()));
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
        if (!value.isValid()) continue;
        
        const int index = value.toInt();
        if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
            indices.push_back(index);
        }
    }
    return indices;
}

std::shared_ptr<RasterLayer> MainWindow::selectedRaster() const {
    auto* item = layerTree_->currentItem();
    if (!item) return {};
    
    const QVariant value = item->data(0, kLayerIndexRole);
    if (!value.isValid()) return {};
    
    try {
        return std::dynamic_pointer_cast<RasterLayer>(layers_.at(value.toInt()));
    } catch (const std::exception&) {
        return {};
    }
}

int MainWindow::selectedBandIndex() const {
    auto* item = layerTree_->currentItem();
    if (!item) return -1;
    const QVariant value = item->data(0, kBandIndexRole);
    return value.isValid() ? value.toInt() : -1;
}

void MainWindow::updateActionStates() {
    int selectedRasters = 0;
    int selectedDems = 0;

    for (auto* item : layerTree_->selectedItems()) {
        QVariant indexVar = item->data(0, kLayerIndexRole);
        if (indexVar.isValid()) {
            try {
                auto layer = layers_.at(indexVar.toInt());
                if (std::dynamic_pointer_cast<RasterLayer>(layer)) selectedRasters++;
                else if (layer->type() == DataType::Dem) selectedDems++;
            } catch (...) {}
        }
    }

    // 将强制禁用的逻辑改为“弱激活”：
    // 如果没有选中项，我们不禁用菜单，而是允许点击，点击后在具体算法里弹出“请选择数据”的提示
    if (deleteLayerAction_) deleteLayerAction_->setEnabled(!layers_.empty());
    if (clearProjectAction_) clearProjectAction_->setEnabled(!layers_.empty());
    
    // 【强制点亮】将这些动作永久激活，点击后的逻辑由算法内部 check
    if (renderAction_) renderAction_->setEnabled(true);
    if (histogramAction_) histogramAction_->setEnabled(true);
    if (equalizeAction_) equalizeAction_->setEnabled(true);
    if (featureAction_) featureAction_->setEnabled(true);
        if (demAction_) demAction_->setEnabled(true);
    if (orthoAction_) orthoAction_->setEnabled(true);
        if (exportPlyAction_) exportPlyAction_->setEnabled(true);
    if (clearPointAction_) clearPointAction_->setEnabled(true);
    if (downsampleAction_) downsampleAction_->setEnabled(true);
    if (filterAction_) filterAction_->setEnabled(true);
    if (pcToDemAction_) pcToDemAction_->setEnabled(true);
}

void MainWindow::appendLog(const QString& text) {
    logEdit_->append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

} // namespace rs

// ── 点云体素降采样 ──
void rs::MainWindow::runPointCloudDownsample() {
    const auto indices = selectedLayerIndices();
    std::shared_ptr<PointCloudLayer> pcLayer;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            pcLayer = std::dynamic_pointer_cast<PointCloudLayer>(layer);
            if (pcLayer) break;
        } catch (...) {}
    }
    if (!pcLayer) {
        appendLog(QStringLiteral("请先选中一个点云图层。"));
        return;
    }

    const auto& points = pcLayer->points();
    PointCloudFilterAlgorithm algo;
    auto filtered = algo.voxelDownsample(points, 0.1);

    auto newLayer = std::make_shared<PointCloudLayer>(
        pcLayer->name() + QStringLiteral("_降采样"), QString(), filtered);
    layers_.add(newLayer);
    scene3DWidget_->setPoints(filtered);
    tabs_->setCurrentWidget(scene3DWidget_);
    refreshLayerTree();
    appendLog(QStringLiteral("体素降采样完成：%1 → %2 个点").arg(points.size()).arg(filtered.size()));
}

// ── 点云统计滤波 ──
void rs::MainWindow::runPointCloudFilter() {
    const auto indices = selectedLayerIndices();
    std::shared_ptr<PointCloudLayer> pcLayer;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            pcLayer = std::dynamic_pointer_cast<PointCloudLayer>(layer);
            if (pcLayer) break;
        } catch (...) {}
    }
    if (!pcLayer) {
        appendLog(QStringLiteral("请先选中一个点云图层。"));
        return;
    }

    const auto& points = pcLayer->points();
    PointCloudFilterAlgorithm algo;
    auto filtered = algo.statisticalOutlierRemoval(points, 20, 2.0);

    auto newLayer = std::make_shared<PointCloudLayer>(
        pcLayer->name() + QStringLiteral("_滤波"), QString(), filtered);
    layers_.add(newLayer);
    scene3DWidget_->setPoints(filtered);
    tabs_->setCurrentWidget(scene3DWidget_);
    refreshLayerTree();
    appendLog(QStringLiteral("统计滤波完成：%1 → %2 个点").arg(points.size()).arg(filtered.size()));
}

// ── 点云转 DEM ──
void rs::MainWindow::runPointCloudToDem() {
    const auto indices = selectedLayerIndices();
    std::shared_ptr<PointCloudLayer> pcLayer;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            pcLayer = std::dynamic_pointer_cast<PointCloudLayer>(layer);
            if (pcLayer) break;
        } catch (...) {}
    }
    if (!pcLayer) {
        appendLog(QStringLiteral("请先选中一个点云图层。"));
        return;
    }

    const auto& points = pcLayer->points();
        PointCloudToDemAlgorithm algo;
    PointCloudToDemAlgorithm::Parameters params;
    params.gridResolution = 1.0;
    params.useMaxZ = true;

    auto dem = algo.convert(points, params, pcLayer->name() + QStringLiteral("_DSM"));
    if (dem) {
        layers_.add(std::static_pointer_cast<DataObject>(dem));
        refreshLayerTree();
        appendLog(QStringLiteral("点云转 DEM 完成：%1（%2x%3）").arg(dem->name()).arg(dem->width()).arg(dem->height()));
    }
}

// ── 导出选中的点云/Mesh 为 PLY ──
void rs::MainWindow::exportPly() {
    const auto indices = selectedLayerIndices();
    if (indices.empty()) {
        appendLog(QStringLiteral("导出 PLY 提示：请先选中一个点云或 Mesh 图层。"));
        return;
    }

    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("导出 PLY"), layer->name() + QStringLiteral(".ply"),
                QStringLiteral("PLY (*.ply);;All Files (*.*)"));
            if (path.isEmpty()) continue;

            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                appendLog(QStringLiteral("无法创建文件: %1").arg(path));
                continue;
            }

            QTextStream out(&file);

            if (auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer)) {
                // 导出点云
                const auto& points = pc->points();
                out << "ply\nformat ascii 1.0\n"
                    << "element vertex " << points.size() << "\n"
                    << "property float x\nproperty float y\nproperty float z\n"
                    << "end_header\n";
                for (const auto& p : points) {
                    out << p.x() << " " << p.y() << " " << p.z() << "\n";
                }
                appendLog(QStringLiteral("已导出点云 PLY：%1（%2 个点）").arg(path).arg(points.size()));
            } else if (auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer)) {
                // 导出 Mesh
                const auto& verts = mesh->vertices();
                const auto& faces = mesh->faces();
                out << "ply\nformat ascii 1.0\n"
                    << "element vertex " << verts.size() << "\n"
                    << "property float x\nproperty float y\nproperty float z\n"
                    << "element face " << faces.size() << "\n"
                    << "property list uchar int vertex_indices\n"
                    << "end_header\n";
                for (const auto& v : verts) {
                    out << v.x() << " " << v.y() << " " << v.z() << "\n";
                }
                for (const auto& f : faces) {
                    out << "3 " << f.a << " " << f.b << " " << f.c << "\n";
                }
                appendLog(QStringLiteral("已导出 Mesh PLY：%1（%2 顶点，%3 面）")
                    .arg(path).arg(verts.size()).arg(faces.size()));
            } else {
                appendLog(QStringLiteral("选中图层不是点云或 Mesh，无法导出。"));
            }
            file.close();
        } catch (const std::exception& e) {
            appendLog(QStringLiteral("导出失败：%1").arg(QString::fromUtf8(e.what())));
        }
    }
}
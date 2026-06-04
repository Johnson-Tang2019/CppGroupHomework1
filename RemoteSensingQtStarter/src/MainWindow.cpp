#include "rs/MainWindow.h"

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

// 生成节点的唯一键（从根到当前节点的路径字符串）
QString itemKey(const QTreeWidgetItem* item) {
    QStringList parts;               // 用于存储从根到当前节点的各层名称
    const auto* current = item;      // 从当前节点开始向上遍历
    while (current) {                // 一直遍历到根节点（parent 为 nullptr）
        parts.prepend(current->text(0));  // 把当前节点的文本插入到列表最前面
        current = current->parent();      // 向上移动到父节点
    }
    return parts.join(QLatin1Char('/'));  // 用 "/" 拼接成路径字符串，如 "源数据/遥感影像/xxx.tif"
}

// 递归收集所有展开节点的键
void collectExpandedKeys(QTreeWidgetItem* item, QSet<QString>& keys) {
    if (!item) {                     // 空节点，直接返回
        return;
    }
    if (item->isExpanded()) {        // 如果当前节点是展开状态
        keys.insert(itemKey(item));  // 则记录它的路径键
    }
    for (int i = 0; i < item->childCount(); ++i) {  // 遍历所有子节点
        collectExpandedKeys(item->child(i), keys);  // 递归处理每个子节点
    }
}

// 在指定父节点下查找或创建子文件夹
QTreeWidgetItem* ensureChildFolder(QTreeWidgetItem* parent, const QString& name) {
    // 先在现有子节点中查找是否已有同名文件夹
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name) {  // 找到了同名的
            return parent->child(i);               // 直接返回已有的节点
        }
    }
    // 没找到，则创建新的文件夹节点
    auto* folder = new QTreeWidgetItem(parent);                      // 创建新节点，parent 为父节点
    folder->setText(0, name);                                         // 设置显示文本为文件夹名
    folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));  // 标记为 Folder 类型
    folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);  // 移除"可选中"标志，保留"启用"标志
    return folder;
}

// 在顶层节点中查找或创建文件夹
QTreeWidgetItem* ensureTopFolder(QTreeWidget* tree, const QString& name) {
    // 先在所有顶层节点中查找是否已有同名文件夹
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == name) {  // 找到了同名的
            return tree->topLevelItem(i);                // 直接返回已有的节点
        }
    }
    // 没找到，则创建新的顶层文件夹节点
    auto* folder = new QTreeWidgetItem(tree);                       // 创建新节点，tree 为根
    folder->setText(0, name);                                        // 设置显示文本为文件夹名
    folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));  // 标记为 Folder 类型
    folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);  // 不可选中，仅启用
    return folder;
}

} // namespace

// 构造函数：初始化窗口标题、菜单、UI 布局，记录启动日志
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Remote Sensing Qt Starter"));  // 设置窗口标题
    createMenus();    // 构建菜单栏
    createUi();       // 构建界面控件
    appendLog(QStringLiteral("Starter 已启动：当前版本提供 GDAL 多波段、参数化算法、DEM/正射流程的工程骨架。"));  // 记录启动日志
    updateActionStates();  // 初始化菜单项的启用状态（刚启动时所有菜单应该禁用）
}

// 构建菜单栏：数据、影像处理、摄影测量/三维
void MainWindow::createMenus() {
    // ---- "数据" 菜单 ----
    auto* dataMenu = menuBar()->addMenu(QStringLiteral("数据"));  // 创建"数据"菜单
    connect(dataMenu->addAction(QStringLiteral("加载遥感影像(GDAL，可多选)")), &QAction::triggered, this, &MainWindow::openRasterDatasets);  // 添加"加载遥感影像"并连接点击信号
    connect(dataMenu->addAction(QStringLiteral("加载点云")), &QAction::triggered, this, &MainWindow::openPointCloud);  // 添加"加载点云"并连接
    connect(dataMenu->addAction(QStringLiteral("加载 Mesh")), &QAction::triggered, this, &MainWindow::openMesh);  // 添加"加载 Mesh"并连接
    connect(dataMenu->addAction(QStringLiteral("加载 DEM")), &QAction::triggered, this, &MainWindow::openDem);  // 添加"加载 DEM"并连接
    dataMenu->addSeparator();  // 添加分隔线，将加载与删除操作分开
    deleteLayerAction_ = dataMenu->addAction(QStringLiteral("删除选中图层"));  // 添加"删除选中图层"并保存指针以便控制启用/禁用
    connect(deleteLayerAction_, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
    clearProjectAction_ = dataMenu->addAction(QStringLiteral("初始化/清空工程"));  // 添加"清空工程"并保存指针
    connect(clearProjectAction_, &QAction::triggered, this, &MainWindow::clearProject);

    // ---- "影像处理" 菜单 ----
    auto* rasterMenu = menuBar()->addMenu(QStringLiteral("影像处理"));  // 创建"影像处理"菜单
    auto* bandMenu = rasterMenu->addMenu(QStringLiteral("波段与设色"));  // 创建子菜单"波段与设色"
    renderAction_ = bandMenu->addAction(QStringLiteral("波段组合/设色..."));  // 添加"波段组合/设色"并保存指针
    connect(renderAction_, &QAction::triggered, this, &MainWindow::configureRasterRendering);

    auto* statMenu = rasterMenu->addMenu(QStringLiteral("统计"));  // 创建子菜单"统计"
    histogramAction_ = statMenu->addAction(QStringLiteral("灰度直方图..."));  // 添加"灰度直方图"并保存指针
    connect(histogramAction_, &QAction::triggered, this, &MainWindow::runHistogram);

    auto* enhanceMenu = rasterMenu->addMenu(QStringLiteral("增强"));  // 创建子菜单"增强"
    equalizeAction_ = enhanceMenu->addAction(QStringLiteral("直方图均衡化..."));  // 添加"直方图均衡化"并保存指针
    connect(equalizeAction_, &QAction::triggered, this, &MainWindow::runHistogramEqualization);

    auto* featureMenu = rasterMenu->addMenu(QStringLiteral("特征"));  // 创建子菜单"特征"
    featureAction_ = featureMenu->addAction(QStringLiteral("ORB/SIFT 特征提取..."));  // 添加"ORB/SIFT 特征提取"并保存指针
    connect(featureAction_, &QAction::triggered, this, &MainWindow::runFeatureExtraction);

    // ---- "摄影测量/三维" 菜单 ----
    auto* photogrammetryMenu = menuBar()->addMenu(QStringLiteral("摄影测量/三维"));  // 创建"摄影测量/三维"菜单
    demAction_ = photogrammetryMenu->addAction(QStringLiteral("DEM 重建..."));  // 添加"DEM 重建"并保存指针
    connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
    orthoAction_ = photogrammetryMenu->addAction(QStringLiteral("正射影像校正..."));  // 添加"正射影像校正"并保存指针
    connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);
}

// 构建界面布局：左侧图层树 + 右侧影像/三维标签页 + 底部日志面板
void MainWindow::createUi() {
    // 主分割器：水平方向，将窗口分为左侧（图层树）和右侧（影像+日志）
    auto* root = new QSplitter(Qt::Horizontal, this);
    // ---- 左侧：图层树 ----
    layerTree_ = new QTreeWidget(root);          // 创建图层树控件
    layerTree_->setHeaderLabel(QStringLiteral("工程图层"));  // 设置表头文字
    layerTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);  // 支持 Ctrl/Shift 多选
    layerTree_->setContextMenuPolicy(Qt::CustomContextMenu);  // 启用自定义右键菜单
    connect(layerTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);  // 选中项改变时刷新影像
    connect(layerTree_, &QTreeWidget::itemChanged, this, &MainWindow::onLayerItemChanged);  // 勾选框改变时切换可见性
    connect(layerTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showLayerContextMenu);  // 右键弹出菜单

    // ---- 右侧：上下分割（上方影像标签页 + 下方日志） ----
    auto* right = new QSplitter(Qt::Vertical, root);  // 右侧垂直分割器

    // 标签页控件：二维影像 / 三维场景
    tabs_ = new QTabWidget(right);              // 创建标签页控件
    imageScene_ = new QGraphicsScene(this);     // 创建图形场景（管理所有图形项）

    imageView_ = new QGraphicsView(imageScene_, tabs_);  // 创建图形视图（显示场景内容）
    imageView_->setDragMode(QGraphicsView::ScrollHandDrag);  // 设置拖拽模式：手型抓手平移
    imageView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);  // 缩放时以鼠标位置为中心

    // 三维场景页：QOpenGLWidget 点云预览
    scene3DWidget_ = new Scene3DWidget(tabs_);

    tabs_->addTab(imageView_, QStringLiteral("二维影像"));  // 添加"二维影像"标签页
    tabs_->addTab(scene3DWidget_, QStringLiteral("三维场景"));  // 添加"三维场景"标签页

    // 日志输出面板
    logEdit_ = new QTextEdit(right);             // 创建文本编辑框用于日志输出
    logEdit_->setReadOnly(true);                  // 设置为只读，用户不能编辑
    logEdit_->setMaximumHeight(210);              // 限制最大高度 210 像素

    // 设置分割器拉伸比例（控件随窗口缩放时的比例分配）
    root->setStretchFactor(0, 1);   // 第0个（图层树）：拉伸因子 = 1
    root->setStretchFactor(1, 5);   // 第1个（右侧区域）：拉伸因子 = 5
    right->setStretchFactor(0, 5);  // 第0个（影像标签页）：拉伸因子 = 5
    right->setStretchFactor(1, 1);  // 第1个（日志面板）：拉伸因子 = 1

    setCentralWidget(root);  // 将分割器设为窗口的中心控件（填满整个窗口）
}

// 打开文件对话框选择遥感影像，使用 GDAL 读取并加载到图层管理器
void MainWindow::openRasterDatasets() {
    // 弹出文件选择对话框，支持多选，过滤遥感影像格式
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,                                              // 父窗口
        QStringLiteral("加载遥感影像"),                      // 对话框标题
        QString(),                                          // 默认路径（空 = 上次路径）
        QStringLiteral("Remote sensing rasters (*.tif *.tiff *.img *.dat *.jp2 *.jpg *.jpeg *.png *.bmp);;All Files (*.*)"));  // 文件过滤器
    if (paths.isEmpty()) {    // 用户取消选择
        return;               // 不做任何操作
    }

    // 遍历所有选中的文件路径
    for (const QString& path : paths) {
        const QFileInfo info(path);                     // 获取文件信息（文件名、后缀等）
        try {
            // 调用 RasterIO 中的 GDAL 读取函数，读取波段、投影、地理变换和缩略图
            auto raster = rs::io::loadRasterDataset(path);
            if (raster) {
                layers_.add(raster);                            // 将图层添加到 LayerManager 中
                appendLog(QStringLiteral("已加载影像：%1（%2 波段，%3x%4）")
                    .arg(info.fileName())
                    .arg(raster->bandCount())
                    .arg(raster->bandCount() > 0 ? raster->band(0).width : 0)
                    .arg(raster->bandCount() > 0 ? raster->band(0).height : 0));
            }
        } catch (const std::exception& e) {
            // GDAL 读取失败时记录错误信息
            appendLog(QStringLiteral("加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
        }
    }
    refreshLayerTree();    // 刷新图层树显示新添加的图层
    updateActionStates();  // 更新菜单启用状态
}

// 加载点云：支持 PLY、XYZ、LAS 格式
void MainWindow::openPointCloud() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载点云"),
        QString(),
        QStringLiteral("Point Cloud (*.ply *.xyz *.las);;All Files (*.*)"));
    if (path.isEmpty()) return;

    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();

    try {
        QVector<QVector3D> points;

        if (ext == QStringLiteral("xyz")) {
            // XYZ 文本格式：每行一个点 "x y z"
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("无法打开文件");
            }
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
            // 简易 PLY 读取：只读顶点（支持 ASCII 格式）
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("无法打开 PLY 文件");
            }
            QTextStream in(&file);
            int vertexCount = 0;
            bool headerEnd = false;
            int readVertices = 0;
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (!headerEnd) {
                    if (line.startsWith(QStringLiteral("element vertex"))) {
                        vertexCount = line.section(QLatin1Char(' '), 2, 2).toInt();
                    } else if (line == QStringLiteral("end_header")) {
                        headerEnd = true;
                        points.reserve(vertexCount);
                    }
                    continue;
                }
                if (readVertices >= vertexCount) break;
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    points.append(QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
                }
                ++readVertices;
            }
            file.close();
        } else if (ext == QStringLiteral("las")) {
            points = readLasPoints(path);
        } else {
            throw std::runtime_error("不支持的格式: " + ext.toStdString() + "，仅支持 PLY/XYZ/LAS");
        }

        if (points.isEmpty()) {
            throw std::runtime_error("未能读取到任何点数据");
        }

        auto layer = std::make_shared<PointCloudLayer>(info.fileName(), path, points);
        layers_.add(layer);
        appendLog(QStringLiteral("已加载点云：%1（%2 个点）").arg(info.fileName()).arg(points.size()));

        // 在三维窗口中显示点云（由本组同学添加）
        scene3DWidget_->setPoints(points);
        tabs_->setCurrentWidget(scene3DWidget_);
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("点云加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载 Mesh：支持 OBJ、PLY 格式
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
            // OBJ 格式：v x y z（顶点），f v1 v2 v3（三角面）
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("无法打开 OBJ 文件");
            }
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
                    // OBJ 索引从 1 开始，需要减 1
                    face.a = parts[1].section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    face.b = parts[2].section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    face.c = parts[3].section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    faces.append(face);
                }
            }
            file.close();
        } else if (ext == QStringLiteral("ply")) {
            // PLY 格式（同点云读取方式，但同时读取面片信息）
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("无法打开 PLY 文件");
            }
            QTextStream in(&file);
            int vertexCount = 0, faceCount = 0;
            bool headerEnd = false;
            int readVerts = 0, readFaces = 0;
            bool readingVerts = true;

            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (!headerEnd) {
                    if (line.startsWith(QStringLiteral("element vertex"))) {
                        vertexCount = line.section(QLatin1Char(' '), 2, 2).toInt();
                    } else if (line.startsWith(QStringLiteral("element face"))) {
                        faceCount = line.section(QLatin1Char(' '), 2, 2).toInt();
                    } else if (line == QStringLiteral("end_header")) {
                        headerEnd = true;
                        vertices.reserve(vertexCount);
                        faces.reserve(faceCount);
                    }
                    continue;
                }

                if (readingVerts && readVerts < vertexCount) {
                    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (parts.size() >= 3) {
                        vertices.append(QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
                    }
                    ++readVerts;
                    if (readVerts >= vertexCount) readingVerts = false;
                } else if (readFaces < faceCount) {
                    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
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
            file.close();
        } else {
            throw std::runtime_error("不支持的格式: " + ext.toStdString() + "，仅支持 OBJ/PLY");
        }

        if (vertices.isEmpty()) {
            throw std::runtime_error("未能读取到任何顶点数据");
        }

        auto layer = std::make_shared<MeshLayer>(info.fileName(), path, vertices, faces);
        layers_.add(layer);
        appendLog(QStringLiteral("已加载 Mesh：%1（%2 个顶点，%3 个三角面）")
            .arg(info.fileName()).arg(vertices.size()).arg(faces.size()));
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("Mesh 加载失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载 DEM：使用 GDAL 读取 DEM GeoTIFF/ASCII Grid 格式
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
        // 使用 GDAL 读取 DEM 文件
        GDALAllRegister();
        GDALDataset* dataset = static_cast<GDALDataset*>(
            GDALOpenEx(path.toUtf8().constData(), GA_ReadOnly, nullptr, nullptr, nullptr));
        if (!dataset) {
            throw std::runtime_error("无法打开 DEM 文件");
        }

        const int width = dataset->GetRasterXSize();
        const int height = dataset->GetRasterYSize();
        const int bandCount = dataset->GetRasterCount();

        if (width <= 0 || height <= 0 || bandCount < 1) {
            GDALClose(dataset);
            throw std::runtime_error("DEM 尺寸或波段数无效");
        }

        // 读取地理变换
        std::array<double, 6> geoTransform = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
        if (dataset->GetGeoTransform(geoTransform.data()) != CE_None) {
            geoTransform = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
        }

        // 读取高程数据（取第一个波段）
        const int pixelCount = width * height;
        QVector<float> elevations(pixelCount);
        GDALRasterBand* gdalBand = dataset->GetRasterBand(1);
        CPLErr err = gdalBand->RasterIO(GF_Read, 0, 0, width, height,
                                         elevations.data(), width, height,
                                         GDT_Float32, 0, 0, nullptr);
        GDALClose(dataset);

        if (err != CE_None) {
            throw std::runtime_error("读取 DEM 像素数据失败");
        }

        // 创建 DEM 图层
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

// 删除图层树中选中的图层
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

// 清空所有图层，重置工程
void MainWindow::clearProject() {
    layers_.clear();
    imageScene_->clear();
    refreshLayerTree();
    appendLog(QStringLiteral("工程已初始化。"));
    updateActionStates();
}

// 打开波段组合/设色对话框
void MainWindow::configureRasterRendering() {
    const auto raster = selectedRaster();
    if (!raster) {
        return;
    }
    const auto request = askRasterRenderRequest(this, *raster);
    if (!request.has_value()) {
        return;
    }

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

// 执行灰度直方图算法（TODO）
void MainWindow::runHistogram() {
    HistogramAlgorithm algorithm;
    appendLog(QStringLiteral("TODO: 打开参数对话框并执行：%1，当前波段索引=%2。")
                  .arg(algorithm.name())
                  .arg(selectedBandIndex()));
}

// 执行直方图均衡化算法（TODO）
void MainWindow::runHistogramEqualization() {
    HistogramEqualizationAlgorithm algorithm;
    appendLog(QStringLiteral("TODO: 打开参数对话框并执行：%1，结果应加入“处理结果/直方图均衡化”。").arg(algorithm.name()));
}

// 执行 ORB/SIFT 特征提取（TODO）
void MainWindow::runFeatureExtraction() {
    FeatureExtractionAlgorithm algorithm;
    appendLog(QStringLiteral("TODO: 打开参数对话框并执行：%1，可选 ORB/SIFT/AKAZE。").arg(algorithm.name()));
}

// 执行 DEM 重建流程（TODO）
void MainWindow::runDemReconstruction() {
    appendLog(QStringLiteral("TODO: 选择两张影像后弹出 DEM 重建对话框，补充相机参数/控制点/输出目录，再自动完成匹配、校正、视差和 DEM 输出。"));
}

// 执行正射校正流程（TODO）
void MainWindow::runOrthorectification() {
    appendLog(QStringLiteral("TODO: 选择影像和对应 DEM，使用 DEM 地理变换与影像模型重采样生成正射影像。"));
}

// 当图层树选中项改变时，刷新影像显示和菜单状态
void MainWindow::onSelectionChanged() {
    displayRaster(selectedRaster(), selectedBandIndex());
    updateActionStates();
}

// 当图层项的勾选状态改变时，切换其可见性
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

// 右键点击图层树时弹出上下文菜单
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
                const int demLayerIndex = layerIndex.toInt();
                connect(exportDem, &QAction::triggered, this, [this, demLayerIndex]() {
                    try {
                        const auto dem = std::dynamic_pointer_cast<DemLayer>(layers_.at(demLayerIndex));
                        if (!dem) {
                            throw std::runtime_error("当前图层不是 DEM");
                        }
                        const QString path = QFileDialog::getSaveFileName(
                            this,
                            QStringLiteral("导出 DEM GeoTIFF"),
                            dem->name() + QStringLiteral(".tif"),
                            QStringLiteral("GeoTIFF (*.tif *.tiff);;All Files (*.*)"));
                        if (path.isEmpty()) {
                            return;
                        }
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
        } catch (const std::exception&) {
        }
    }
    menu.exec(layerTree_->viewport()->mapToGlobal(position));
}

// 根据 LayerManager 中的数据重建图层树，保持展开/折叠状态
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

// 在 QGraphicsView 中显示选中的影像（优先显示选中波段）
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

// 获取当前选中的所有图层的索引列表（去重）
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

// 获取当前选中的 RasterLayer（非 Raster 类型返回 nullptr）
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

// 获取当前选中的波段索引（-1 表示未选中具体波段）
int MainWindow::selectedBandIndex() const {
    auto* item = layerTree_->currentItem();
    if (!item) {
        return -1;
    }
    const QVariant value = item->data(0, kBandIndexRole);
    return value.isValid() ? value.toInt() : -1;
}

// 根据当前选中图层的类型，更新菜单项的启用/禁用状态
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

// 在日志面板追加带时间戳的信息
void MainWindow::appendLog(const QString& text) {
    logEdit_->append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

} // namespace rs

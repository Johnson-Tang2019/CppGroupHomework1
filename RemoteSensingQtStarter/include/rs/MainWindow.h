#pragma once // 防止头文件被重复包含

#include "rs/DataObject.h"   // DataObject 基类型，LayerManager 管理该类型
#include "rs/Geometry.h"     // RasterLayer 等子类型，用于类型转换和显示
#include "rs/LayerManager.h" // LayerManager 模板，管理图层的增删查改

#include <QAbstractItemView>
#include <QAction> // Qt 菜单操作类，用于菜单项的启用/禁用控制
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene> // Qt 图形场景，管理影像显示的所有图形项
#include <QGraphicsTextItem>
#include <QGraphicsView> // Qt 图形视图，在窗口中显示 GraphicsScene
#include <QLabel>
#include <QMainWindow> // Qt 主窗口基类，提供菜单栏、工具栏、状态栏等
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QSet>
#include <QSplitter>
#include <QStringList>
#include <QTabWidget> // Qt 标签页控件，切换二维影像/三维场景
#include <QTextEdit>  // Qt 文本编辑框，用于日志输出面板
#include <QTextStream>
#include <QTreeWidget> // Qt 树形控件，显示图层的文件夹层级结构
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#ifdef RS_WITH_GDAL
#include <gdal_priv.h>
#endif
#include <limits>
#include <memory>
#include <memory> // std::shared_ptr，管理图层对象的共享所有权
#include <stdexcept>
#include <vector> // std::vector，存储选中的图层索引列表

class Scene3DWidget; // 前向声明 3D 点云预览控件

namespace rs { // 遥感（Remote Sensing）命名空间

// MainWindow：应用程序的主窗口
// 继承 QMainWindow，包含完整的 UI 界面：菜单栏、图层树、影像显示区、日志面板
// 使用 Q_OBJECT 宏启用 Qt 信号/槽机制
class MainWindow final : public QMainWindow { // final 禁止进一步继承
  Q_OBJECT                                    // Qt 元对象编译器宏，启用信号/槽和元对象功能

      public : explicit MainWindow(QWidget *parent = nullptr); // 构造函数，可指定父窗口

  private slots:
    // ============ 数据加载（菜单项对应的槽函数） ============
        void openRasterDatasets(); // 打开文件对话框加载遥感影像（GDAL，支持多选）
    void openPointCloud();     // 加载点云数据（PLY/LAS/XYZ）
    void openMesh();           // 加载三维网格模型（OBJ/PLY）
    void openDem();            // 加载数字高程模型（GeoTIFF/ASCII Grid）

    // ============ 数据管理 ============
    void deleteSelectedLayers(); // 删除图层树中当前选中的所有图层
    void clearProject();         // 清空所有图层，重置工程

    // ============ 影像处理 ============
    void configureRasterRendering(); // 打开波段组合/设色设置对话框
    void runHistogram();             // 执行灰度直方图统计
    void runHistogramEqualization(); // 执行直方图均衡化
    void runFeatureExtraction();     // 执行 ORB/SIFT 特征提取

    // ============ 摄影测量/三维处理 ============
    void runDemReconstruction();  // 执行 DEM 重建（立体像对）
    void runOrthorectification(); // 执行正射影像校正

    // ============ 三维点云/Mesh ============
    void exportPly();                 // 导出选中的点云/Mesh 为 PLY
    void runPointCloudDownsample();   // 点云降采样
    void runPointCloudFilter();       // 点云统计滤波
    void runPointCloudToDem();        // 点云转 DEM

    // ============ 界面交互 ============
    void onSelectionChanged();                                  // 图层树选中项改变时的响应
    void onLayerItemChanged(QTreeWidgetItem *item, int column); // 图层项（勾选框）改变时的响应
    void showLayerContextMenu(const QPoint &position);          // 右键上下文菜单

  private:
    // ============ UI 构建 ============
    void createUi();    // 构建界面布局（分割器、图层树、标签页、日志面板）
    void createMenus(); // 构建菜单栏（数据、影像处理、摄影测量/三维）

    // ============ 图层管理 ============
    void refreshLayerTree(); // 根据 LayerManager 数据重建图层树（保持展开状态）

    // ============ 影像显示 ============
    void displayRaster(const std::shared_ptr<RasterLayer> &raster,
                       int bandIndex); // 在 QGraphicsView 中显示影像

    // ============ 日志 ============
    void appendLog(const QString &text); // 在日志面板追加带时间戳的消息

    // ============ 状态管理 ============
    void updateActionStates(); // 根据选中图层类型更新菜单项的启用/禁用状态

    // ============ 查询辅助 ============
    std::vector<int> selectedLayerIndices() const; // 获取当前选中的所有图层索引（去重）
    std::shared_ptr<RasterLayer>
    selectedRaster() const;        // 获取当前选中的 RasterLayer（非影像返回 nullptr）
    int selectedBandIndex() const; // 获取当前选中的波段索引（-1 表示未选中具体波段）

    // ============ UI 控件指针 ============
    QTreeWidget *layerTree_{};       // 图层树控件（左侧面板）
    QGraphicsView *imageView_{};     // 影像显示控件（二维影像标签页中的图形视图）
    QGraphicsScene *imageScene_{};   // 图形场景（管理所有图形项）
    Scene3DWidget *scene3DWidget_{}; // 三维点云预览控件（基于 QOpenGLWidget）
    QTabWidget *tabs_{};             // 标签页控件（切换"二维影像"和"三维场景"）
    QTextEdit *logEdit_{};           // 日志输出面板（底部）

    // ============ 菜单项指针（用于启用/禁用控制） ============
    QAction *deleteLayerAction_{};  // "删除选中图层"菜单项
    QAction *clearProjectAction_{}; // "初始化/清空工程"菜单项
    QAction *renderAction_{};       // "波段组合/设色"菜单项
    QAction *histogramAction_{};    // "灰度直方图"菜单项
    QAction *equalizeAction_{};     // "直方图均衡化"菜单项
    QAction *featureAction_{};      // "ORB/SIFT 特征提取"菜单项
    QAction *demAction_{};          // "DEM 重建"菜单项
    QAction *orthoAction_{};        // "正射影像校正"菜单项
    QAction* exportPlyAction_ {};        // "导出 PLY"菜单项
    QAction* clearPointAction_ {};       // "清空三维场景"菜单项
    QAction* downsampleAction_ {};       // "体素降采样"菜单项
    QAction* filterAction_ {};           // "统计滤波"菜单项
    QAction* pcToDemAction_ {};          // "点云转 DEM"菜单项

    // ============ 状态变量 ============
    bool rebuildingTree_{};           // 正在重建图层树的标志（刷新过程中阻止重复响应）
    LayerManager<DataObject> layers_; // 图层管理器，管理所有数据图层（影像、点云、DEM 等）
};

} // namespace rs

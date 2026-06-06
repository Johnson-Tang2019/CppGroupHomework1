#ifndef RS_SCENE3DWIDGET_H
#define RS_SCENE3DWIDGET_H

#include "rs/DataObject.h"
#include "rs/Geometry.h" // 确保引用了命名空间

#include <QOpenGLWidget>
#include <QVector3D>
#include <QVector>

class Scene3DWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit Scene3DWidget(QWidget *parent = nullptr);

    // 修复：显式使用 rs::Face 确保编译器能找到定义
    void setMesh(const QVector<QVector3D>& vertices, const QVector<rs::Face>& faces);
    void setPoints(const QVector<QVector3D>& points);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QVector<QVector3D> m_points;      // 保存点云
    QVector<QVector3D> meshVertices_; // 保存 Mesh 顶点
    QVector<rs::Face> meshFaces_;     // 保存 Mesh 面片

    float m_rotX = 0, m_rotY = 0, m_zoom = 1.0f;
    QPoint m_lastPos;
};

#endif
#include "rs/Scene3DWidget.h"
#include <GL/gl.h>
#include <QImage>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdint>

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c) {
    QVector3D ab = b - a;
    QVector3D ac = c - a;
    QVector3D n = QVector3D::crossProduct(ab, ac);
    float len = n.length();
    if (len > 1e-10f) n /= len;
    return n;
}

static void computeBounds(const QVector<QVector3D> &points,
                          const QVector<QVector3D> &meshVerts,
                          QVector3D &center, float &halfExtent) {
    float cx = 0, cy = 0, cz = 0;
    int count = 0;

    auto addPoint = [&](float x, float y, float z) {
        cx += x; cy += y; cz += z;
        ++count;
    };

    for (const auto &p : points) addPoint(p.x(), p.y(), p.z());
    for (const auto &v : meshVerts) addPoint(v.x(), v.y(), v.z());

    if (count == 0) {
        center = QVector3D(0, 0, 0);
        halfExtent = 1.0f;
        return;
    }

    cx /= count; cy /= count; cz /= count;
    center = QVector3D(cx, cy, cz);

    float maxDist = 0;
    for (const auto &p : points) {
        float dx = p.x() - cx, dy = p.y() - cy, dz = p.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    for (const auto &v : meshVerts) {
        float dx = v.x() - cx, dy = v.y() - cy, dz = v.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    halfExtent = maxDist > 0 ? maxDist : 1.0f;
}

Scene3DWidget::Scene3DWidget(QWidget *parent) : QOpenGLWidget(parent) {}

Scene3DWidget::~Scene3DWidget() {
    makeCurrent();
    if (m_meshDList) {
        glDeleteLists(m_meshDList, 1);
        m_meshDList = 0;
    }
    releaseMeshBuffers();
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_dlistValid = false;
    doneCurrent();
}

void Scene3DWidget::markDlistDirty() {
    m_dlistValid = false;
    m_meshBuffersValid = false;
}

void Scene3DWidget::setPoints(const QVector<QVector3D> &points) {
    m_points = points;
    if (!points.isEmpty()) {
        meshVertices_.clear();
        meshTexCoords_.clear();
        meshFaces_.clear();
        demTexture_ = QImage();
        m_textureDirty = true;
    }
    markDlistDirty();
    update();
}

void Scene3DWidget::setMesh(const QVector<QVector3D> &vertices, const QVector<rs::Face> &faces) {
    meshVertices_ = vertices;
    meshFaces_ = faces;
    meshTexCoords_.clear();
    demTexture_ = QImage();
    m_textureDirty = true;
    if (!vertices.isEmpty()) {
        m_points.clear();
    }
    markDlistDirty();
    update();
}

void Scene3DWidget::setMeshPreview(const QVector<QVector3D> &vertices, const QVector<rs::Face> &faces,
                                   int maxFaces) {
    Q_UNUSED(maxFaces);
    setMesh(vertices, faces);
}

void Scene3DWidget::setDem(const rs::DemLayer &dem, int maxGrid) {
    setDem(dem, QImage(), maxGrid);
}

void Scene3DWidget::setDem(const rs::DemLayer &dem, const QImage &texture, int maxGrid) {
    m_points.clear();
    meshVertices_.clear();
    meshTexCoords_.clear();
    meshFaces_.clear();
    demTexture_ = texture.isNull() ? QImage() : texture.convertToFormat(QImage::Format_RGBA8888);
    m_textureDirty = true;

    const int w = dem.width();
    const int h = dem.height();
    if (w <= 0 || h <= 0) return;

    const int stepX = std::max(1, w / maxGrid);
    const int stepY = std::max(1, h / maxGrid);
    const auto &elevs = dem.elevations();
    const auto gt = dem.geoTransform();

    float zMin = elevs[0], zMax = elevs[0];
    for (float z : elevs) { zMin = std::min(zMin, z); zMax = std::max(zMax, z); }
    const float spanX = static_cast<float>(std::hypot(w * gt[1], w * gt[4]));
    const float spanY = static_cast<float>(std::hypot(h * gt[2], h * gt[5]));
    const float horizontalSpan = std::max(spanX, spanY);
    // Keep all three axes in the same scene scale.  A modest 25% relief
    // exaggeration makes terrain readable without turning it into a wall.
    const float zRange = zMax - zMin;
    const float zScale = zRange > 1e-6f ? horizontalSpan * 0.25f / zRange : 0.0f;

    const int cols = (w + stepX - 1) / stepX;
    const int rows = (h + stepY - 1) / stepY;
    meshVertices_.reserve(cols * rows);
    meshTexCoords_.reserve(cols * rows);

    for (int gy = 0, y = 0; gy < rows; ++gy, y += stepY) {
        for (int gx = 0, x = 0; gx < cols; ++gx, x += stepX) {
            const int sx = std::min(x, w - 1);
            const int sy = std::min(y, h - 1);
            const float z = elevs[sy * w + sx];
            const float wx = static_cast<float>(gt[0] + sx * gt[1] + sy * gt[2]);
            const float wy = static_cast<float>(gt[3] + sx * gt[4] + sy * gt[5]);
            meshVertices_.append(QVector3D(wx, wy, (z - zMin) * zScale));

            const float u = cols > 1 ? static_cast<float>(gx) / static_cast<float>(cols - 1) : 0.0f;
            const float v = rows > 1 ? static_cast<float>(gy) / static_cast<float>(rows - 1) : 0.0f;
            meshTexCoords_.append(QPointF(u, v));
        }
    }

    for (int gy = 0; gy < rows - 1; ++gy) {
        for (int gx = 0; gx < cols - 1; ++gx) {
            const int i0 = gy * cols + gx;
            const int i1 = i0 + 1;
            const int i2 = i0 + cols;
            const int i3 = i2 + 1;
            meshFaces_.append({i0, i1, i2});
            meshFaces_.append({i1, i3, i2});
        }
    }
    // DEMs should open in an oblique terrain view. Do not inherit a previous
    // point-cloud side view, which makes the surface look stretched and flat.
    m_rotX = 55.0f;
    m_rotY = 0.0f;
    m_zoom = 1.0f;
    markDlistDirty();
    update();
}

void Scene3DWidget::clearData() {
    m_points.clear();
    meshVertices_.clear();
    meshTexCoords_.clear();
    meshFaces_.clear();
    demTexture_ = QImage();
    m_textureDirty = true;
    if (isValid()) {
        makeCurrent();
        if (m_meshDList) {
            glDeleteLists(m_meshDList, 1);
            m_meshDList = 0;
        }
        releaseMeshBuffers();
        doneCurrent();
    }
    m_cachedCenter = QVector3D(0, 0, 0);
    m_cachedHalfExtent = 1.0f;
    markDlistDirty();
    update();
    repaint();
}

void Scene3DWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    if (!m_points.isEmpty() || (!meshVertices_.isEmpty() && !meshFaces_.isEmpty())) {
        update();
    }
}

void Scene3DWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(1.0f, 0.94f, 0.96f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_NORMALIZE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    GLfloat ambient[] = {0.28f, 0.28f, 0.30f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glPointSize(2.0f);

    // OpenGL context 可能被重建，让显示列表在下次 paintGL 时重新编译
    m_dlistValid = false;
    m_meshBuffersValid = false;
    m_textureDirty = true;
}

void Scene3DWidget::releaseMeshBuffers() {
    if (m_meshVbo) {
        glDeleteBuffers(1, &m_meshVbo);
        m_meshVbo = 0;
    }
    if (m_meshIbo) {
        glDeleteBuffers(1, &m_meshIbo);
        m_meshIbo = 0;
    }
    m_meshBuffersValid = false;
    m_meshIndexCount = 0;
    m_meshHasTexCoords = false;
}

void Scene3DWidget::uploadTextureIfNeeded() {
    if (!m_textureDirty) {
        return;
    }

    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }

    if (!demTexture_.isNull()) {
        const QImage glImage = demTexture_.convertToFormat(QImage::Format_RGBA8888);
        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glImage.width(), glImage.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, glImage.constBits());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    m_textureDirty = false;
}

void Scene3DWidget::rebuildMeshBuffers() {
    releaseMeshBuffers();

    const bool hasMeshFaces = !meshVertices_.isEmpty() && !meshFaces_.isEmpty();
    if (!hasMeshFaces) {
        return;
    }

    computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);
    const float scale = (m_cachedHalfExtent > 0) ? (1.0f / m_cachedHalfExtent) : 1.0f;
    const float cx = m_cachedCenter.x();
    const float cy = m_cachedCenter.y();
    const float cz = m_cachedCenter.z();

    QVector<QVector3D> vertexNormals(meshVertices_.size());
    QVector<std::uint32_t> indices;
    indices.reserve(static_cast<int>(meshFaces_.size()) * 3);

    for (const auto &face : meshFaces_) {
        if (face.a < 0 || face.a >= meshVertices_.size() ||
            face.b < 0 || face.b >= meshVertices_.size() ||
            face.c < 0 || face.c >= meshVertices_.size()) {
            continue;
        }
        const QVector3D n = faceNormal(meshVertices_[face.a],
                                       meshVertices_[face.b],
                                       meshVertices_[face.c]);
        vertexNormals[face.a] += n;
        vertexNormals[face.b] += n;
        vertexNormals[face.c] += n;
        indices.append(static_cast<std::uint32_t>(face.a));
        indices.append(static_cast<std::uint32_t>(face.b));
        indices.append(static_cast<std::uint32_t>(face.c));
    }

    for (auto &n : vertexNormals) {
        const float len = n.length();
        n = len > 1e-10f ? n / len : QVector3D(0, 0, 1);
    }

    m_meshHasTexCoords = meshTexCoords_.size() == meshVertices_.size();
    QVector<float> vertexData;
    vertexData.reserve(static_cast<int>(meshVertices_.size()) * 8);
    for (int i = 0; i < meshVertices_.size(); ++i) {
        const QVector3D &v = meshVertices_[i];
        const QVector3D &n = vertexNormals[i];
        vertexData.append((v.x() - cx) * scale);
        vertexData.append((v.y() - cy) * scale);
        vertexData.append((v.z() - cz) * scale);
        vertexData.append(n.x());
        vertexData.append(n.y());
        vertexData.append(n.z());
        if (m_meshHasTexCoords) {
            vertexData.append(meshTexCoords_[i].x());
            vertexData.append(meshTexCoords_[i].y());
        } else {
            vertexData.append(0.0f);
            vertexData.append(0.0f);
        }
    }

    if (vertexData.isEmpty() || indices.isEmpty()) {
        return;
    }

    glGenBuffers(1, &m_meshVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertexData.size() * sizeof(float)),
                 vertexData.constData(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &m_meshIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_meshIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.constData(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    m_meshIndexCount = static_cast<int>(indices.size());
    m_meshBuffersValid = true;
}

void Scene3DWidget::rebuildDisplayList() {
    // 删除旧的显示列表
    if (m_meshDList) {
        glDeleteLists(m_meshDList, 1);
        m_meshDList = 0;
    }
    releaseMeshBuffers();

    const bool hasMeshFaces = !meshVertices_.isEmpty() && !meshFaces_.isEmpty();
    const bool hasMeshVerticesOnly = !meshVertices_.isEmpty() && meshFaces_.isEmpty();
    const bool hasTexture = !demTexture_.isNull() &&
                            meshTexCoords_.size() == meshVertices_.size();
    uploadTextureIfNeeded();

    if (hasMeshFaces) {
        rebuildMeshBuffers();
        m_dlistValid = true;
        return;
    }

    if (!hasMeshFaces && !hasMeshVerticesOnly) {
    // 没有 mesh 数据也要缓存包围盒，这样 paintGL 不会重复 computeBounds
        computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);
        m_dlistValid = true;
        return;
    }

    // 预计算平滑顶点法线（只在重建时做一次，而不是每帧）
    QVector<QVector3D> vertexNormals;
    QVector<QVector3D> faceNormals;
    const bool useSmoothNormals = hasMeshFaces && meshFaces_.size() <= 2000000;
    if (hasMeshFaces) {
        faceNormals.reserve(meshFaces_.size());
        if (useSmoothNormals) {
            vertexNormals.resize(meshVertices_.size());
        }
        for (const auto &face : meshFaces_) {
            if (face.a < 0 || face.a >= meshVertices_.size() ||
                face.b < 0 || face.b >= meshVertices_.size() ||
                face.c < 0 || face.c >= meshVertices_.size()) {
                faceNormals.append(QVector3D(0, 0, 1));
                continue;
            }
            const QVector3D n = faceNormal(meshVertices_[face.a],
                                           meshVertices_[face.b],
                                           meshVertices_[face.c]);
            faceNormals.append(n);
            if (useSmoothNormals) {
                vertexNormals[face.a] += n;
                vertexNormals[face.b] += n;
                vertexNormals[face.c] += n;
            }
        }
        if (useSmoothNormals) {
            for (auto &n : vertexNormals) {
                const float len = n.length();
                n = len > 1e-10f ? n / len : QVector3D(0, 0, 1);
            }
        }
    }

    // 缓存包围盒
    computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);

    const float scale = (m_cachedHalfExtent > 0) ? (1.0f / m_cachedHalfExtent) : 1.0f;
    const float cx = m_cachedCenter.x();
    const float cy = m_cachedCenter.y();
    const float cz = m_cachedCenter.z();

    // 编译显示列表 —— 把整个 mesh 几何体一次性录制到 GPU 端
    m_meshDList = glGenLists(1);
    glNewList(m_meshDList, GL_COMPILE);

    if (hasMeshFaces) {
    // 第一遍：填充三角形 + 光照
        glEnable(GL_LIGHTING);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        GLfloat specular[] = {0.22f, 0.22f, 0.22f, 1.0f};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 36.0f);
        if (hasTexture && m_textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m_textureId);
            glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < meshFaces_.size(); ++i) {
            const auto &face = meshFaces_[i];
            if (face.a < 0 || face.a >= meshVertices_.size() ||
                face.b < 0 || face.b >= meshVertices_.size() ||
                face.c < 0 || face.c >= meshVertices_.size())
                continue;

            const QVector3D &va = meshVertices_[face.a];
            const QVector3D &vb = meshVertices_[face.b];
            const QVector3D &vc = meshVertices_[face.c];
            const QVector3D n = faceNormals.isEmpty() ? QVector3D(0, 0, 1) : faceNormals[i];
            const QVector3D na = useSmoothNormals ? vertexNormals[face.a] : n;
            const QVector3D nb = useSmoothNormals ? vertexNormals[face.b] : n;
            const QVector3D nc = useSmoothNormals ? vertexNormals[face.c] : n;

            if (hasTexture && m_textureId) {
                glColor3f(0.92f, 0.92f, 0.92f);
            } else {
                glColor3f(0.78f, 0.79f, 0.77f);
            }
            glNormal3f(na.x(), na.y(), na.z());
            if (hasTexture && m_textureId) glTexCoord2f(meshTexCoords_[face.a].x(), meshTexCoords_[face.a].y());
            glVertex3f((va.x() - cx) * scale, (va.y() - cy) * scale, (va.z() - cz) * scale);
            glNormal3f(nb.x(), nb.y(), nb.z());
            if (hasTexture && m_textureId) glTexCoord2f(meshTexCoords_[face.b].x(), meshTexCoords_[face.b].y());
            glVertex3f((vb.x() - cx) * scale, (vb.y() - cy) * scale, (vb.z() - cz) * scale);
            glNormal3f(nc.x(), nc.y(), nc.z());
            if (hasTexture && m_textureId) glTexCoord2f(meshTexCoords_[face.c].x(), meshTexCoords_[face.c].y());
            glVertex3f((vc.x() - cx) * scale, (vc.y() - cy) * scale, (vc.z() - cz) * scale);
        }
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);

        if (meshFaces_.size() <= 80000) {
        // 第二遍：小模型绘制线框轮廓；大模型跳过线框，避免加载和旋转卡顿
            glDisable(GL_LIGHTING);
            glDisable(GL_TEXTURE_2D);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(1.0f);
            glColor3f(hasTexture && m_textureId ? 0.08f : 0.3f,
                      hasTexture && m_textureId ? 0.08f : 0.3f,
                      hasTexture && m_textureId ? 0.1f : 0.35f);
            glBegin(GL_TRIANGLES);
            for (const auto &face : meshFaces_) {
                if (face.a < 0 || face.a >= meshVertices_.size() ||
                    face.b < 0 || face.b >= meshVertices_.size() ||
                    face.c < 0 || face.c >= meshVertices_.size())
                    continue;
                const QVector3D &va = meshVertices_[face.a];
                const QVector3D &vb = meshVertices_[face.b];
                const QVector3D &vc = meshVertices_[face.c];
                glVertex3f((va.x() - cx) * scale, (va.y() - cy) * scale, (va.z() - cz) * scale);
                glVertex3f((vb.x() - cx) * scale, (vb.y() - cy) * scale, (vb.z() - cz) * scale);
                glVertex3f((vc.x() - cx) * scale, (vc.y() - cy) * scale, (vc.z() - cz) * scale);
            }
            glEnd();
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    } else if (hasMeshVerticesOnly) {
        glDisable(GL_LIGHTING);
        glPointSize(3.0f);
        glBegin(GL_POINTS);
        glColor3f(0.85f, 0.35f, 0.55f);
        for (const auto &v : meshVertices_) {
            glVertex3f((v.x() - cx) * scale, (v.y() - cy) * scale, (v.z() - cz) * scale);
        }
        glEnd();
        glPointSize(2.0f);
    }

    glEndList();
    m_dlistValid = true;
}

void Scene3DWidget::paintGL() {
    // 确保显示列表已编译（数据变更或 context 重建后自动重建）
    if (!m_dlistValid) {
        rebuildDisplayList();
    }

    const bool hasPoints = !m_points.isEmpty();
    const bool hasMesh = m_meshBuffersValid && m_meshIndexCount > 0;

    glClearColor(1.0f, 0.94f, 0.96f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 使用缓存的包围盒
    const float halfExtent = m_cachedHalfExtent;
    const QVector3D &center = m_cachedCenter;

    // 投影矩阵
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    const float zFar = halfExtent * 20.0f + 10.0f;
    const float zNear = 0.01f;
    float projMatrix[16] = {};
    const float f = 1.0f / std::tan(45.0f * 3.14159265f / 360.0f);
    projMatrix[0]  = f / aspect;
    projMatrix[5]  = f;
    projMatrix[10] = (zFar + zNear) / (zNear - zFar);
    projMatrix[11] = -1;
    projMatrix[14] = 2 * zFar * zNear / (zNear - zFar);
    glMultMatrixf(projMatrix);

    // 模型视图矩阵
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLfloat keyPos[] = {-0.45f, 0.75f, 1.0f, 0.0f};
    GLfloat keyDiffuse[] = {0.95f, 0.95f, 0.92f, 1.0f};
    GLfloat keyAmbient[] = {0.10f, 0.10f, 0.11f, 1.0f};
    GLfloat fillPos[] = {0.75f, -0.25f, 0.65f, 0.0f};
    GLfloat fillDiffuse[] = {0.36f, 0.40f, 0.48f, 1.0f};
    GLfloat fillAmbient[] = {0.02f, 0.02f, 0.03f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, keyPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, keyAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, keyDiffuse);
    glLightfv(GL_LIGHT1, GL_POSITION, fillPos);
    glLightfv(GL_LIGHT1, GL_AMBIENT, fillAmbient);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, fillDiffuse);

    const float scale = (halfExtent > 0) ? (1.0f / halfExtent) : 1.0f;

    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    // Mesh 模式隐藏大坐标轴，避免穿过模型影响观察；点云/空场景仍保留参考轴。
    if (!hasMesh) {
        glDisable(GL_LIGHTING);
        glBegin(GL_LINES);
        const float axisLen = halfExtent * 1.5f;
        glColor3f(1.0f, 0.2f, 0.2f);
        glVertex3f((center.x() - axisLen) * scale, center.y() * scale, center.z() * scale);
        glVertex3f((center.x() + axisLen) * scale, center.y() * scale, center.z() * scale);
        glColor3f(0.2f, 1.0f, 0.2f);
        glVertex3f(center.x() * scale, (center.y() - axisLen) * scale, center.z() * scale);
        glVertex3f(center.x() * scale, (center.y() + axisLen) * scale, center.z() * scale);
        glColor3f(0.2f, 0.3f, 1.0f);
        glVertex3f(center.x() * scale, center.y() * scale, (center.z() - axisLen) * scale);
        glVertex3f(center.x() * scale, center.y() * scale, (center.z() + axisLen) * scale);
        glEnd();
    }

    // VBO + index buffer 绘制整个 mesh，交互时不再重新提交三角面
    if (hasMesh) {
        glEnable(GL_LIGHTING);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        GLfloat specular[] = {0.22f, 0.22f, 0.22f, 1.0f};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 36.0f);
        glColor3f(0.93f, 0.68f, 0.79f);

        const bool textured = m_textureId != 0 && m_meshHasTexCoords;
        if (textured) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m_textureId);
            glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glColor3f(1.0f, 0.88f, 0.94f);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_meshIbo);

        constexpr GLsizei stride = static_cast<GLsizei>(8 * sizeof(float));
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glVertexPointer(3, GL_FLOAT, stride, reinterpret_cast<const void *>(0));
        glNormalPointer(GL_FLOAT, stride, reinterpret_cast<const void *>(3 * sizeof(float)));
        if (textured) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, stride, reinterpret_cast<const void *>(6 * sizeof(float)));
        }

        glDrawElements(GL_TRIANGLES, m_meshIndexCount, GL_UNSIGNED_INT, reinterpret_cast<const void *>(0));

        if (textured) {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // 点云（点云通常较小，直接绘制即可）
    if (hasPoints) {
        glEnable(GL_LIGHTING);
        glBegin(GL_POINTS);
        glColor3f(0.8f, 0.2f, 0.5f);
        for (const auto &p : m_points) {
            glVertex3f((p.x() - center.x()) * scale,
                       (p.y() - center.y()) * scale,
                       (p.z() - center.z()) * scale);
        }
        glEnd();
    }
}

void Scene3DWidget::mousePressEvent(QMouseEvent *event) { m_lastPos = event->pos(); }

void Scene3DWidget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        m_rotY += (event->pos().x() - m_lastPos.x()) * 0.5f;
        m_rotX += (event->pos().y() - m_lastPos.y()) * 0.5f;
        m_rotX = std::clamp(m_rotX, -90.0f, 90.0f);
        update();
    }
    m_lastPos = event->pos();
}

void Scene3DWidget::fitToBounds() {
    // 用缓存的包围盒（如果有效），否则重新计算
    if (!m_dlistValid) {
        computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);
    }
    m_center = m_cachedCenter;
    m_halfExtent = m_cachedHalfExtent;
    m_zoom = 1.0f;
    m_rotX = 0;
    m_rotY = 0;
    update();
}

void Scene3DWidget::wheelEvent(QWheelEvent *event) {
    const float delta = event->angleDelta().y() / 120.0f;
    m_zoom *= (1.0f - delta * 0.08f);
    m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
    update();
}

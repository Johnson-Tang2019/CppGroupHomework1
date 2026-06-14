// ===== 主窗口界面与交互逻辑 (Group3 - UI & Main Interaction) =====
#include "rs/MainWindow.h"
#include "rs/Algorithms.h"
#include "rs/RasterIO.h"
#include "rs/RasterRenderDialog.h"
#include "rs/Scene3DWidget.h"
#include "rs/ExtendedAlgorithms.h"
#include "rs/RemoteSensingIndices.h"

#include <QApplication>
#include <QBuffer>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QUrl>
#include <QTreeWidgetItemIterator>
#include <QtConcurrent/QtConcurrent>
#include <functional>

namespace rs {
namespace {

// 自定义角色，用于在 QTreeWidgetItem 中存储额外数据
constexpr int kLayerIndexRole = Qt::UserRole + 1; // 存储图层在 LayerManager 中的索引
constexpr int kBandIndexRole = Qt::UserRole + 2;  // 存储波段索引（用于波段子节点）
constexpr int kNodeKindRole = Qt::UserRole + 3;   // 存储节点类型（文件夹/图层/波段）

// 图层树节点的类型枚举
enum class NodeKind {
    Folder, // 文件夹节点（如"源数据/遥感影像"），不可选中
    Layer,  // 图层节点，可选中/勾选
    Band    // 波段子节点，仅信息展示
};

class DomesticVisionChatPanel final : public QWidget {
  public:
    explicit DomesticVisionChatPanel(std::function<QString()> contextProvider,
                                     std::function<QImage()> imageProvider,
                                     QWidget *parent = nullptr)
        : QWidget(parent),
          contextProvider_(std::move(contextProvider)),
          imageProvider_(std::move(imageProvider)) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);

        auto *keyRow = new QHBoxLayout;
        keyRow->addWidget(new QLabel(QStringLiteral("国产视觉 Key:"), this));
        apiKeyEdit_ = new QLineEdit(this);
        apiKeyEdit_->setEchoMode(QLineEdit::Password);
        apiKeyEdit_->setPlaceholderText(QStringLiteral("ARK_API_KEY / MOONSHOT_API_KEY"));
        QByteArray domesticKey = qgetenv("ARK_API_KEY");
        if (domesticKey.isEmpty()) {
            domesticKey = qgetenv("MOONSHOT_API_KEY");
        }
        apiKeyEdit_->setText(QString::fromUtf8(domesticKey));
        keyRow->addWidget(apiKeyEdit_, 1);
        layout->addLayout(keyRow);

        auto *visionKeyRow = new QHBoxLayout;
        visionKeyRow->addWidget(new QLabel(QStringLiteral("接口 URL:"), this));
        openAiKeyEdit_ = new QLineEdit(this);
        openAiKeyEdit_->setPlaceholderText(QStringLiteral("OpenAI-compatible chat/completions URL"));
        openAiKeyEdit_->setText(QStringLiteral("https://ark.cn-beijing.volces.com/api/v3/chat/completions"));
        visionKeyRow->addWidget(openAiKeyEdit_, 1);
        layout->addLayout(visionKeyRow);

        auto *modelRow = new QHBoxLayout;
        modelRow->addWidget(new QLabel(QStringLiteral("模型/Endpoint:"), this));
        modelEdit_ = new QLineEdit(QStringLiteral("ep-xxxxxxxxxxxxxxxx"), this);
        modelRow->addWidget(modelEdit_, 1);
        contextButton_ = new QPushButton(QStringLiteral("Insert File Info"), this);
        modelRow->addWidget(contextButton_);
        layout->addLayout(modelRow);

        chatEdit_ = new QTextEdit(this);
        chatEdit_->setReadOnly(true);
        chatEdit_->setMinimumHeight(150);
        chatEdit_->setPlaceholderText(QStringLiteral("Ask the domestic vision model to identify land-cover objects from the selected image."));
        layout->addWidget(chatEdit_, 8);

        inputEdit_ = new QTextEdit(this);
        inputEdit_->setMaximumHeight(110);
        inputEdit_->setPlaceholderText(QStringLiteral("Type your message here. Imported layer information is attached automatically."));
        layout->addWidget(inputEdit_, 1);

        auto *buttonRow = new QHBoxLayout;
        buttonRow->addStretch(1);
        clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
        sendButton_ = new QPushButton(QStringLiteral("Send"), this);
        buttonRow->addWidget(clearButton_);
        buttonRow->addWidget(sendButton_);
        layout->addLayout(buttonRow);

        resetHistory();

        connect(sendButton_, &QPushButton::clicked, this, [this]() { sendPrompt(); });
        connect(clearButton_, &QPushButton::clicked, this, [this]() {
            chatEdit_->clear();
            resetHistory();
        });
        connect(contextButton_, &QPushButton::clicked, this, [this]() {
            appendMessage(QStringLiteral("Imported file info"), currentLayerContext());
        });
    }

  private:
    void resetHistory() {
        history_ = QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"),
                         QStringLiteral("You are a helpful assistant for a Qt/C++ remote sensing application. Answer clearly and practically.")}}};
    }

    QString currentLayerContext() const {
        if (!contextProvider_) {
            return QStringLiteral("No layer context provider is available.");
        }
        const QString context = contextProvider_().trimmed();
        return context.isEmpty() ? QStringLiteral("No imported layer is available.") : context;
    }

    QImage currentVisionImage() const {
        if (!imageProvider_) {
            return {};
        }
        QImage image = imageProvider_();
        if (image.isNull()) {
            return {};
        }
        const int maxSide = 768;
        if (image.width() > maxSide || image.height() > maxSide) {
            image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return image.convertToFormat(QImage::Format_RGB888);
    }

    QString imageToJpegDataUrl(const QImage &image) const {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "JPEG", 75);
        return QStringLiteral("data:image/jpeg;base64,%1")
            .arg(QString::fromLatin1(bytes.toBase64()));
    }

    void appendMessage(const QString &speaker, const QString &text) {
        chatEdit_->append(QStringLiteral("<b>%1:</b>").arg(speaker.toHtmlEscaped()));
        chatEdit_->append(text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
        chatEdit_->append(QString());
    }

    void sendPrompt() {
        sendVisionPrompt();
    }

    void sendVisionPrompt() {
        const QString apiKey = apiKeyEdit_->text().trimmed();
        const QString apiUrl = openAiKeyEdit_->text().trimmed().isEmpty()
                                   ? QStringLiteral("https://ark.cn-beijing.volces.com/api/v3/chat/completions")
                                   : openAiKeyEdit_->text().trimmed();
        const QString prompt = inputEdit_->toPlainText().trimmed();
        const QString model = modelEdit_->text().trimmed();
        const QImage image = currentVisionImage();

        if (apiKey.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("国产视觉 API Key"),
                                 QStringLiteral("Please enter your model API key, or set ARK_API_KEY / MOONSHOT_API_KEY."));
            return;
        }
        if (model.isEmpty() || model == QStringLiteral("ep-xxxxxxxxxxxxxxxx")) {
            QMessageBox::warning(this, QStringLiteral("模型/Endpoint"),
                                 QStringLiteral("Please enter your model name or endpoint ID, for example a Volcengine Ark endpoint like ep-xxxx."));
            return;
        }
        if (image.isNull()) {
            QMessageBox::warning(this, QStringLiteral("Vision input"),
                                 QStringLiteral("Please select a raster layer with a display image first."));
            return;
        }
        if (prompt.isEmpty()) {
            return;
        }

        inputEdit_->clear();
        appendMessage(QStringLiteral("You"), prompt + QStringLiteral("\n[Current selected image attached]"));
        sendButton_->setEnabled(false);
        sendButton_->setText(QStringLiteral("Sending to vision model..."));

        const QString layerContext = currentLayerContext();
        const QString visionPrompt =
            QStringLiteral("你是遥感影像视觉解译助手。请直接观察附图，并结合下列导入图层元数据回答用户问题。"
                           "重点识别可能的地物类别，例如建筑、道路、水体、植被、裸地、阴影、停车场等。"
                           "如果无法确认具体建筑名称，请说明原因，但仍要给出基于图像可见特征的地物判断。\n\n"
                           "导入图层元数据：\n%1\n\n用户问题：%2")
                .arg(layerContext, prompt);

        QJsonArray content;
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                   {QStringLiteral("text"), visionPrompt}});
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"),
             QJsonObject{{QStringLiteral("url"), imageToJpegDataUrl(image)}}}});

        QJsonObject body;
        body.insert(QStringLiteral("model"), model);
        body.insert(QStringLiteral("messages"),
                    QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                           {QStringLiteral("content"), content}}});
        body.insert(QStringLiteral("temperature"), 0.2);
        body.insert(QStringLiteral("stream"), false);

        QNetworkRequest request{QUrl(apiUrl)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

        auto *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, prompt]() {
            handleVisionReply(reply, prompt);
            reply->deleteLater();
        });
    }

    void handleVisionReply(QNetworkReply *reply, const QString &prompt) {
        sendButton_->setEnabled(true);
        sendButton_->setText(QStringLiteral("Send"));

        const QByteArray responseBody = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            appendMessage(QStringLiteral("Vision Error"),
                          reply->errorString() + QStringLiteral("\n") +
                              QString::fromUtf8(responseBody));
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            appendMessage(QStringLiteral("Vision Error"),
                          QStringLiteral("Invalid JSON response: %1").arg(parseError.errorString()));
            return;
        }

        const QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("error"))) {
            const QJsonObject error = root.value(QStringLiteral("error")).toObject();
            appendMessage(QStringLiteral("Vision Error"),
                          error.value(QStringLiteral("message"))
                              .toString(QString::fromUtf8(responseBody)));
            return;
        }

        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            appendMessage(QStringLiteral("Vision Error"), QStringLiteral("The vision model returned no choices."));
            return;
        }

        const QJsonObject message =
            choices.first().toObject().value(QStringLiteral("message")).toObject();
        const QString answer = message.value(QStringLiteral("content")).toString().trimmed();
        if (answer.isEmpty()) {
            appendMessage(QStringLiteral("Vision Error"), QStringLiteral("The vision model returned an empty answer."));
            return;
        }

        history_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), prompt}});
        history_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                    {QStringLiteral("content"), answer}});
        appendMessage(QStringLiteral("国产视觉模型"), answer);
    }

    QLineEdit *apiKeyEdit_{};
    QLineEdit *openAiKeyEdit_{};
    QLineEdit *modelEdit_{};
    QTextEdit *chatEdit_{};
    QTextEdit *inputEdit_{};
    QPushButton *sendButton_{};
    QPushButton *clearButton_{};
    QPushButton *contextButton_{};
    QNetworkAccessManager network_;
    QJsonArray history_;
    std::function<QString()> contextProvider_;
    std::function<QImage()> imageProvider_;
};

quint8 readUInt8At(QFile &file, qint64 offset) {
    if (!file.seek(offset))
        throw std::runtime_error("LAS 文件头不完整");
    char value = 0;
    if (file.read(&value, 1) != 1)
        throw std::runtime_error("LAS 文件头不完整");
    return static_cast<quint8>(value);
}

quint16 readUInt16LeAt(QFile &file, qint64 offset) {
    if (!file.seek(offset))
        throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint16 value = 0;
    stream >> value;
    if (stream.status() != QDataStream::Ok)
        throw std::runtime_error("LAS 文件头不完整");
    return value;
}

quint32 readUInt32LeAt(QFile &file, qint64 offset) {
    if (!file.seek(offset))
        throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 value = 0;
    stream >> value;
    if (stream.status() != QDataStream::Ok)
        throw std::runtime_error("LAS 文件头不完整");
    return value;
}

quint64 readUInt64LeAt(QFile &file, qint64 offset) {
    if (!file.seek(offset))
        throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint64 value = 0;
    stream >> value;
    if (stream.status() != QDataStream::Ok)
        throw std::runtime_error("LAS 文件头不完整");
    return value;
}

double readDoubleLeAt(QFile &file, qint64 offset) {
    if (!file.seek(offset))
        throw std::runtime_error("LAS 文件头不完整");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    double value = 0.0;
    stream >> value;
    if (stream.status() != QDataStream::Ok)
        throw std::runtime_error("LAS 文件头不完整");
    return value;
}

qint32 readInt32Le(const char *data) {
    const auto *b = reinterpret_cast<const unsigned char *>(data);
    const quint32 value = (static_cast<quint32>(b[0])) | (static_cast<quint32>(b[1]) << 8) |
                          (static_cast<quint32>(b[2]) << 16) | (static_cast<quint32>(b[3]) << 24);
    return static_cast<qint32>(value);
}

QVector<QVector3D> readLasPoints(const QString &path) {
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

    const qint64 availableRecords =
        (file.size() - static_cast<qint64>(pointDataOffset)) / recordLength;
    pointCount =
        std::min<quint64>(pointCount, static_cast<quint64>(std::max<qint64>(0, availableRecords)));
    pointCount =
        std::min<quint64>(pointCount, static_cast<quint64>(std::numeric_limits<int>::max()));
    const int reserveCount = static_cast<int>(pointCount);

    QVector<QVector3D> points;
    points.reserve(reserveCount);
    QByteArray record(recordLength, Qt::Uninitialized);
    if (!file.seek(pointDataOffset)) {
        throw std::runtime_error("无法定位 LAS 点数据");
    }
    for (quint64 i = 0; i < pointCount; ++i) {
        if (file.read(record.data(), recordLength) != recordLength)
            break;
        const double x = readInt32Le(record.constData()) * xScale + xOffset;
        const double y = readInt32Le(record.constData() + 4) * yScale + yOffset;
        const double z = readInt32Le(record.constData() + 8) * zScale + zOffset;
        points.append(
            QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
    }
    return points;
}

// 生成节点的唯一键（从根到当前节点的路径字符串）
QString itemKey(const QTreeWidgetItem *item) {
    QStringList parts;                   // 用于存储从根到当前节点的各层名称
    const auto *current = item;          // 从当前节点开始向上遍历
    while (current) {                    // 一直遍历到根节点（parent 为 nullptr）
        parts.prepend(current->text(0)); // 把当前节点的文本插入到列表最前面
        current = current->parent();     // 向上移动到父节点
    }
    return parts.join(QLatin1Char('/')); // 用 "/" 拼接成路径字符串，如 "源数据/遥感影像/xxx.tif"
}

// 递归收集所有展开节点的键
void collectExpandedKeys(QTreeWidgetItem *item, QSet<QString> &keys) {
    if (!item) { // 空节点，直接返回
        return;
    }
    if (item->isExpanded()) {       // 如果当前节点是展开状态
        keys.insert(itemKey(item)); // 则记录它的路径键
    }
    for (int i = 0; i < item->childCount(); ++i) { // 遍历所有子节点
        collectExpandedKeys(item->child(i), keys); // 递归处理每个子节点
    }
}

// 在指定父节点下查找或创建子文件夹
QTreeWidgetItem *ensureChildFolder(QTreeWidgetItem *parent, const QString &name) {
    // 先在现有子节点中查找是否已有同名文件夹
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name) { // 找到了同名的
            return parent->child(i);             // 直接返回已有的节点
        }
    }
    // 没找到，则创建新的文件夹节点
    auto *folder = new QTreeWidgetItem(parent); // 创建新节点，parent 为父节点
    folder->setText(0, name);                   // 设置显示文本为文件夹名
    folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder)); // 标记为 Folder 类型
    folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) |
                     Qt::ItemIsEnabled); // 移除"可选中"标志，保留"启用"标志
    return folder;
}

// 在顶层节点中查找或创建文件夹
QTreeWidgetItem *ensureTopFolder(QTreeWidget *tree, const QString &name) {
    // 先在所有顶层节点中查找是否已有同名文件夹
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == name) { // 找到了同名的
            return tree->topLevelItem(i);             // 直接返回已有的节点
        }
    }
    // 没找到，则创建新的顶层文件夹节点
    auto *folder = new QTreeWidgetItem(tree);                              // 创建新节点，tree 为根
    folder->setText(0, name);                                              // 设置显示文本为文件夹名
    folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder)); // 标记为 Folder 类型
    folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) |
                     Qt::ItemIsEnabled); // 不可选中，仅启用
    return folder;
}

qint32 readInt32LeBytes(const char *data) {
    const auto *b = reinterpret_cast<const unsigned char *>(data);
    const quint32 value = (static_cast<quint32>(b[0])) | (static_cast<quint32>(b[1]) << 8) |
                          (static_cast<quint32>(b[2]) << 16) | (static_cast<quint32>(b[3]) << 24);
    return static_cast<qint32>(value);
}

struct LoadedMeshData {
    QVector<QVector3D> vertices;
    QVector<Face> faces;
};

LoadedMeshData loadMeshFromPly(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("无法打开 PLY 文件");
    }
    const QByteArray allData = file.readAll();
    file.close();

    const int headerEndPos = allData.indexOf("end_header");
    if (headerEndPos < 0) {
        throw std::runtime_error("PLY 缺少 end_header");
    }
    const int nlPos = allData.indexOf('\n', headerEndPos);
    const int headerBytes = (nlPos >= 0) ? nlPos + 1 : allData.size();

    bool isAscii = false;
    int vertexCount = 0;
    int faceCount = 0;
    int vertexPropCount = 0;
    bool inVertex = false;

    QTextStream headerStream(allData.left(headerBytes));
    while (!headerStream.atEnd()) {
        const QString line = headerStream.readLine().trimmed();
        if (line.startsWith(QLatin1String("element vertex"))) {
            vertexCount = line.section(QLatin1Char(' '), 2, 2).toInt();
            inVertex = true;
            continue;
        }
        if (line.startsWith(QLatin1String("element face"))) {
            faceCount = line.section(QLatin1Char(' '), 2, 2).toInt();
            inVertex = false;
            continue;
        }
        if (inVertex && line.startsWith(QLatin1String("property "))) {
            ++vertexPropCount;
        }
        if (line.contains(QLatin1String("format ascii"))) {
            isAscii = true;
        }
    }

    if (vertexCount <= 0) {
        throw std::runtime_error("PLY 顶点数量无效");
    }

    LoadedMeshData mesh;
    if (isAscii) {
        QTextStream in(allData);
        while (!in.atEnd()) {
            if (in.readLine().trimmed() == QLatin1String("end_header")) {
                break;
            }
        }

        mesh.vertices.reserve(vertexCount);
        for (int i = 0; i < vertexCount && !in.atEnd(); ++i) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) {
                --i;
                continue;
            }
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 3) {
                continue;
            }
            mesh.vertices.append(
                QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
        }

        mesh.faces.reserve(faceCount);
        for (int i = 0; i < faceCount && !in.atEnd(); ++i) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) {
                --i;
                continue;
            }
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 4) {
                continue;
            }
            const int n = parts[0].toInt();
            if (n < 3 || parts.size() < n + 1) {
                continue;
            }
            const int i0 = parts[1].toInt();
            for (int t = 1; t < n - 1; ++t) {
                Face face;
                face.a = i0;
                face.b = parts[1 + t].toInt();
                face.c = parts[2 + t].toInt();
                mesh.faces.append(face);
            }
        }
    } else {
        if (vertexPropCount < 3) {
            throw std::runtime_error("PLY 顶点属性数不足");
        }
        const int vertexSize = vertexPropCount * static_cast<int>(sizeof(float));
        int offset = headerBytes;

        mesh.vertices.reserve(vertexCount);
        for (int i = 0; i < vertexCount; ++i) {
            if (offset + vertexSize > allData.size()) {
                break;
            }
            const float *f = reinterpret_cast<const float *>(allData.constData() + offset);
            mesh.vertices.append(QVector3D(f[0], f[1], f[2]));
            offset += vertexSize;
        }

        mesh.faces.reserve(faceCount);
        for (int i = 0; i < faceCount; ++i) {
            if (offset >= allData.size()) {
                break;
            }
            const quint8 n = static_cast<quint8>(allData[offset++]);
            if (n < 3 || offset + n * 4 > allData.size()) {
                break;
            }
            const int i0 = readInt32LeBytes(allData.constData() + offset);
            for (int t = 1; t < n - 1; ++t) {
                Face face;
                face.a = i0;
                face.b = readInt32LeBytes(allData.constData() + offset + t * 4);
                face.c = readInt32LeBytes(allData.constData() + offset + (t + 1) * 4);
                mesh.faces.append(face);
            }
            offset += n * 4;
        }
    }

    return mesh;
}

} // namespace

// 构造函数：初始化窗口标题、菜单、UI 布局，记录启动日志
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Remote Sensing Qt Starter")); // 设置窗口标题
    createMenus();                                               // 构建菜单栏
    createUi();                                                  // 构建界面控件
    appendLog(QStringLiteral("Starter 已启动：当前版本提供 GDAL "
                             "多波段、参数化算法、DEM/正射流程的工程骨架。")); // 记录启动日志
    updateActionStates(); // 初始化菜单项的启用状态（刚启动时所有菜单应该禁用）
}

// 构建菜单栏：数据、影像处理、摄影测量/三维
void MainWindow::createMenus() {
    // ---- "数据" 菜单 ----
    auto *dataMenu = menuBar()->addMenu(QStringLiteral("数据")); // 创建"数据"菜单
    connect(dataMenu->addAction(QStringLiteral("加载遥感影像(GDAL，可多选)")), &QAction::triggered,
            this, &MainWindow::openRasterDatasets); // 添加"加载遥感影像"并连接点击信号
    connect(dataMenu->addAction(QStringLiteral("加载点云")), &QAction::triggered, this,
            &MainWindow::openPointCloud); // 添加"加载点云"并连接
    connect(dataMenu->addAction(QStringLiteral("加载 Mesh")), &QAction::triggered, this,
            &MainWindow::openMesh); // 添加"加载 Mesh"并连接
    connect(dataMenu->addAction(QStringLiteral("加载 DEM")), &QAction::triggered, this,
            &MainWindow::openDem); // 添加"加载 DEM"并连接
    dataMenu->addSeparator();      // 添加分隔线，将加载与删除操作分开
    deleteLayerAction_ = dataMenu->addAction(
        QStringLiteral("删除选中图层")); // 添加"删除选中图层"并保存指针以便控制启用/禁用
    connect(deleteLayerAction_, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
    clearProjectAction_ =
        dataMenu->addAction(QStringLiteral("初始化/清空工程")); // 添加"清空工程"并保存指针
    connect(clearProjectAction_, &QAction::triggered, this, &MainWindow::clearProject);

    // ---- "影像处理" 菜单 ----
    auto *rasterMenu = menuBar()->addMenu(QStringLiteral("影像处理"));  // 创建"影像处理"菜单
    auto *bandMenu = rasterMenu->addMenu(QStringLiteral("波段与设色")); // 创建子菜单"波段与设色"
    renderAction_ =
        bandMenu->addAction(QStringLiteral("波段组合/设色...")); // 添加"波段组合/设色"并保存指针
    connect(renderAction_, &QAction::triggered, this, &MainWindow::configureRasterRendering);

    auto *statMenu = rasterMenu->addMenu(QStringLiteral("统计")); // 创建子菜单"统计"
    histogramAction_ =
        statMenu->addAction(QStringLiteral("灰度直方图...")); // 添加"灰度直方图"并保存指针
    connect(histogramAction_, &QAction::triggered, this, &MainWindow::runHistogram);

    auto *enhanceMenu = rasterMenu->addMenu(QStringLiteral("增强")); // 创建子菜单"增强"
    equalizeAction_ =
        enhanceMenu->addAction(QStringLiteral("直方图均衡化...")); // 添加"直方图均衡化"并保存指针
    connect(equalizeAction_, &QAction::triggered, this, &MainWindow::runHistogramEqualization);
    connect(enhanceMenu->addAction(QStringLiteral("线性/百分比拉伸...")), &QAction::triggered, this,
            [this]() {
                QStringList modes = {QStringLiteral("percent"), QStringLiteral("linear")};
                bool ok = false;
                const QString mode = QInputDialog::getItem(
                    this, QStringLiteral("拉伸模式"), QStringLiteral("选择拉伸方式："),
                    {QStringLiteral("百分比拉伸"), QStringLiteral("线性拉伸")}, 0, false, &ok);
                if (!ok)
                    return;
                StretchEnhancementAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("mode")] =
                    mode.contains(QStringLiteral("线性")) ? QStringLiteral("linear")
                                                          : QStringLiteral("percent");
                executeRasterAlgorithm(algo, ctx);
            });
    connect(enhanceMenu->addAction(QStringLiteral("CLAHE 增强...")), &QAction::triggered, this,
            [this]() {
                bool ok = false;
                const double clip =
                    QInputDialog::getDouble(this, QStringLiteral("CLAHE"), QStringLiteral("clipLimit:"),
                                            2.0, 0.1, 40.0, 1, &ok);
                if (!ok)
                    return;
                const int tile = QInputDialog::getInt(this, QStringLiteral("CLAHE"),
                                                      QStringLiteral("tileSize:"), 8, 2, 64, 1, &ok);
                if (!ok)
                    return;
                ClaheEnhancementAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("clipLimit")] = clip;
                ctx.parameters[QStringLiteral("tileSize")] = tile;
                executeRasterAlgorithm(algo, ctx);
            });
    connect(enhanceMenu->addAction(QStringLiteral("高斯滤波...")), &QAction::triggered, this,
            [this]() {
                DenoiseFilterAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("filterType")] = QStringLiteral("gaussian");
                executeRasterAlgorithm(algo, ctx);
            });
    connect(enhanceMenu->addAction(QStringLiteral("中值滤波...")), &QAction::triggered, this,
            [this]() {
                DenoiseFilterAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("filterType")] = QStringLiteral("median");
                executeRasterAlgorithm(algo, ctx);
            });
    connect(enhanceMenu->addAction(QStringLiteral("双边滤波...")), &QAction::triggered, this,
            [this]() {
                DenoiseFilterAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("filterType")] = QStringLiteral("bilateral");
                executeRasterAlgorithm(algo, ctx);
            });
    connect(enhanceMenu->addAction(QStringLiteral("Unsharp 锐化...")), &QAction::triggered, this,
            [this]() {
                SharpenEnhancementAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("method")] = QStringLiteral("unsharp");
                executeRasterAlgorithm(algo, ctx);
            });
    connect(enhanceMenu->addAction(QStringLiteral("拉普拉斯锐化...")), &QAction::triggered, this,
            [this]() {
                SharpenEnhancementAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("method")] = QStringLiteral("laplacian");
                executeRasterAlgorithm(algo, ctx);
            });

    auto *featureMenu = rasterMenu->addMenu(QStringLiteral("特征与检测")); // 特征/边缘/检测
    featureAction_ = featureMenu->addAction(
        QStringLiteral("ORB/SIFT/AKAZE 特征提取...")); // 添加特征提取并保存指针
    connect(featureAction_, &QAction::triggered, this, &MainWindow::runFeatureExtraction);
    connect(featureMenu->addAction(QStringLiteral("Canny 边缘检测...")), &QAction::triggered, this,
            [this]() {
                bool ok = false;
                const int t1 = QInputDialog::getInt(this, QStringLiteral("Canny"), QStringLiteral("低阈值:"),
                                                    50, 1, 500, 1, &ok);
                if (!ok)
                    return;
                const int t2 = QInputDialog::getInt(this, QStringLiteral("Canny"), QStringLiteral("高阈值:"),
                                                      150, 1, 500, 1, &ok);
                if (!ok)
                    return;
                CannyEdgeAlgorithm algo;
                ProcessingContext ctx;
                ctx.bandIndex = selectedBandIndex();
                ctx.parameters[QStringLiteral("threshold1")] = t1;
                ctx.parameters[QStringLiteral("threshold2")] = t2;
                executeRasterAlgorithm(algo, ctx);
            });

    auto *classMenu = rasterMenu->addMenu(QStringLiteral("分类与检测"));
    connect(classMenu->addAction(QStringLiteral("K-Means 无监督分类...")), &QAction::triggered, this,
            [this]() {
                bool ok = false;
                const int k = QInputDialog::getInt(this, QStringLiteral("K-Means"),
                                                   QStringLiteral("类别数 K："), 3, 2, 10, 1, &ok);
                if (!ok)
                    return;
                KMeansClassificationAlgorithm algo;
                ProcessingContext ctx;
                ctx.parameters[QStringLiteral("k")] = k;
                executeRasterAlgorithm(algo, ctx);
            });
    connect(classMenu->addAction(QStringLiteral("SVM 地物分类...")), &QAction::triggered, this,
            [this]() {
                bool ok = false;
                const int classes = QInputDialog::getInt(this, QStringLiteral("SVM"),
                                                       QStringLiteral("类别数："), 3, 2, 10, 1, &ok);
                if (!ok)
                    return;
                const int samples = QInputDialog::getInt(this, QStringLiteral("SVM"),
                                                         QStringLiteral("训练样本数："), 500, 50, 5000,
                                                         50, &ok);
                if (!ok)
                    return;
                SvmClassificationAlgorithm algo;
                ProcessingContext ctx;
                ctx.parameters[QStringLiteral("classes")] = classes;
                ctx.parameters[QStringLiteral("trainSamples")] = samples;
                executeRasterAlgorithm(algo, ctx);
            });
    connect(classMenu->addAction(QStringLiteral("轮廓目标检测...")), &QAction::triggered, this, [this]() {
        ContourDetectionAlgorithm algo;
        executeRasterAlgorithm(algo);
    });
    connect(classMenu->addAction(QStringLiteral("连通域目标检测...")), &QAction::triggered, this,
            [this]() {
                bool ok = false;
                const int minArea = QInputDialog::getInt(this, QStringLiteral("连通域"),
                                                         QStringLiteral("最小面积："), 100, 1, 100000,
                                                         10, &ok);
                if (!ok)
                    return;
                ConnectedComponentsAlgorithm algo;
                ProcessingContext ctx;
                ctx.parameters[QStringLiteral("minArea")] = minArea;
                executeRasterAlgorithm(algo, ctx);
            });
    connect(classMenu->addAction(QStringLiteral("混淆矩阵精度评价...")), &QAction::triggered, this,
            [this]() {
                const auto indices = selectedLayerIndices();
                std::shared_ptr<RasterLayer> pred, ref;
                for (int idx : indices) {
                    try {
                        auto r = std::dynamic_pointer_cast<RasterLayer>(layers_.at(idx));
                        if (!r)
                            continue;
                        if (!pred)
                            pred = r;
                        else if (!ref)
                            ref = r;
                    } catch (...) {
                    }
                }
                if (!pred || !ref) {
                    appendLog(QStringLiteral("请选中两个栅格图层（预测结果 + 参考分类）。"));
                    return;
                }
                const QString csvPath = QFileDialog::getSaveFileName(
                    this, QStringLiteral("导出混淆矩阵 CSV"), QString(), QStringLiteral("CSV (*.csv)"));
                ConfusionMatrixAlgorithm algo;
                ProcessingContext ctx;
                ctx.auxiliaryRaster = ref.get();
                if (!csvPath.isEmpty())
                    ctx.parameters[QStringLiteral("csvPath")] = csvPath;
                const auto result = algo.execute(*pred, ctx);
                applyProcessingResult(result, pred, QStringLiteral("混淆矩阵精度评价"),
                                      QStringLiteral("_精度"));
            });

    auto *indexMenu = menuBar()->addMenu(QStringLiteral("遥感指数"));
    connect(indexMenu->addAction(QStringLiteral("计算 NDVI/NDWI/NDBI...")), &QAction::triggered, this,
            [this]() {
                QStringList indices = {QStringLiteral("NDVI"), QStringLiteral("NDWI"),
                                       QStringLiteral("NDBI")};
                bool ok = false;
                const QString index = QInputDialog::getItem(
                    this, QStringLiteral("遥感指数"), QStringLiteral("选择指数："), indices, 0, false,
                    &ok);
                if (!ok)
                    return;
                const bool threshold = QMessageBox::question(
                                           this, QStringLiteral("阈值分割"),
                                           QStringLiteral("是否应用阈值分割掩膜？")) == QMessageBox::Yes;
                RemoteSensingIndexAlgorithm algo;
                ProcessingContext ctx;
                ctx.parameters[QStringLiteral("index")] = index;
                ctx.parameters[QStringLiteral("applyThreshold")] = threshold;
                ctx.parameters[QStringLiteral("threshold")] = 0.2;
                const auto raster = selectedRaster();
                if (!raster) {
                    appendLog(QStringLiteral("请先选择遥感影像图层。"));
                    return;
                }
                const auto result = algo.execute(*raster, ctx);
                applyProcessingResult(result, raster, index, QStringLiteral("_") + index);
            });
    connect(indexMenu->addAction(QStringLiteral("多时相指数对比...")), &QAction::triggered, this,
            [this]() {
                const auto indices = selectedLayerIndices();
                std::shared_ptr<RasterLayer> t1, t2;
                for (int idx : indices) {
                    try {
                        auto r = std::dynamic_pointer_cast<RasterLayer>(layers_.at(idx));
                        if (!r)
                            continue;
                        if (!t1)
                            t1 = r;
                        else if (!t2)
                            t2 = r;
                    } catch (...) {
                    }
                }
                if (!t1 || !t2) {
                    appendLog(QStringLiteral("请选中两个时相的栅格图层。"));
                    return;
                }
                IndexTemporalCompareAlgorithm algo;
                ProcessingContext ctx;
                ctx.auxiliaryRaster = t2.get();
                ctx.parameters[QStringLiteral("index")] = QStringLiteral("NDVI");
                const auto result = algo.execute(*t1, ctx);
                applyProcessingResult(result, t1, QStringLiteral("多时相指数对比"),
                                      QStringLiteral("_时相对比"));
            });
    connect(indexMenu->addAction(QStringLiteral("导出指数统计 CSV...")), &QAction::triggered, this,
            [this]() {
                const auto raster = selectedRaster();
                if (!raster) {
                    appendLog(QStringLiteral("请先选择图层。"));
                    return;
                }
                const QString path = QFileDialog::getSaveFileName(
                    this, QStringLiteral("导出 CSV"), QString(), QStringLiteral("CSV (*.csv)"));
                if (path.isEmpty())
                    return;
                RemoteSensingIndexAlgorithm algo;
                ProcessingContext ctx;
                ctx.parameters[QStringLiteral("index")] = QStringLiteral("NDVI");
                ctx.parameters[QStringLiteral("csvPath")] = path;
                executeRasterAlgorithm(algo, ctx);
            });

    // ---- "摄影测量/三维" 菜单 ----
    auto *photogrammetryMenu =
        menuBar()->addMenu(QStringLiteral("摄影测量/三维")); // 创建"摄影测量/三维"菜单
    demAction_ =
        photogrammetryMenu->addAction(QStringLiteral("DEM 重建...")); // 添加"DEM 重建"并保存指针
    connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
    orthoAction_ = photogrammetryMenu->addAction(
        QStringLiteral("正射影像校正...")); // 添加"正射影像校正"并保存指针
    connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);

    // ---- "点云处理" 菜单 ----
    auto *pcMenu = menuBar()->addMenu(QStringLiteral("点云处理"));  // 创建"点云处理"菜单
    downsampleAction_ =
        pcMenu->addAction(QStringLiteral("体素降采样...")); // 添加"体素降采样"并保存指针
    connect(downsampleAction_, &QAction::triggered, this, &MainWindow::runPointCloudDownsample);
    filterAction_ =
        pcMenu->addAction(QStringLiteral("统计滤波...")); // 添加"统计滤波"并保存指针
    connect(filterAction_, &QAction::triggered, this, &MainWindow::runPointCloudFilter);
    pcToDemAction_ =
        pcMenu->addAction(QStringLiteral("点云转 DEM...")); // 添加"点云转 DEM"并保存指针
    connect(pcToDemAction_, &QAction::triggered, this, &MainWindow::runPointCloudToDem);
    exportPlyAction_ =
        pcMenu->addAction(QStringLiteral("导出 PLY...")); // 添加"导出 PLY"并保存指针
    connect(exportPlyAction_, &QAction::triggered, this, &MainWindow::exportPly);

}

// 构建界面布局：左侧图层树 + 右侧影像/三维标签页 + 底部日志面板
void MainWindow::createUi() {
    // 主分割器：水平方向，将窗口分为左侧（图层树）和右侧（影像+日志）
    auto *root = new QSplitter(Qt::Horizontal, this);
    // ---- 左侧：图层树 ----
    layerTree_ = new QTreeWidget(root);                                 // 创建图层树控件
    layerTree_->setHeaderLabel(QStringLiteral("工程图层"));             // 设置表头文字
    layerTree_->setAlternatingRowColors(true);                          // 开启交替行颜色
    layerTree_->setSelectionMode(QAbstractItemView::ExtendedSelection); // 支持 Ctrl/Shift 多选
    layerTree_->setContextMenuPolicy(Qt::CustomContextMenu);            // 启用自定义右键菜单
    connect(layerTree_, &QTreeWidget::itemSelectionChanged, this,
            &MainWindow::onSelectionChanged); // 选中项改变时刷新影像
    connect(layerTree_, &QTreeWidget::itemChanged, this,
            &MainWindow::onLayerItemChanged); // 勾选框改变时切换可见性
    connect(layerTree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showLayerContextMenu); // 右键弹出菜单

    // ---- 右侧：上下分割（上方影像标签页 + 下方日志） ----
    auto *right = new QSplitter(Qt::Vertical, root); // 右侧垂直分割器
    right->setChildrenCollapsible(false);
    right->setHandleWidth(8);

    // 标签页控件：二维影像 / 三维场景
    tabs_ = new QTabWidget(right);          // 创建标签页控件
    imageScene_ = new QGraphicsScene(this); // 创建图形场景（管理所有图形项）

    imageView_ = new QGraphicsView(imageScene_, tabs_);     // 创建图形视图（显示场景内容）
    imageView_->setDragMode(QGraphicsView::ScrollHandDrag); // 设置拖拽模式：手型抓手平移
    imageView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse); // 缩放时以鼠标位置为中心

    // 三维场景页：QOpenGLWidget 点云预览
    scene3DWidget_ = new Scene3DWidget(tabs_);

    tabs_->addTab(imageView_, QStringLiteral("二维影像"));     // 添加"二维影像"标签页
    tabs_->addTab(scene3DWidget_, QStringLiteral("三维场景")); // 添加"三维场景"标签页

    auto *bottomTabs = new QTabWidget(right);
    bottomTabs->setMinimumHeight(180);

    logEdit_ = new QTextEdit(bottomTabs);
    logEdit_->setReadOnly(true);
    bottomTabs->addTab(logEdit_, QStringLiteral("Log"));

    auto *aiPanel = new DomesticVisionChatPanel([this]() {
        QStringList lines;
        lines << QStringLiteral("Imported layer count: %1").arg(layers_.size());
        const auto selected = selectedLayerIndices();
        if (!selected.empty()) {
            QStringList selectedText;
            for (int index : selected) {
                selectedText << QString::number(index);
            }
            lines << QStringLiteral("Selected layer indexes: %1").arg(selectedText.join(QStringLiteral(", ")));
        }

        for (int i = 0; i < layers_.size(); ++i) {
            const auto layer = layers_.at(i);
            QString typeName = QStringLiteral("Unknown");
            switch (layer->type()) {
            case DataType::Raster:
                typeName = QStringLiteral("Raster image");
                break;
            case DataType::PointCloud:
                typeName = QStringLiteral("Point cloud");
                break;
            case DataType::Mesh:
                typeName = QStringLiteral("Mesh");
                break;
            case DataType::Dem:
                typeName = QStringLiteral("DEM");
                break;
            case DataType::Result:
                typeName = QStringLiteral("Processing result");
                break;
            }

            lines << QStringLiteral("\nLayer %1").arg(i);
            lines << QStringLiteral("- Name: %1").arg(layer->name());
            lines << QStringLiteral("- Type: %1").arg(typeName);
            lines << QStringLiteral("- Path: %1").arg(layer->path());
            lines << QStringLiteral("- Summary: %1").arg(layer->summary());
            lines << QStringLiteral("- Visible: %1").arg(layer->visible() ? QStringLiteral("yes") : QStringLiteral("no"));

            if (const auto raster = std::dynamic_pointer_cast<RasterLayer>(layer)) {
                lines << QStringLiteral("- Render: %1").arg(raster->renderDescription());
                lines << QStringLiteral("- Projection: %1").arg(raster->projection().left(240));
                const auto gt = raster->geoTransform();
                lines << QStringLiteral("- GeoTransform: [%1, %2, %3, %4, %5, %6]")
                             .arg(gt[0])
                             .arg(gt[1])
                             .arg(gt[2])
                             .arg(gt[3])
                             .arg(gt[4])
                             .arg(gt[5]);
                const QImage &display = raster->currentDisplayImage();
                if (!display.isNull()) {
                    lines << QStringLiteral("- Display image: %1 x %2").arg(display.width()).arg(display.height());
                }
                const int bandLimit = std::min(raster->bandCount(), 8);
                lines << QStringLiteral("- Band count: %1").arg(raster->bandCount());
                for (int band = 0; band < bandLimit; ++band) {
                    const auto &b = raster->band(band);
                    lines << QStringLiteral("  Band %1: %2, %3 x %4, min=%5, max=%6, nodata=%7")
                                 .arg(band + 1)
                                 .arg(b.name.isEmpty() ? QStringLiteral("(unnamed)") : b.name)
                                 .arg(b.width)
                                 .arg(b.height)
                                 .arg(b.minValue)
                                 .arg(b.maxValue)
                                 .arg(b.hasNoDataValue ? QString::number(b.noDataValue) : QStringLiteral("none"));
                }
                lines << QStringLiteral("- Land-cover note: metadata and band ranges can suggest clues, but reliable object identification needs visual interpretation or a trained classifier.");
            } else if (const auto dem = std::dynamic_pointer_cast<DemLayer>(layer)) {
                const auto &elevations = dem->elevations();
                if (!elevations.isEmpty()) {
                    float minZ = elevations.front();
                    float maxZ = elevations.front();
                    double sumZ = 0.0;
                    for (float z : elevations) {
                        minZ = std::min(minZ, z);
                        maxZ = std::max(maxZ, z);
                        sumZ += z;
                    }
                    lines << QStringLiteral("- Elevation min/max/mean: %1 / %2 / %3")
                                 .arg(minZ)
                                 .arg(maxZ)
                                 .arg(sumZ / elevations.size());
                }
            } else if (const auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer)) {
                const auto &points = pc->points();
                if (!points.isEmpty()) {
                    QVector3D minP = points.front();
                    QVector3D maxP = points.front();
                    for (const QVector3D &p : points) {
                        minP.setX(std::min(minP.x(), p.x()));
                        minP.setY(std::min(minP.y(), p.y()));
                        minP.setZ(std::min(minP.z(), p.z()));
                        maxP.setX(std::max(maxP.x(), p.x()));
                        maxP.setY(std::max(maxP.y(), p.y()));
                        maxP.setZ(std::max(maxP.z(), p.z()));
                    }
                    lines << QStringLiteral("- Bounds: min(%1, %2, %3), max(%4, %5, %6)")
                                 .arg(minP.x())
                                 .arg(minP.y())
                                 .arg(minP.z())
                                 .arg(maxP.x())
                                 .arg(maxP.y())
                                 .arg(maxP.z());
                }
            } else if (const auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer)) {
                const auto &vertices = mesh->vertices();
                if (!vertices.isEmpty()) {
                    QVector3D minP = vertices.front();
                    QVector3D maxP = vertices.front();
                    for (const QVector3D &p : vertices) {
                        minP.setX(std::min(minP.x(), p.x()));
                        minP.setY(std::min(minP.y(), p.y()));
                        minP.setZ(std::min(minP.z(), p.z()));
                        maxP.setX(std::max(maxP.x(), p.x()));
                        maxP.setY(std::max(maxP.y(), p.y()));
                        maxP.setZ(std::max(maxP.z(), p.z()));
                    }
                    lines << QStringLiteral("- Vertex bounds: min(%1, %2, %3), max(%4, %5, %6)")
                                 .arg(minP.x())
                                 .arg(minP.y())
                                 .arg(minP.z())
                                 .arg(maxP.x())
                                 .arg(maxP.y())
                                 .arg(maxP.z());
                    lines << QStringLiteral("- Face count: %1").arg(mesh->faces().size());
                }
            }
        }
        return lines.join(QStringLiteral("\n"));
    }, [this]() {
        const auto raster = selectedRaster();
        if (raster) {
            const int bandIndex = selectedBandIndex();
            if (bandIndex >= 0 && bandIndex < raster->bandCount()) {
                return io::renderSingleBandGray(*raster, bandIndex);
            }
            if (!raster->currentDisplayImage().isNull()) {
                return raster->currentDisplayImage();
            }
        }

        for (int i = 0; i < layers_.size(); ++i) {
            const auto rasterLayer = std::dynamic_pointer_cast<RasterLayer>(layers_.at(i));
            if (rasterLayer && !rasterLayer->currentDisplayImage().isNull()) {
                return rasterLayer->currentDisplayImage();
            }
        }
        return QImage{};
    }, bottomTabs);
    bottomTabs->addTab(aiPanel, QStringLiteral("AI Assistant"));

    auto *aiMenu = menuBar()->addMenu(QStringLiteral("AI"));
    connect(aiMenu->addAction(QStringLiteral("Show AI Assistant")), &QAction::triggered, this,
            [bottomTabs, aiPanel]() { bottomTabs->setCurrentWidget(aiPanel); });

    // 设置分割器拉伸比例（控件随窗口缩放时的比例分配）
    root->setStretchFactor(0, 1);  // 第0个（图层树）：拉伸因子 = 1
    root->setStretchFactor(1, 5);  // 第1个（右侧区域）：拉伸因子 = 5
    right->setStretchFactor(0, 5); // 第0个（影像标签页）：拉伸因子 = 5
    right->setStretchFactor(1, 2); // 第1个（日志面板）：拉伸因子 = 2
    right->setSizes({620, 300});

    setCentralWidget(root); // 将分割器设为窗口的中心控件（填满整个窗口）

    // ── 全局样式美化 ──
    setStyleSheet(QStringLiteral(R"(
        QMainWindow {
            background-color: #fdf6f0;
        }
        QMenuBar {
            background-color: #ffffff;
            color: #5a4a4a;
            font-size: 13px;
            padding: 2px 0;
            border-bottom: 2px solid #f4b8c8;
        }
        QMenuBar::item {
            padding: 6px 16px;
            background: transparent;
        }
        QMenuBar::item:selected {
            background-color: #fce4ec;
            border-radius: 4px;
            color: #8b5a6a;
        }
        QMenu {
            background-color: #ffffff;
            color: #5a4a4a;
            border: 1px solid #f4d0d8;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 24px;
            border-radius: 3px;
        }
        QMenu::item:selected {
            background-color: #fce4ec;
            color: #8b5a6a;
        }
        QMenu::separator {
            height: 1px;
            background: #f0d0d8;
            margin: 4px 8px;
        }
        QTabWidget::pane {
            border: 1px solid #e8d0d8;
            border-top: 2px solid #f4b8c8;
            background-color: #ffffff;
        }
        QTabBar::tab {
            background-color: #fdf0f4;
            color: #8b7a7a;
            padding: 8px 20px;
            border: 1px solid #e8d0d8;
            border-bottom: none;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
            font-size: 12px;
        }
        QTabBar::tab:selected {
            background-color: #ffffff;
            color: #5a4a4a;
            border-bottom: 2px solid #f4b8c8;
            font-weight: bold;
        }
        QTabBar::tab:hover:!selected {
            background-color: #fce4ec;
            color: #5a4a4a;
        }
        QTreeWidget {
            background-color: #fffafa;
            border: 1px solid #e8d0d8;
            border-radius: 4px;
            font-size: 13px;
            color: #5a4a4a;
            alternate-background-color: #fdf6f0;
        }
        QTreeWidget::item {
            padding: 4px 0;
            border-bottom: 1px solid #fdf0f4;
        }
        QTreeWidget::item:selected {
            background-color: #f4b8c8;
            color: #ffffff;
        }
        QTreeWidget::item:hover {
            background-color: #fce4ec;
        }
        QTextEdit {
            background-color: #fff5f5;
            color: #5a4a4a;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 12px;
            border: 1px solid #e8d0d8;
            border-radius: 4px;
            padding: 4px;
        }
        QSplitter::handle {
            background-color: #f0d0d8;
            width: 3px;
        }
        QSplitter::handle:hover {
            background-color: #f4b8c8;
        }
        QScrollBar:vertical {
            background-color: #fdf6f0;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background-color: #f0d0d8;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #e8b8c8;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QSplitter {
            padding: 4px;
        }
    )"));
}

// 打开文件对话框选择遥感影像，使用 GDAL 读取并加载到图层管理器
void MainWindow::openRasterDatasets() {
    // 弹出文件选择对话框，支持多选，过滤遥感影像格式
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,                           // 父窗口
        QStringLiteral("加载遥感影像"), // 对话框标题
        QString(),                      // 默认路径（空 = 上次路径）
        QStringLiteral("Remote sensing rasters (*.tif *.tiff *.img *.dat *.jp2 *.jpg *.jpeg *.png "
                       "*.bmp);;All Files (*.*)")); // 文件过滤器
    if (paths.isEmpty()) {                          // 用户取消选择
        return;                                     // 不做任何操作
    }

    // 遍历所有选中的文件路径
    for (const QString &path : paths) {
        const QFileInfo info(path); // 获取文件信息（文件名、后缀等）
        try {
            // 调用 RasterIO 中的 GDAL 读取函数，读取波段、投影、地理变换和缩略图
            auto raster = rs::io::loadRasterDataset(path);
            if (raster) {
                layers_.add(raster); // 将图层添加到 LayerManager 中
                appendLog(QStringLiteral("已加载影像：%1（%2 波段，%3x%4）")
                              .arg(info.fileName())
                              .arg(raster->bandCount())
                              .arg(raster->bandCount() > 0 ? raster->band(0).width : 0)
                              .arg(raster->bandCount() > 0 ? raster->band(0).height : 0));
            }
        } catch (const std::exception &e) {
            // GDAL 读取失败时记录错误信息
            appendLog(QStringLiteral("加载失败 [%1]：%2")
                          .arg(info.fileName(), QString::fromUtf8(e.what())));
        }
    }
    refreshLayerTree();   // 刷新图层树显示新添加的图层
    updateActionStates(); // 更新菜单启用状态
}

// 加载点云：支持 PLY、XYZ、LAS 格式
void MainWindow::openPointCloud() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载点云"), QString(),
        QStringLiteral("Point Cloud (*.ply *.xyz *.las);;All Files (*.*)"));
    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();

    QVector<QVector3D> points;

    try {
        if (ext == QStringLiteral("xyz")) {
            // XYZ 文本格式
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("无法打开文件");
            }
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    points.append(
                        QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
                }
            }
            file.close();
        } else if (ext == QStringLiteral("ply")) {
            // PLY 格式（支持 ASCII 和二进制小端）
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("无法打开 PLY 文件");
            }
            QByteArray allData = file.readAll();
            file.close();

            // ── 解析文本头 ──
            int headerEndPos = allData.indexOf("end_header");
            if (headerEndPos < 0) {
                throw std::runtime_error("PLY 缺少 end_header");
            }
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
                if (inVertex && line.startsWith(QLatin1String("element "))) {
                    inVertex = false;
                }
                if (inVertex && line.startsWith(QLatin1String("property "))) {
                    propCount++;
                }
                if (line.contains(QLatin1String("format ascii"))) {
                    isAscii = true;
                }
            }

            if (vertexCount <= 0) {
                throw std::runtime_error("PLY 顶点数量无效");
            }

            if (isAscii) {
                // ASCII PLY
                QTextStream in(allData);
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    if (line.trimmed() == QLatin1String("end_header"))
                        break;
                }
                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (line.isEmpty())
                        continue;
                    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (parts.size() < 3)
                        continue;
                    bool xOk, yOk, zOk;
                    float x = parts[0].toFloat(&xOk);
                    float y = parts[1].toFloat(&yOk);
                    float z = parts[2].toFloat(&zOk);
                    if (xOk && yOk && zOk) {
                        points.append(QVector3D(x, y, z));
                    }
                }
            } else {
                // 二进制 PLY（小端 float）
                if (propCount < 3) {
                    throw std::runtime_error("PLY 顶点属性数不足");
                }
                int vertexSize = propCount * sizeof(float);
                const char *vertexData = allData.constData() + headerBytes;
                int remaining = allData.size() - headerBytes;
                int maxVerts = remaining / vertexSize;
                int n = std::min(vertexCount, maxVerts);
                points.reserve(n);
                for (int i = 0; i < n; ++i) {
                    const float *f = reinterpret_cast<const float *>(vertexData + i * vertexSize);
                    points.append(QVector3D(f[0], f[1], f[2]));
                }
            }
        } else if (ext == QStringLiteral("las")) {
            // LAS 格式（使用已定义的 readLasPoints 辅助函数）
            // 需要对应的辅助函数在文件上方定义（其他同学已添加）
            QFile lasFile(path);
            if (!lasFile.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("无法打开 LAS 文件");
            }
            // LAS 格式使用完整的二进制读取方法
            QByteArray lasData = lasFile.readAll();
            lasFile.close();
            // 简单 LAS 读取：仅读取 xyz 点
            if (lasData.size() < 227) {
                throw std::runtime_error("LAS 文件头不完整");
            }
            const unsigned char *hdr = reinterpret_cast<const unsigned char *>(lasData.constData());
            if (hdr[0] != 'L' || hdr[1] != 'A' || hdr[2] != 'S' || hdr[3] != 'F') {
                throw std::runtime_error("无效的 LAS 签名");
            }
            quint32 offset = *reinterpret_cast<const quint32 *>(hdr + 96);
            quint16 recLen = *reinterpret_cast<const quint16 *>(hdr + 105);
            quint32 ptCount = *reinterpret_cast<const quint32 *>(hdr + 107);
            double xScale = *reinterpret_cast<const double *>(hdr + 131);
            double yScale = *reinterpret_cast<const double *>(hdr + 139);
            double zScale = *reinterpret_cast<const double *>(hdr + 147);
            double xOff = *reinterpret_cast<const double *>(hdr + 155);
            double yOff = *reinterpret_cast<const double *>(hdr + 163);
            double zOff = *reinterpret_cast<const double *>(hdr + 171);
            if (recLen < 12)
                throw std::runtime_error("LAS 记录长度无效");
            quint64 totalPoints = ptCount;
            if (totalPoints == 0 && lasData.size() >= 255) {
                totalPoints = *reinterpret_cast<const quint64 *>(hdr + 247);
            }
            quint64 maxRead = (lasData.size() - offset) / recLen;
            quint64 n = std::min(totalPoints, maxRead);
            if (n > 10000000)
                n = 10000000; // 最多读取 1000 万点
            points.reserve(static_cast<int>(n));
            for (quint64 i = 0; i < n; ++i) {
                const char *rec = lasData.constData() + offset + i * recLen;
                qint32 ix = *reinterpret_cast<const qint32 *>(rec);
                qint32 iy = *reinterpret_cast<const qint32 *>(rec + 4);
                qint32 iz = *reinterpret_cast<const qint32 *>(rec + 8);
                points.append(QVector3D(static_cast<float>(ix * xScale + xOff),
                                        static_cast<float>(iy * yScale + yOff),
                                        static_cast<float>(iz * zScale + zOff)));
            }
        } else {
            throw std::runtime_error("不支持的格式");
        }

        if (points.isEmpty()) {
            throw std::runtime_error("未能读取到任何点数据");
        }

        auto layer = std::make_shared<PointCloudLayer>(info.fileName(), path, points);
        layers_.add(layer);
        appendLog(
            QStringLiteral("已加载点云：%1（%2 个点）").arg(info.fileName()).arg(points.size()));

        // 在三维窗口中显示点云
        scene3DWidget_->setPoints(points);
        tabs_->setCurrentWidget(scene3DWidget_);
    } catch (const std::exception &e) {
        appendLog(QStringLiteral("点云加载失败 [%1]：%2")
                      .arg(info.fileName(), QString::fromUtf8(e.what())));
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载三维网格模型
void MainWindow::openMesh() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("加载 Mesh"), QString(),
                                     QStringLiteral("Mesh (*.obj *.ply);;All Files (*.*)"));
    if (path.isEmpty())
        return;

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
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.isEmpty())
                    continue;

                if (parts[0] == QStringLiteral("v") && parts.size() >= 4) {
                    vertices.append(
                        QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
                } else if (parts[0] == QStringLiteral("f") && parts.size() >= 4) {
                    const auto parseIndex = [](const QString &token) {
                        return token.section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    };
                    const int i0 = parseIndex(parts[1]);
                    for (int t = 2; t < parts.size() - 1; ++t) {
                        Face face;
                        face.a = i0;
                        face.b = parseIndex(parts[t]);
                        face.c = parseIndex(parts[t + 1]);
                        faces.append(face);
                    }
                }
            }
            file.close();
        } else if (ext == QStringLiteral("ply")) {
            const LoadedMeshData plyMesh = loadMeshFromPly(path);
            vertices = plyMesh.vertices;
            faces = plyMesh.faces;
        } else {
            throw std::runtime_error("不支持的格式: " + ext.toStdString() + "，仅支持 OBJ/PLY");
        }

        if (vertices.isEmpty()) {
            throw std::runtime_error("未能读取到任何顶点数据");
        }

        auto layer = std::make_shared<MeshLayer>(info.fileName(), path, vertices, faces);
        layers_.add(layer);
        scene3DWidget_->setMesh(vertices, faces);
        scene3DWidget_->fitToBounds();
        tabs_->setCurrentWidget(scene3DWidget_);
        scene3DWidget_->update();
        if (faces.isEmpty()) {
            appendLog(QStringLiteral("已加载 Mesh：%1（%2 个顶点，未读取到三角面，已在三维场景显示顶点）")
                          .arg(info.fileName())
                          .arg(vertices.size()));
        } else {
            appendLog(QStringLiteral("已加载 Mesh：%1（%2 个顶点，%3 个三角面）")
                          .arg(info.fileName())
                          .arg(vertices.size())
                          .arg(faces.size()));
        }
    } catch (const std::exception &e) {
        appendLog(QStringLiteral("Mesh 加载失败 [%1]：%2")
                      .arg(info.fileName(), QString::fromUtf8(e.what())));
    }
    refreshLayerTree();
    updateActionStates();
}

// 加载 DEM：使用 GDAL 读取 DEM GeoTIFF/ASCII Grid 格式
void MainWindow::openDem() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载 DEM"), QString(),
        QStringLiteral("Digital Elevation Model (*.tif *.tiff *.asc *.dem);;All Files (*.*)"));
    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    try {
        const auto dem = io::loadDemDataset(path, info.fileName());
        layers_.add(dem);
        appendLog(QStringLiteral("已加载 DEM：%1（%2x%3）")
                      .arg(info.fileName())
                      .arg(dem->width())
                      .arg(dem->height()));
    } catch (const std::exception &e) {
        const QString reason = QString::fromUtf8(e.what());
        appendLog(QStringLiteral("DEM 加载失败 [%1]：%2").arg(info.fileName(), reason));
        if (reason.contains(QStringLiteral("未启用 GDAL"))) {
            QMessageBox::warning(this, QStringLiteral("GDAL 未启用"),
                                 QStringLiteral("当前构建未启用 GDAL，无法读取 DEM 文件。\n"
                                                "请使用 MSYS2 脚本 build_msys2_ucrt.ps1 构建，或在 CMake 中启用 GDAL。"));
        }
    }
    refreshLayerTree();
    updateActionStates();
}

// 删除图层树中选中的图层
void MainWindow::deleteSelectedLayers() {
    const auto indices = selectedLayerIndices();
    if (indices.empty()) {
        return;
    }

    // 检查是否有被删除的点云，如有则清空三维场景
    bool hasPointCloud = false;
    for (const int idx : indices) {
        try {
            if (layers_.at(idx)->type() == DataType::PointCloud) {
                hasPointCloud = true;
                break;
            }
        } catch (...) {
        }
    }

    layers_.removeMany(indices);
    imageScene_->clear();
    if (hasPointCloud) {
        scene3DWidget_->setPoints({});
    }
    refreshLayerTree();
    appendLog(QStringLiteral("已删除 %1 个选中图层。").arg(indices.size()));
    updateActionStates();
}

// 清空所有图层，重置工程
void MainWindow::clearProject() {
    layers_.clear();      // 清空所有图层
    imageScene_->clear(); // 清空图像场景
    refreshLayerTree();   // 刷新图层树
    appendLog(QStringLiteral("工程已初始化。"));
    updateActionStates(); // 更新菜单所有操作按钮的状态
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
        image = io::renderRgbComposite(*raster, request->redBand, request->greenBand,
                                       request->blueBand);
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
        appendLog(QStringLiteral("渲染失败：%1，请确认加载时已读取像素样本且波段索引有效。")
                      .arg(raster->name()));
        return;
    }
    raster->setCurrentDisplayImage(image);
    raster->setRenderDescription(description);
    displayRaster(raster, -1);
    appendLog(QStringLiteral("已渲染影像：%1，%2。").arg(raster->name(), description));
}

// 执行灰度直方图算法
void MainWindow::runHistogram() {
    const auto raster = selectedRaster();
    if (!raster) {
        appendLog(QStringLiteral("请先选择一个遥感影像图层。"));
        return;
    }

    const int bandIdx = selectedBandIndex();
    const int bandCount = raster->bandCount();

    // 让用户选择波段
    int targetBand = bandIdx >= 0 && bandIdx < bandCount ? bandIdx : 0;
    if (bandCount > 1) {
        QStringList bandNames;
        for (int i = 0; i < bandCount; ++i)
            bandNames << QStringLiteral("Band %1").arg(i + 1);
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, QStringLiteral("选择波段"),
                                                     QStringLiteral("请选择要统计直方图的波段："),
                                                     bandNames, targetBand, false, &ok);
        if (!ok)
            return;
        targetBand = bandNames.indexOf(chosen);
    }

    // 让用户输入分箱数
    bool ok = false;
    const int bins = QInputDialog::getInt(this, QStringLiteral("直方图参数"),
                                          QStringLiteral("分箱数："), 256, 2, 65536, 1, &ok);
    if (!ok)
        return;

    // 执行算法（先模拟，等人员二实现后替换以下代码）
    ProcessingContext ctx;
    ctx.bandIndex = targetBand;
    ctx.parameters[QStringLiteral("bins")] = bins;
    ctx.parameters[QStringLiteral("ignoreNoData")] = true;

    HistogramAlgorithm algorithm;
    const auto result = algorithm.execute(*raster, ctx);
    applyProcessingResult(result, raster, QStringLiteral("灰度直方图"), QStringLiteral("_直方图"));
}

// 执行直方图均衡化算法
void MainWindow::runHistogramEqualization() {
    const auto raster = selectedRaster();
    if (!raster) {
        appendLog(QStringLiteral("请先选择一个遥感影像图层。"));
        return;
    }

    HistogramEqualizationAlgorithm algorithm;
    ProcessingContext ctx;
    ctx.bandIndex = selectedBandIndex();
    executeRasterAlgorithm(algorithm, ctx);
}

// 执行 ORB/SIFT 特征提取
void MainWindow::runFeatureExtraction() {
    const auto raster = selectedRaster();
    if (!raster) {
        appendLog(QStringLiteral("请先选择一个遥感影像图层。"));
        return;
    }

    // 让用户选择特征提取方法
    QStringList methods = {QStringLiteral("ORB"), QStringLiteral("SIFT"), QStringLiteral("AKAZE")};
    bool ok = false;
    const QString method =
        QInputDialog::getItem(this, QStringLiteral("特征提取方法"),
                              QStringLiteral("请选择特征提取方法："), methods, 0, false, &ok);
    if (!ok)
        return;

    // 让用户输入最大特征数
    const int maxFeatures =
        QInputDialog::getInt(this, QStringLiteral("特征提取参数"), QStringLiteral("最大特征数："),
                             2000, 10, 100000, 100, &ok);
    if (!ok)
        return;

    FeatureExtractionAlgorithm algorithm;
    ProcessingContext ctx;
    ctx.bandIndex = selectedBandIndex();
    ctx.parameters[QStringLiteral("method")] = method;
    ctx.parameters[QStringLiteral("maxFeatures")] = maxFeatures;
    executeRasterAlgorithm(algorithm, ctx);
}

// 执行 DEM 重建流程
void MainWindow::runDemReconstruction() {
    // 需要选中两个栅格影像
    const auto indices = selectedLayerIndices();
    std::vector<std::shared_ptr<RasterLayer>> selectedRasters;
    for (const int idx : indices) {
        try {
            auto r = std::dynamic_pointer_cast<RasterLayer>(layers_.at(idx));
            if (r)
                selectedRasters.push_back(std::move(r));
        } catch (...) {
        }
    }

    if (selectedRasters.size() < 2) {
        appendLog(QStringLiteral("请选中两个遥感影像作为立体像对（左影像和右影像）。"));
        return;
    }

    const auto &leftImage = selectedRasters[0];
    const auto &rightImage = selectedRasters[1];

    // 让用户选择输出目录
    const QString outputDir =
        QFileDialog::getExistingDirectory(this, QStringLiteral("选择 DEM 输出目录"), QString());
    if (outputDir.isEmpty())
        return;

    // DEM 重建在后台线程执行，避免 SGBM 阻塞 UI
    const auto leftCopy = leftImage;
    const auto rightCopy = rightImage;
    const QString outputDirCopy = outputDir;

    auto *progress = new QProgressDialog(QStringLiteral("正在执行 SGBM 立体匹配，请稍候…"), QString(),
                                         0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setWindowTitle(QStringLiteral("DEM 重建"));
    progress->show();
    QApplication::processEvents();

    auto *watcher = new QFutureWatcher<ProcessingResult>(this);
    connect(watcher, &QFutureWatcher<ProcessingResult>::finished, this,
            [this, watcher, progress, leftCopy]() {
                progress->close();
                progress->deleteLater();

                const ProcessingResult result = watcher->result();
                watcher->deleteLater();

                appendLog(result.message);
                if (result.demResult) {
                    result.demResult->setTreeGroup(QStringLiteral("DEM 重建"));
                    layers_.add(result.demResult);
                }
                if (!result.image.isNull()) {
                    auto preview = std::make_shared<RasterLayer>(
                        leftCopy->name() + QStringLiteral("_DEM预览"), QString(),
                        QVector<RasterBand>{}, result.image);
                    preview->setTreeGroup(QStringLiteral("DEM 重建"));
                    preview->setRenderDescription(QStringLiteral("立体匹配预览"));
                    layers_.add(preview);
                }
                refreshLayerTree();
                if (result.demResult || !result.image.isNull()) {
                    revealLayerInTree(layers_.size() - 1);
                }
                updateActionStates();
            });

    watcher->setFuture(QtConcurrent::run([leftCopy, rightCopy, outputDirCopy]() {
        ProcessingContext ctx;
        ctx.auxiliaryRaster = rightCopy.get();
        ctx.parameters[QStringLiteral("outputDirectory")] = outputDirCopy;
        DemReconstructionAlgorithm algorithm;
        return algorithm.execute(*leftCopy, ctx);
    }));
}

// 执行正射校正流程
void MainWindow::runOrthorectification() {
    // 需要选中一个影像和一个 DEM
    std::shared_ptr<RasterLayer> raster;
    std::shared_ptr<DemLayer> dem;

    for (const int idx : selectedLayerIndices()) {
        try {
            const auto &layer = layers_.at(idx);
            if (!raster)
                raster = std::dynamic_pointer_cast<RasterLayer>(layer);
            if (!dem)
                dem = std::dynamic_pointer_cast<DemLayer>(layer);
        } catch (...) {
        }
    }

    if (!raster || !dem) {
        appendLog(QStringLiteral("请选中一个遥感影像和一个 DEM 图层。"));
        return;
    }

    // 正射校正（使用统一 ProcessingAlgorithm 接口，DEM 通过 context 传递）
    ProcessingContext ctx;
    ctx.auxiliaryDem = dem.get();

    OrthorectificationAlgorithm algorithm;
    const auto result = algorithm.execute(*raster, ctx);
    if (result.image.isNull()) {
        appendLog(result.message.isEmpty() ? QStringLiteral("正射校正失败。") : result.message);
        return;
    }

    const QString resultName = raster->name() + QStringLiteral("_正射");
    auto resultLayer = std::make_shared<RasterLayer>(resultName, QString(), QVector<RasterBand>{},
                                                     result.image);
    resultLayer->setRenderDescription(QStringLiteral("正射校正结果"));
    resultLayer->setTreeGroup(QStringLiteral("正射影像校正"));
    layers_.add(resultLayer);
    refreshLayerTree();
    revealLayerInTree(layers_.size() - 1);
    appendLog(result.message);
}

// ============ 三维点云/Mesh============

// ── 导出 PLY ──
void MainWindow::exportPly() {
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
            if (path.isEmpty())
                continue;

            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                appendLog(QStringLiteral("无法创建文件: %1").arg(path));
                continue;
            }

            QTextStream out(&file);

            if (auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer)) {
                // 导出点云
                const auto &points = pc->points();
                out << "ply\nformat ascii 1.0\n"
                    << "element vertex " << points.size() << "\n"
                    << "property float x\nproperty float y\nproperty float z\n"
                    << "end_header\n";
                for (const auto &p : points) {
                    out << p.x() << " " << p.y() << " " << p.z() << "\n";
                }
                appendLog(
                    QStringLiteral("已导出点云 PLY：%1（%2 个点）").arg(path).arg(points.size()));
            } else if (auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer)) {
                // 导出 Mesh
                const auto &verts = mesh->vertices();
                const auto &faces = mesh->faces();
                out << "ply\nformat ascii 1.0\n"
                    << "element vertex " << verts.size() << "\n"
                    << "property float x\nproperty float y\nproperty float z\n"
                    << "element face " << faces.size() << "\n"
                    << "property list uchar int vertex_indices\n"
                    << "end_header\n";
                for (const auto &v : verts) {
                    out << v.x() << " " << v.y() << " " << v.z() << "\n";
                }
                for (const auto &f : faces) {
                    out << "3 " << f.a << " " << f.b << " " << f.c << "\n";
                }
                appendLog(QStringLiteral("已导出 Mesh PLY：%1（%2 顶点，%3 面）")
                              .arg(path)
                              .arg(verts.size())
                              .arg(faces.size()));
            } else {
                appendLog(QStringLiteral("选中图层不是点云或 Mesh，无法导出。"));
            }
            file.close();
        } catch (const std::exception &e) {
            appendLog(QStringLiteral("导出失败：%1").arg(QString::fromUtf8(e.what())));
        }
    }
}

// ── 点云体素降采样 ──
void MainWindow::runPointCloudDownsample() {
    const auto indices = selectedLayerIndices();
    std::shared_ptr<PointCloudLayer> pcLayer;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            pcLayer = std::dynamic_pointer_cast<PointCloudLayer>(layer);
            if (pcLayer)
                break;
        } catch (...) {
        }
    }
    if (!pcLayer) {
        appendLog(QStringLiteral("请先选中一个点云图层。"));
        return;
    }

    const auto &points = pcLayer->points();

    // 体素降采样（使用统一 ProcessingAlgorithm 接口）
    ProcessingContext ctx;
    ctx.pointCloudData = &points;
    ctx.parameters["voxelSize"] = 0.1;

    // 构造一个最小 RasterLayer 满足接口要求（算法内部忽略该参数）
    RasterLayer dummyInput(pcLayer->name(), pcLayer->path());
    PointCloudVoxelDownsampleAlgorithm algo;
    const auto result = algo.execute(dummyInput, ctx);

    if (result.pointCloudResult.isEmpty() && !result.message.isEmpty()) {
        appendLog(result.message);
        return;
    }

    auto newLayer = std::make_shared<PointCloudLayer>(pcLayer->name() + QStringLiteral("_降采样"),
                                                      QString(), result.pointCloudResult);
    layers_.add(newLayer);
    scene3DWidget_->setPoints(result.pointCloudResult);
    tabs_->setCurrentWidget(scene3DWidget_);
    refreshLayerTree();
    appendLog(result.message);
}

// ── 点云统计滤波 ──
void MainWindow::runPointCloudFilter() {
    const auto indices = selectedLayerIndices();
    std::shared_ptr<PointCloudLayer> pcLayer;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            pcLayer = std::dynamic_pointer_cast<PointCloudLayer>(layer);
            if (pcLayer)
                break;
        } catch (...) {
        }
    }
    if (!pcLayer) {
        appendLog(QStringLiteral("请先选中一个点云图层。"));
        return;
    }

    const auto &points = pcLayer->points();

    // 统计滤波（使用统一 ProcessingAlgorithm 接口）
    ProcessingContext ctx;
    ctx.pointCloudData = &points;
    ctx.parameters["meanK"] = 20;
    ctx.parameters["stddevThreshold"] = 2.0;

    RasterLayer dummyInput(pcLayer->name(), pcLayer->path());
    PointCloudStatisticalFilterAlgorithm algo;
    const auto result = algo.execute(dummyInput, ctx);

    if (result.pointCloudResult.isEmpty() && !result.message.isEmpty()) {
        appendLog(result.message);
        return;
    }

    auto newLayer = std::make_shared<PointCloudLayer>(pcLayer->name() + QStringLiteral("_滤波"),
                                                      QString(), result.pointCloudResult);
    layers_.add(newLayer);
    scene3DWidget_->setPoints(result.pointCloudResult);
    tabs_->setCurrentWidget(scene3DWidget_);
    refreshLayerTree();
    appendLog(result.message);
}

// ── 点云转 DEM ──
void MainWindow::runPointCloudToDem() {
    const auto indices = selectedLayerIndices();
    std::shared_ptr<PointCloudLayer> pcLayer;
    for (int idx : indices) {
        try {
            auto layer = layers_.at(idx);
            pcLayer = std::dynamic_pointer_cast<PointCloudLayer>(layer);
            if (pcLayer)
                break;
        } catch (...) {
        }
    }
    if (!pcLayer) {
        appendLog(QStringLiteral("请先选中一个点云图层。"));
        return;
    }

    const auto &points = pcLayer->points();

    // 点云转 DEM（使用统一 ProcessingAlgorithm 接口）
    ProcessingContext ctx;
    ctx.pointCloudData = &points;
    ctx.parameters["gridResolution"] = 1.0;
    ctx.parameters["useMaxZ"] = true;
    ctx.parameters["layerName"] = pcLayer->name() + QStringLiteral("_DSM");

    RasterLayer dummyInput(pcLayer->name(), pcLayer->path());
    rs::PointCloudToDemAlgorithm algo;
    const auto result = algo.execute(dummyInput, ctx);

    if (result.demResult) {
        layers_.add(std::static_pointer_cast<DataObject>(result.demResult));
        refreshLayerTree();
        appendLog(result.message);
    } else {
        appendLog(result.message);
    }
}

// 当图层树选中项改变时，刷新影像显示和菜单状态
void MainWindow::onSelectionChanged() {
    const auto selected = selectedRaster();
    if (selected) {
        displayRaster(selected, selectedBandIndex());
    } else {
        // 检查是否选中了 3D 类型的图层
        const auto indices = selectedLayerIndices();
        for (int idx : indices) {
            try {
                const auto layer = layers_.at(idx);
                if (layer->type() == DataType::PointCloud) {
                    const auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer);
                    if (pc && !pc->points().isEmpty()) {
                        scene3DWidget_->setPoints(pc->points());
                        tabs_->setCurrentWidget(scene3DWidget_);
                    }
                } else if (layer->type() == DataType::Mesh) {
                    const auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer);
                    if (mesh && !mesh->vertices().isEmpty()) {
                        scene3DWidget_->setMesh(mesh->vertices(), mesh->faces());
                        scene3DWidget_->fitToBounds();
                        tabs_->setCurrentWidget(scene3DWidget_);
                    }
                } else if (layer->type() == DataType::Dem) {
                    const auto dem = std::dynamic_pointer_cast<DemLayer>(layer);
                    if (dem) {
                        scene3DWidget_->setDem(*dem);
                        scene3DWidget_->fitToBounds();
                        tabs_->setCurrentWidget(scene3DWidget_);
                    }
                }
            } catch (...) {
            }
        }
    }
    updateActionStates();
}

// 当图层项的勾选状态改变时，切换其可见性
void MainWindow::onLayerItemChanged(QTreeWidgetItem *item, int column) {
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
        const bool visible = item->checkState(0) == Qt::Checked;
        layers_.at(value.toInt())->setVisible(visible);
        const auto layer = layers_.at(value.toInt());
        if (layer->type() == DataType::PointCloud) {
            if (visible) {
                const auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer);
                if (pc) {
                    scene3DWidget_->setPoints(pc->points());
                    tabs_->setCurrentWidget(scene3DWidget_);
                }
            } else {
                scene3DWidget_->setPoints({});
            }
        } else if (layer->type() == DataType::Mesh) {
            if (visible) {
                const auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer);
                if (mesh) {
                    scene3DWidget_->setMesh(mesh->vertices(), mesh->faces());
                    scene3DWidget_->fitToBounds();
                    tabs_->setCurrentWidget(scene3DWidget_);
                }
            } else {
                scene3DWidget_->clearData();
            }
        } else if (layer->type() == DataType::Dem) {
            if (visible) {
                const auto dem = std::dynamic_pointer_cast<DemLayer>(layer);
                if (dem) {
                    scene3DWidget_->setDem(*dem);
                    scene3DWidget_->fitToBounds();
                    tabs_->setCurrentWidget(scene3DWidget_);
                }
            } else {
                scene3DWidget_->clearData();
            }
        } else if (layer->type() == DataType::Raster) {
            // 二维影像图层：刷新当前显示的影像
            const auto selected = selectedRaster();
            const auto toggled = std::dynamic_pointer_cast<RasterLayer>(layer);
            if (selected == toggled) {
                if (visible) {
                    displayRaster(toggled, selectedBandIndex());
                } else {
                    displayRaster(nullptr, -1);
                }
            }
        }
        appendLog(QStringLiteral("%1：%2").arg(item->text(0), visible ? QStringLiteral("显示")
                                                                      : QStringLiteral("隐藏")));
    } catch (const std::exception &) {
    }
}

// 右键点击图层树时弹出上下文菜单
void MainWindow::showLayerContextMenu(const QPoint &position) {
    QTreeWidgetItem *item = layerTree_->itemAt(position);
    if (!item) {
        return;
    }
    const QVariant layerIndexVar = item->data(0, kLayerIndexRole);
    const int nodeKind = item->data(0, kNodeKindRole).toInt();
    QMenu menu(this);

    if (!layerIndexVar.isValid() || nodeKind != static_cast<int>(NodeKind::Layer)) {
        // 文件夹或波段节点：只允许删除选中图层
        const auto indices = selectedLayerIndices();
        QAction *deleteAction = menu.addAction(QStringLiteral("删除选中图层"));
        deleteAction->setEnabled(!indices.empty());
        connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
        menu.exec(layerTree_->viewport()->mapToGlobal(position));
        return;
    }

    // 右键点击某个图层，自动选中它
    layerTree_->setCurrentItem(item);

    const int layerIndex = layerIndexVar.toInt();
    std::shared_ptr<DataObject> layer;
    try {
        layer = layers_.at(layerIndex);
    } catch (const std::exception &) {
        return;
    }

    // ── 删除图层 ──
    QAction *deleteAction = menu.addAction(QStringLiteral("删除图层"));
    connect(deleteAction, &QAction::triggered, this, [this, layerIndex]() {
        try {
            if (layers_.at(layerIndex)->type() == DataType::PointCloud) {
                scene3DWidget_->setPoints({});
            }
        } catch (...) {
        }
        layers_.removeMany({layerIndex});
        imageScene_->clear();
        refreshLayerTree();
        appendLog(QStringLiteral("已删除图层。"));
        updateActionStates();
    });

    // ── 导出 ──
    if (layer->type() == DataType::Raster || layer->type() == DataType::Dem) {
        menu.addSeparator();
        QAction *exportAction = menu.addAction(QStringLiteral("导出..."));
        connect(exportAction, &QAction::triggered, this, [this, layerIndex]() {
            exportLayerImage(layerIndex);
        });
    }

    // ── 缩放至范围 ──
    QAction *zoomAction = menu.addAction(QStringLiteral("缩放至范围"));
    connect(zoomAction, &QAction::triggered, this, [this, layer]() {
        if (layer->type() == DataType::Raster) {
            appendLog(QStringLiteral("TODO: 缩放至 %1 的影像范围。").arg(layer->name()));
        } else if (layer->type() == DataType::PointCloud || layer->type() == DataType::Mesh) {
            scene3DWidget_->fitToBounds();
            appendLog(QStringLiteral("缩放至 %1 的范围。").arg(layer->name()));
        } else {
            appendLog(QStringLiteral("TODO: 缩放至 %1 的范围。").arg(layer->name()));
        }
    });

    // ── 属性对话框 ──
    QAction *propAction = menu.addAction(QStringLiteral("属性"));
    connect(propAction, &QAction::triggered, this, [this, layer]() {
        QString typeName;
        switch (layer->type()) {
        case DataType::Raster:
            typeName = QStringLiteral("遥感影像");
            break;
        case DataType::PointCloud:
            typeName = QStringLiteral("点云");
            break;
        case DataType::Mesh:
            typeName = QStringLiteral("网格模型");
            break;
        case DataType::Dem:
            typeName = QStringLiteral("数字高程模型");
            break;
        case DataType::Result:
            typeName = QStringLiteral("处理结果");
            break;
        }

        QString info;
        info += QStringLiteral("名称: %1\n").arg(layer->name());
        info += QStringLiteral("路径: %1\n").arg(layer->path());
        info += QStringLiteral("类型: %1\n").arg(typeName);
        info += QStringLiteral("可见: %1\n")
                    .arg(layer->visible() ? QStringLiteral("是") : QStringLiteral("否"));

        if (const auto raster = std::dynamic_pointer_cast<RasterLayer>(layer)) {
            info += QStringLiteral("波段数: %1\n").arg(raster->bandCount());
            if (raster->bandCount() > 0) {
                const auto &b = raster->band(0);
                info += QStringLiteral("尺寸: %1 x %2像素\n").arg(b.width).arg(b.height);
            }
            info += QStringLiteral("投影: %1\n")
                        .arg(raster->projection().isEmpty() ? QStringLiteral("(未知)")
                                                            : raster->projection());
        } else if (const auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer)) {
            info += QStringLiteral("点数: %1\n").arg(pc->points().size());
        } else if (const auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer)) {
            info += QStringLiteral("顶点数: %1\n").arg(mesh->vertices().size());
            info += QStringLiteral("三角面: %1\n").arg(mesh->faces().size());
        } else if (const auto dem = std::dynamic_pointer_cast<DemLayer>(layer)) {
            info += QStringLiteral("尺寸: %1 x %2\n").arg(dem->width()).arg(dem->height());
        }

        info += QStringLiteral("\n摘要: %1").arg(layer->summary());
        QMessageBox::information(nullptr, QStringLiteral("图层属性 - %1").arg(layer->name()), info);
    });

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

    auto *sourceRoot = ensureTopFolder(layerTree_, QStringLiteral("源数据"));
    auto *resultRoot = ensureTopFolder(layerTree_, QStringLiteral("处理结果"));
    auto *rasterFolder = ensureChildFolder(sourceRoot, QStringLiteral("遥感影像"));
    auto *pointFolder = ensureChildFolder(sourceRoot, QStringLiteral("点云"));
    auto *meshFolder = ensureChildFolder(sourceRoot, QStringLiteral("Mesh"));
    auto *demFolder = ensureChildFolder(sourceRoot, QStringLiteral("DEM"));

    for (int i = 0; i < layers_.size(); ++i) {
        const auto layer = layers_.at(i);
        QTreeWidgetItem *parent = nullptr;
        if (!layer->treeGroup().isEmpty()) {
            parent = ensureChildFolder(resultRoot, layer->treeGroup());
        } else {
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
        }

        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, QStringLiteral("%1  [%2]").arg(layer->name(), layer->summary()));
        item->setData(0, kLayerIndexRole, i);
        item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Layer));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable |
                       Qt::ItemIsEnabled);
        item->setCheckState(0, layer->visible() ? Qt::Checked : Qt::Unchecked);

        if (const auto raster = std::dynamic_pointer_cast<RasterLayer>(layer)) {
            if (raster->bandCount() > 0) {
                for (int band = 0; band < raster->bandCount(); ++band) {
                    const auto &bandInfo = raster->band(band);
                    auto *child = new QTreeWidgetItem(item);
                    child->setText(0, QStringLiteral("Band %1  %2 x %3")
                                          .arg(band + 1)
                                          .arg(bandInfo.width)
                                          .arg(bandInfo.height));
                    child->setData(0, kLayerIndexRole, i);
                    child->setData(0, kBandIndexRole, band);
                    child->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Band));
                }
            }
        }
    }

    const bool firstBuild = expandedKeys.isEmpty();
    for (int i = 0; i < layerTree_->topLevelItemCount(); ++i) {
        auto *top = layerTree_->topLevelItem(i);
        top->setExpanded(firstBuild || expandedKeys.contains(itemKey(top)));
        for (int j = 0; j < top->childCount(); ++j) {
            auto *child = top->child(j);
            child->setExpanded(firstBuild || expandedKeys.contains(itemKey(child)));
            for (int k = 0; k < child->childCount(); ++k) {
                auto *layer = child->child(k);
                layer->setExpanded(firstBuild || expandedKeys.contains(itemKey(layer)));
            }
        }
    }

    rebuildingTree_ = false;
}

// 在 QGraphicsView 中显示选中的影像（优先显示选中波段）
void MainWindow::displayRaster(const std::shared_ptr<RasterLayer> &raster, int bandIndex) {
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
        imageScene_->addText(
            QStringLiteral("当前影像没有可显示的渲染结果。\n当前图层：%1").arg(raster->name()));
        return;
    }

    imageScene_->addPixmap(QPixmap::fromImage(image));
    imageScene_->setSceneRect(image.rect());
    imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio);
}

// 获取当前选中的所有图层的索引列表（去重）
std::vector<int> MainWindow::selectedLayerIndices() const {
    std::vector<int> indices;
    for (const auto *item : layerTree_->selectedItems()) {
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
    auto *item = layerTree_->currentItem();
    if (!item) {
        return {};
    }
    const QVariant value = item->data(0, kLayerIndexRole);
    if (!value.isValid()) {
        return {};
    }
    try {
        return std::dynamic_pointer_cast<RasterLayer>(layers_.at(value.toInt()));
    } catch (const std::exception &) {
        return {};
    }
}

// 获取当前选中的波段索引（-1 表示未选中具体波段）
int MainWindow::selectedBandIndex() const {
    auto *item = layerTree_->currentItem();
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
    int selectedPointClouds = 0;
    for (const int index : selectedLayerIndices()) {
        try {
            const auto layer = layers_.at(index);
            if (std::dynamic_pointer_cast<RasterLayer>(layer)) {
                ++selectedRasters;
            } else if (layer->type() == DataType::Dem) {
                ++selectedDems;
            } else if (layer->type() == DataType::PointCloud) {
                ++selectedPointClouds;
            }
        } catch (const std::exception &) {
        }
    }

    const bool hasOneRaster = selectedRasters == 1;
    const bool hasPointCloud = selectedPointClouds >= 1;
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
    if (downsampleAction_) {
        downsampleAction_->setEnabled(hasPointCloud);
    }
    if (filterAction_) {
        filterAction_->setEnabled(hasPointCloud);
    }
    if (pcToDemAction_) {
        pcToDemAction_->setEnabled(hasPointCloud);
    }
    if (exportPlyAction_) {
        exportPlyAction_->setEnabled(hasPointCloud);
    }
}

// 在日志面板追加带时间戳的信息
void MainWindow::appendLog(const QString &text) {
    logEdit_->append(QStringLiteral("[%1] %2").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void MainWindow::executeRasterAlgorithm(const ProcessingAlgorithm &algorithm, ProcessingContext ctx) {
    const auto raster = selectedRaster();
    if (!raster) {
        appendLog(QStringLiteral("请先选择一个遥感影像图层。"));
        return;
    }
    if (ctx.bandIndex < 0)
        ctx.bandIndex = selectedBandIndex();
    const ProcessingResult result = algorithm.execute(*raster, ctx);
    applyProcessingResult(result, raster, algorithm.name());
}

void MainWindow::revealLayerInTree(int layerIndex) {
    QTreeWidgetItemIterator it(layerTree_);
    while (*it) {
        QTreeWidgetItem *item = *it;
        if (static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt()) == NodeKind::Layer &&
            item->data(0, kLayerIndexRole).toInt() == layerIndex) {
            for (QTreeWidgetItem *parent = item->parent(); parent; parent = parent->parent()) {
                parent->setExpanded(true);
            }
            return;
        }
        ++it;
    }
}

void MainWindow::exportLayerImage(int layerIndex) {
    std::shared_ptr<DataObject> layer;
    try {
        layer = layers_.at(layerIndex);
    } catch (const std::exception &) {
        return;
    }

    if (const auto raster = std::dynamic_pointer_cast<RasterLayer>(layer)) {
        QImage image = raster->currentDisplayImage();
        if (image.isNull()) {
            if (raster->bandCount() >= 3) {
                image = io::renderRgbComposite(*raster, 0, 1, 2);
            } else if (raster->bandCount() >= 1) {
                image = io::renderSingleBandGray(*raster, 0);
            }
        }
        if (image.isNull()) {
            appendLog(QStringLiteral("导出失败 [%1]：没有可导出的影像。").arg(raster->name()));
            return;
        }

        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出影像"), raster->name(),
            QStringLiteral("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;TIFF (*.tif *.tiff)"));
        if (path.isEmpty()) {
            return;
        }
        if (!image.save(path)) {
            appendLog(QStringLiteral("导出失败：无法写入 %1").arg(path));
            return;
        }
        appendLog(QStringLiteral("已导出影像：%1 → %2").arg(raster->name(), path));
        return;
    }

    if (const auto dem = std::dynamic_pointer_cast<DemLayer>(layer)) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出 DEM"), dem->name() + QStringLiteral(".tif"),
            QStringLiteral("GeoTIFF (*.tif *.tiff)"));
        if (path.isEmpty()) {
            return;
        }
        try {
            io::exportDemAsGeoTiff(*dem, path);
            appendLog(QStringLiteral("已导出 DEM：%1 → %2").arg(dem->name(), path));
        } catch (const std::exception &e) {
            appendLog(QStringLiteral("DEM 导出失败：%1").arg(QString::fromUtf8(e.what())));
        }
    }
}

void MainWindow::applyProcessingResult(const ProcessingResult &result,
                                       const std::shared_ptr<RasterLayer> &source,
                                       const QString &treeGroup, const QString &suffix) {
    if (!result.message.isEmpty())
        appendLog(result.message);

    int addedIndex = -1;

    if (result.demResult) {
        result.demResult->setTreeGroup(treeGroup);
        addedIndex = layers_.add(result.demResult);
    }

    std::shared_ptr<RasterLayer> layer = result.rasterResult;
    if (!layer && !result.image.isNull()) {
        layer = std::make_shared<RasterLayer>(source->name() + suffix, QString(),
                                              QVector<RasterBand>{}, result.image);
        layer->setRenderDescription(result.message);
    }

    if (layer) {
        layer->setTreeGroup(treeGroup);
        addedIndex = layers_.add(layer);
    }

    if (addedIndex >= 0 || layer || result.demResult) {
        refreshLayerTree();
        if (addedIndex >= 0) {
            revealLayerInTree(addedIndex);
        }
    }

    updateActionStates();
}

} // namespace rs

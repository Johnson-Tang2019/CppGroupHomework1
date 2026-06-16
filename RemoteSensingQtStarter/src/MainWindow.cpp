    // ===== 主窗口界面与交互逻辑 (Group3 - UI & Main Interaction) =====
    #include "rs/MainWindow.h"
    #include "rs/Algorithms.h"
    #include "rs/RasterIO.h"
    #include "rs/RasterRenderDialog.h"
    #include "rs/Scene3DWidget.h"
    #include "rs/Panorama360Widget.h"
    #include "rs/ExtendedAlgorithms.h"
    #include "rs/RemoteSensingIndices.h"
    #include "rs/SettingsDialog.h"
    #include "rs/AppTheme.h"
    #include "rs/Translation.h"

    #include <QApplication>
    #include <QBuffer>
    #include <QDialog>
    #include <QDir>
    #include <QFutureWatcher>
    #include <QHBoxLayout>
    #include <QJsonArray>
    #include <QJsonDocument>
    #include <QJsonObject>
    #include <QImageReader>
    #include <QLineEdit>
    #include <QNetworkAccessManager>
    #include <QNetworkReply>
    #include <QNetworkRequest>
    #include <QMouseEvent>
    #include <QProgressDialog>
    #include <QPushButton>
    #include <QRegularExpression>
    #include <QSettings>
    #include <QStatusBar>
    #include <QUrl>
    #include <QWheelEvent>
    #include <QTreeWidgetItemIterator>
    #include <QtConcurrent/QtConcurrent>
    #include <cmath>
    #include <functional>
#ifdef RS_WITH_GDAL
    #include <ogr_spatialref.h>
#endif

    namespace rs {
    namespace {

    // 自定义角色，用于在 QTreeWidgetItem 中存储额外数据
    constexpr int kLayerIndexRole = Qt::UserRole + 1; // 存储图层在 LayerManager 中的索引
    constexpr int kBandIndexRole = Qt::UserRole + 2;  // 存储波段索引（用于波段子节点）
    constexpr int kNodeKindRole = Qt::UserRole + 3;   // 存储节点类型（文件夹/图层/波段）
    constexpr int kFolderKeyRole = Qt::UserRole + 4;  // 文件夹稳定键（语言切换时不变）

    // 图层树节点的类型枚举
    enum class NodeKind {
        Folder, // 文件夹节点（如"源数据/遥感影像"），不可选中
        Layer,  // 图层节点，可选中/勾选
        Band    // 波段子节点，仅信息展示
    };

    struct LoadedMeshData {
        QVector<QVector3D> vertices;
        QVector<Face> faces;
    };

    LoadedMeshData loadMeshFromPly(const QString &path);

    bool hasGeoreference(const RasterLayer &raster) {
        const auto gt = raster.geoTransform();
        constexpr double eps = 1e-12;
        const bool hasDefaultGt = std::abs(gt[0] - 0.0) < eps && std::abs(gt[1] - 1.0) < eps &&
                                  std::abs(gt[2] - 0.0) < eps && std::abs(gt[3] - 0.0) < eps &&
                                  std::abs(gt[4] - 0.0) < eps && std::abs(gt[5] + 1.0) < eps;
        return !raster.projection().trimmed().isEmpty() || !hasDefaultGt;
    }

    QString safeFileBaseName(QString name) {
        static const QRegularExpression invalidChars(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
        name.replace(invalidChars, QStringLiteral("_"));
        name = name.trimmed();
        return name.isEmpty() ? QStringLiteral("layer") : name;
    }

    QVector<QVector3D> loadPointCloudPoints(const QString &path) {
        const QFileInfo info(path);
        const QString ext = info.suffix().toLower();
        QVector<QVector3D> points;

        if (ext == QStringLiteral("xyz") || ext == QStringLiteral("txt") || ext == QStringLiteral("csv")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("无法打开点云文件");
            }
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                    continue;
                }
                line.replace(QLatin1Char(','), QLatin1Char(' '));
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() < 3) {
                    continue;
                }
                bool xOk = false;
                bool yOk = false;
                bool zOk = false;
                const float x = parts[0].toFloat(&xOk);
                const float y = parts[1].toFloat(&yOk);
                const float z = parts[2].toFloat(&zOk);
                if (xOk && yOk && zOk) {
                    points.append(QVector3D(x, y, z));
                }
            }
        } else if (ext == QStringLiteral("ply")) {
            const LoadedMeshData mesh = loadMeshFromPly(path);
            points = mesh.vertices;
        } else if (ext == QStringLiteral("las")) {
            QFile lasFile(path);
            if (!lasFile.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("无法打开 LAS 文件");
            }
            const QByteArray lasData = lasFile.readAll();
            if (lasData.size() < 227) {
                throw std::runtime_error("LAS 文件头不完整");
            }
            const unsigned char *hdr = reinterpret_cast<const unsigned char *>(lasData.constData());
            if (hdr[0] != 'L' || hdr[1] != 'A' || hdr[2] != 'S' || hdr[3] != 'F') {
                throw std::runtime_error("无效的 LAS 签名");
            }
            const quint32 offset = *reinterpret_cast<const quint32 *>(hdr + 96);
            const quint16 recLen = *reinterpret_cast<const quint16 *>(hdr + 105);
            const quint32 ptCount = *reinterpret_cast<const quint32 *>(hdr + 107);
            const double xScale = *reinterpret_cast<const double *>(hdr + 131);
            const double yScale = *reinterpret_cast<const double *>(hdr + 139);
            const double zScale = *reinterpret_cast<const double *>(hdr + 147);
            const double xOff = *reinterpret_cast<const double *>(hdr + 155);
            const double yOff = *reinterpret_cast<const double *>(hdr + 163);
            const double zOff = *reinterpret_cast<const double *>(hdr + 171);
            if (recLen < 12 || static_cast<quint64>(offset) >= static_cast<quint64>(lasData.size())) {
                throw std::runtime_error("LAS 记录信息无效");
            }
            quint64 totalPoints = ptCount;
            if (totalPoints == 0 && lasData.size() >= 255) {
                totalPoints = *reinterpret_cast<const quint64 *>(hdr + 247);
            }
            quint64 n = std::min(totalPoints, (static_cast<quint64>(lasData.size()) - offset) / recLen);
            n = std::min<quint64>(n, 10000000);
            points.reserve(static_cast<int>(n));
            for (quint64 i = 0; i < n; ++i) {
                const char *rec = lasData.constData() + offset + i * recLen;
                const qint32 ix = *reinterpret_cast<const qint32 *>(rec);
                const qint32 iy = *reinterpret_cast<const qint32 *>(rec + 4);
                const qint32 iz = *reinterpret_cast<const qint32 *>(rec + 8);
                points.append(QVector3D(static_cast<float>(ix * xScale + xOff),
                                        static_cast<float>(iy * yScale + yOff),
                                        static_cast<float>(iz * zScale + zOff)));
            }
        } else {
            throw std::runtime_error("不支持的点云格式");
        }

        if (points.isEmpty()) {
            throw std::runtime_error("未能读取到任何点数据");
        }
        return points;
    }

    LoadedMeshData loadMeshDataSync(const QString &path) {
        const QFileInfo info(path);
        const QString ext = info.suffix().toLower();
        QVector<QVector3D> vertices;
        QVector<Face> faces;

        if (ext == QStringLiteral("obj")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("无法打开 OBJ 文件");
            }
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                    continue;
                }
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.isEmpty()) {
                    continue;
                }
                if (parts[0] == QStringLiteral("v") && parts.size() >= 4) {
                    vertices.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
                } else if (parts[0] == QStringLiteral("f") && parts.size() >= 4) {
                    const auto parseIndex = [](const QString &token) {
                        return token.section(QLatin1Char('/'), 0, 0).toInt() - 1;
                    };
                    const int i0 = parseIndex(parts[1]);
                    for (int t = 2; t < parts.size() - 1; ++t) {
                        faces.append(Face{i0, parseIndex(parts[t]), parseIndex(parts[t + 1])});
                    }
                }
            }
        } else if (ext == QStringLiteral("ply")) {
            return loadMeshFromPly(path);
        } else {
            throw std::runtime_error("不支持的 Mesh 格式");
        }

        if (vertices.isEmpty()) {
            throw std::runtime_error("未能读取到任何顶点数据");
        }
        return {vertices, faces};
    }

    QImage loadPanoramaImageSync(const QString &path, QString *detail = nullptr) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setDecideFormatFromContent(true);
        QImage image = reader.read();
        if (detail) {
            *detail = QStringLiteral("Qt ImageReader");
        }
        if (!image.isNull()) {
            return image;
        }

        const auto raster = io::loadRasterDataset(path);
        if (raster && raster->bandCount() >= 3) {
            if (detail) {
                *detail = QStringLiteral("GDAL RGB fallback");
            }
            return io::renderRgbComposite(*raster, 0, 1, 2);
        }
        if (raster && raster->bandCount() >= 1) {
            if (detail) {
                *detail = QStringLiteral("GDAL gray fallback");
            }
            return io::renderSingleBandGray(*raster, 0);
        }
        return {};
    }

#ifdef RS_WITH_GDAL
    bool tryProjectToLonLat(const RasterLayer &raster, double x, double y, double &lon, double &lat) {
        const QString wkt = raster.projection().trimmed();
        if (wkt.isEmpty()) {
            return false;
        }

        OGRSpatialReference src;
        if (src.importFromWkt(wkt.toUtf8().data()) != OGRERR_NONE) {
            return false;
        }

        OGRSpatialReference wgs84;
        wgs84.SetWellKnownGeogCS("WGS84");

        OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(&src, &wgs84);
        if (!ct) {
            return false;
        }

        double tx = x;
        double ty = y;
        const int ok = ct->Transform(1, &tx, &ty);
        OCTDestroyCoordinateTransformation(ct);
        if (!ok) {
            return false;
        }

        lon = tx;
        lat = ty;
        return true;
    }
#endif

    class ZoomableGraphicsView final : public QGraphicsView {
    public:
        explicit ZoomableGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
            : QGraphicsView(scene, parent) {}

    protected:
        void wheelEvent(QWheelEvent *event) override {
            if (!event) {
                return;
            }

            const int dy = event->angleDelta().y();
            if (dy == 0) {
                event->ignore();
                return;
            }

            constexpr qreal kMinScale = 0.1;
            constexpr qreal kMaxScale = 50.0;

            const qreal currentScale = transform().m11();
            if (currentScale <= 0.0) {
                event->accept();
                return;
            }

            qreal factor = std::pow(1.0015, static_cast<qreal>(dy));
            qreal nextScale = currentScale * factor;
            if (nextScale < kMinScale) {
                factor = kMinScale / currentScale;
                nextScale = kMinScale;
            } else if (nextScale > kMaxScale) {
                factor = kMaxScale / currentScale;
                nextScale = kMaxScale;
            }

            scale(factor, factor);
            event->accept();
        }
    };

    class DeepSeekChatPanel final : public QWidget {
    public:
        explicit DeepSeekChatPanel(std::function<QString()> contextProvider,
                                QWidget *parent = nullptr)
            : QWidget(parent), contextProvider_(std::move(contextProvider)) {
            auto *layout = new QVBoxLayout(this);
            layout->setContentsMargins(6, 6, 6, 6);

            auto *keyRow = new QHBoxLayout;
            keyRow->addWidget(new QLabel(QStringLiteral("API Key:"), this));
            apiKeyEdit_ = new QLineEdit(this);
            apiKeyEdit_->setEchoMode(QLineEdit::Password);
            apiKeyEdit_->setPlaceholderText(QStringLiteral("sk-... or DEEPSEEK_API_KEY"));
            apiKeyEdit_->setText(QString::fromUtf8(qgetenv("DEEPSEEK_API_KEY")));
            keyRow->addWidget(apiKeyEdit_, 1);
            layout->addLayout(keyRow);

            auto *modelRow = new QHBoxLayout;
            modelRow->addWidget(new QLabel(QStringLiteral("Model:"), this));
            modelEdit_ = new QLineEdit(QStringLiteral("deepseek-chat"), this);
            modelRow->addWidget(modelEdit_, 1);
            contextButton_ = new QPushButton(QStringLiteral("Insert File Info"), this);
            modelRow->addWidget(contextButton_);
            layout->addLayout(modelRow);

            chatEdit_ = new QTextEdit(this);
            chatEdit_->setReadOnly(true);
            chatEdit_->setMinimumHeight(150);
            chatEdit_->setPlaceholderText(QStringLiteral("Ask DeepSeek about the imported files, land-cover clues, C++, Qt, or processing workflow."));
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

        void appendMessage(const QString &speaker, const QString &text) {
            chatEdit_->append(QStringLiteral("<b>%1:</b>").arg(speaker.toHtmlEscaped()));
            chatEdit_->append(text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
            chatEdit_->append(QString());
        }

        void sendPrompt() {
            const QString apiKey = apiKeyEdit_->text().trimmed();
            const QString prompt = inputEdit_->toPlainText().trimmed();
            const QString model = modelEdit_->text().trimmed().isEmpty()
                                    ? QStringLiteral("deepseek-chat")
                                    : modelEdit_->text().trimmed();

            if (apiKey.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("DeepSeek API Key"),
                                    QStringLiteral("Please enter your DeepSeek API key or set DEEPSEEK_API_KEY."));
                return;
            }
            if (prompt.isEmpty()) {
                return;
            }

            inputEdit_->clear();
            appendMessage(QStringLiteral("You"), prompt);
            sendButton_->setEnabled(false);
            sendButton_->setText(QStringLiteral("Sending..."));

            const QString layerContext = currentLayerContext();
            const QString promptWithContext =
                QStringLiteral("当前程序中已导入的数据如下。请只基于这些信息和用户问题回答；如果仅凭元数据无法可靠识别具体地物，请明确说明不确定性，并给出可验证的判断依据。\n\n%1\n\n用户问题：%2")
                    .arg(layerContext, prompt);

            QJsonArray requestMessages = history_;
            requestMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                            {QStringLiteral("content"), promptWithContext}});

            QJsonObject body;
            body.insert(QStringLiteral("model"), model);
            body.insert(QStringLiteral("messages"), requestMessages);
            body.insert(QStringLiteral("temperature"), 0.7);
            body.insert(QStringLiteral("stream"), false);

            QNetworkRequest request(QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

            auto *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
            connect(reply, &QNetworkReply::finished, this, [this, reply, prompt]() {
                handleReply(reply, prompt);
                reply->deleteLater();
            });
        }

        void handleReply(QNetworkReply *reply, const QString &prompt) {
            sendButton_->setEnabled(true);
            sendButton_->setText(QStringLiteral("Send"));

            const QByteArray responseBody = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                appendMessage(QStringLiteral("Error"),
                            reply->errorString() + QStringLiteral("\n") +
                                QString::fromUtf8(responseBody));
                return;
            }

            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                appendMessage(QStringLiteral("Error"),
                            QStringLiteral("Invalid JSON response: %1").arg(parseError.errorString()));
                return;
            }

            const QJsonObject root = doc.object();
            if (root.contains(QStringLiteral("error"))) {
                const QJsonObject error = root.value(QStringLiteral("error")).toObject();
                appendMessage(QStringLiteral("Error"),
                            error.value(QStringLiteral("message"))
                                .toString(QString::fromUtf8(responseBody)));
                return;
            }

            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) {
                appendMessage(QStringLiteral("Error"), QStringLiteral("DeepSeek returned no choices."));
                return;
            }

            const QJsonObject message =
                choices.first().toObject().value(QStringLiteral("message")).toObject();
            const QString answer = message.value(QStringLiteral("content")).toString().trimmed();
            if (answer.isEmpty()) {
                appendMessage(QStringLiteral("Error"), QStringLiteral("DeepSeek returned an empty answer."));
                return;
            }

            history_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                        {QStringLiteral("content"), prompt}});
            history_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                        {QStringLiteral("content"), answer}});
            appendMessage(QStringLiteral("DeepSeek"), answer);
        }

        QLineEdit *apiKeyEdit_{};
        QLineEdit *modelEdit_{};
        QTextEdit *chatEdit_{};
        QTextEdit *inputEdit_{};
        QPushButton *sendButton_{};
        QPushButton *clearButton_{};
        QPushButton *contextButton_{};
        QNetworkAccessManager network_;
        QJsonArray history_;
        std::function<QString()> contextProvider_;
    };

    class VisionChatPanel final : public QWidget {
    public:
        explicit VisionChatPanel(std::function<QString()> contextProvider,
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
            apiKeyEdit_->setPlaceholderText(QStringLiteral("ARK_API_KEY"));
            const QString envKey = QString::fromUtf8(qgetenv("ARK_API_KEY")).trimmed();
            apiKeyEdit_->setText(envKey.isEmpty()
                                     ? QStringLiteral("ark-1add2ab0-f33a-43d6-be15-5b3986fd85ae-f729a")
                                     : envKey);
            keyRow->addWidget(apiKeyEdit_, 1);
            layout->addLayout(keyRow);

            auto *urlRow = new QHBoxLayout;
            urlRow->addWidget(new QLabel(QStringLiteral("接口 URL:"), this));
            apiUrlEdit_ = new QLineEdit(QStringLiteral("https://ark.cn-beijing.volces.com/api/v3/chat/completions"), this);
            urlRow->addWidget(apiUrlEdit_, 1);
            layout->addLayout(urlRow);

            auto *modelRow = new QHBoxLayout;
            modelRow->addWidget(new QLabel(QStringLiteral("模型/Endpoint:"), this));
            modelEdit_ = new QLineEdit(QStringLiteral("ep-20260614191726-976lp"), this);
            modelRow->addWidget(modelEdit_, 1);
            contextButton_ = new QPushButton(QStringLiteral("Insert File Info"), this);
            modelRow->addWidget(contextButton_);
            layout->addLayout(modelRow);

            usageLabel_ = new QLabel(this);
            usageLabel_->setWordWrap(true);
            layout->addWidget(usageLabel_);

            chatEdit_ = new QTextEdit(this);
            chatEdit_->setReadOnly(true);
            chatEdit_->setMinimumHeight(150);
            chatEdit_->setPlaceholderText(QStringLiteral("Ask the domestic vision model to identify land-cover, buildings, roads, water, vegetation, or imported data."));
            layout->addWidget(chatEdit_, 8);

            inputEdit_ = new QTextEdit(this);
            inputEdit_->setMaximumHeight(110);
            inputEdit_->setPlaceholderText(QStringLiteral("Type your message here. Imported layer information and current selected image are attached automatically."));
            layout->addWidget(inputEdit_, 1);

            auto *buttonRow = new QHBoxLayout;
            buttonRow->addStretch(1);
            clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
            sendButton_ = new QPushButton(QStringLiteral("Send"), this);
            buttonRow->addWidget(clearButton_);
            buttonRow->addWidget(sendButton_);
            layout->addLayout(buttonRow);

            resetHistory();
            updateUsageLabel();

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
                             QStringLiteral("You are a remote-sensing vision assistant. Analyze the attached image when present, identify visible land-cover and objects, and clearly separate visual evidence from uncertainty. Answer in Chinese by default.")}}};
        }

        QString currentLayerContext() const {
            if (!contextProvider_) {
                return QStringLiteral("No layer context provider is available.");
            }
            const QString context = contextProvider_().trimmed();
            return context.isEmpty() ? QStringLiteral("No imported layer is available.") : context;
        }

        void appendMessage(const QString &speaker, const QString &text) {
            const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            chatEdit_->append(QStringLiteral("<b>[%1] %2:</b>")
                                  .arg(timestamp.toHtmlEscaped(), speaker.toHtmlEscaped()));
            chatEdit_->append(text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
            chatEdit_->append(QString());
        }

        QString currentImageDataUrl() const {
            if (!imageProvider_) {
                return {};
            }

            QImage image = imageProvider_();
            if (image.isNull()) {
                return {};
            }

            constexpr int kMaxSide = 1280;
            if (image.width() > kMaxSide || image.height() > kMaxSide) {
                image = image.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            image = image.convertToFormat(QImage::Format_RGB888);

            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "JPG", 85)) {
                return {};
            }

            return QStringLiteral("data:image/jpeg;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
        }

        bool usingDefaultVisionService(const QString &apiKey,
                                       const QString &apiUrl,
                                       const QString &model) const {
            Q_UNUSED(apiKey);
            return apiUrl == QStringLiteral("https://ark.cn-beijing.volces.com/api/v3/chat/completions") &&
                   model == QStringLiteral("ep-20260614191726-976lp");
        }

        void updateUsageLabel() {
            if (!usageLabel_) {
                return;
            }

            QSettings settings;
            if (settings.value(QStringLiteral("ai/defaultVisionUnlocked"), false).toBool()) {
                usageLabel_->setText(QStringLiteral("默认国产视觉模型：已永久开通"));
                return;
            }

            const int used = settings.value(QStringLiteral("ai/defaultVisionUseCount"), 0).toInt();
            usageLabel_->setText(QStringLiteral("默认国产视觉模型：免费体验已用 %1 / 5 次").arg(used));
        }

        bool ensureDefaultVisionAccess() {
            QSettings settings;
            if (settings.value(QStringLiteral("ai/defaultVisionUnlocked"), false).toBool()) {
                updateUsageLabel();
                return true;
            }

            const int used = settings.value(QStringLiteral("ai/defaultVisionUseCount"), 0).toInt();
            if (used < 5) {
                return true;
            }

            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("开通永久 AI 服务"));
            dialog.setModal(true);
            dialog.resize(520, 720);

            auto *layout = new QVBoxLayout(&dialog);
            auto *title = new QLabel(QStringLiteral("默认国产视觉模型免费体验 5 次已用完"), &dialog);
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
            layout->addWidget(title);

            auto *desc = new QLabel(QStringLiteral("请使用微信扫码支付 0.01 元。支付完成后点击下方按钮，即可在本机永久开通后续 AI 服务。"), &dialog);
            desc->setWordWrap(true);
            desc->setAlignment(Qt::AlignCenter);
            layout->addWidget(desc);

            auto *qrLabel = new QLabel(&dialog);
            qrLabel->setAlignment(Qt::AlignCenter);
            const QPixmap qr(QStringLiteral(":/wechat_ai_unlock.jpg"));
            if (!qr.isNull()) {
                qrLabel->setPixmap(qr.scaled(360, 520, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else {
                qrLabel->setText(QStringLiteral("收款码资源加载失败"));
            }
            layout->addWidget(qrLabel, 1);

            auto *hint = new QLabel(QStringLiteral("说明：当前版本采用本地确认方式保存开通状态。"), &dialog);
            hint->setWordWrap(true);
            hint->setAlignment(Qt::AlignCenter);
            layout->addWidget(hint);

            auto *buttonRow = new QHBoxLayout;
            auto *cancelButton = new QPushButton(QStringLiteral("稍后再说"), &dialog);
            auto *unlockButton = new QPushButton(QStringLiteral("我已支付，永久开通"), &dialog);
            unlockButton->setDefault(true);
            buttonRow->addStretch(1);
            buttonRow->addWidget(cancelButton);
            buttonRow->addWidget(unlockButton);
            layout->addLayout(buttonRow);

            connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
            connect(unlockButton, &QPushButton::clicked, &dialog, [&]() {
                settings.setValue(QStringLiteral("ai/defaultVisionUnlocked"), true);
                settings.sync();
                updateUsageLabel();
                dialog.accept();
            });

            return dialog.exec() == QDialog::Accepted;
        }

        void recordDefaultVisionUseIfNeeded(const QString &apiKey,
                                            const QString &apiUrl,
                                            const QString &model) {
            if (!usingDefaultVisionService(apiKey, apiUrl, model)) {
                return;
            }

            QSettings settings;
            if (settings.value(QStringLiteral("ai/defaultVisionUnlocked"), false).toBool()) {
                return;
            }

            const int used = settings.value(QStringLiteral("ai/defaultVisionUseCount"), 0).toInt();
            settings.setValue(QStringLiteral("ai/defaultVisionUseCount"), used + 1);
            settings.sync();
            updateUsageLabel();
        }

        void sendPrompt() {
            const QString apiKey = apiKeyEdit_->text().trimmed();
            const QString apiUrl = apiUrlEdit_->text().trimmed().isEmpty()
                                       ? QStringLiteral("https://ark.cn-beijing.volces.com/api/v3/chat/completions")
                                       : apiUrlEdit_->text().trimmed();
            const QString prompt = inputEdit_->toPlainText().trimmed();
            const QString model = modelEdit_->text().trimmed().isEmpty()
                                      ? QStringLiteral("ep-20260614191726-976lp")
                                      : modelEdit_->text().trimmed();

            if (apiKey.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Vision API Key"),
                                     QStringLiteral("Please enter your Ark API key or set ARK_API_KEY."));
                return;
            }
            if (prompt.isEmpty()) {
                return;
            }
            if (usingDefaultVisionService(apiKey, apiUrl, model) && !ensureDefaultVisionAccess()) {
                appendMessage(QStringLiteral("AI Assistant"), QStringLiteral("已取消发送。开通后可继续使用默认国产视觉模型。"));
                return;
            }

            inputEdit_->clear();
            appendMessage(QStringLiteral("You"), prompt + QStringLiteral("\n[Current selected image attached when available]"));
            sendButton_->setEnabled(false);
            sendButton_->setText(QStringLiteral("Sending..."));

            const QString layerContext = currentLayerContext();
            const QString promptWithContext =
                QStringLiteral("当前程序中已导入的数据如下。请优先观察随请求附带的当前选中影像，识别可见地物，例如建筑、道路、水体、植被、裸地、阴影等；同时结合元数据回答。不要把仅由元数据推测的内容说成确定事实。\n\n%1\n\n用户问题：%2")
                    .arg(layerContext, prompt);

            QJsonValue userContent = promptWithContext;
            const QString imageDataUrl = currentImageDataUrl();
            if (!imageDataUrl.isEmpty()) {
                QJsonArray contentParts;
                contentParts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                {QStringLiteral("text"), promptWithContext}});
                contentParts.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), imageDataUrl}}}});
                userContent = contentParts;
            }

            QJsonArray requestMessages = history_;
            requestMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                               {QStringLiteral("content"), userContent}});

            QJsonObject body;
            body.insert(QStringLiteral("model"), model);
            body.insert(QStringLiteral("messages"), requestMessages);
            body.insert(QStringLiteral("temperature"), 0.3);
            body.insert(QStringLiteral("stream"), false);

            QNetworkRequest request{QUrl(apiUrl)};
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

            recordDefaultVisionUseIfNeeded(apiKey, apiUrl, model);
            auto *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
            connect(reply, &QNetworkReply::finished, this, [this, reply, prompt]() {
                handleReply(reply, prompt);
                reply->deleteLater();
            });
        }

        void handleReply(QNetworkReply *reply, const QString &prompt) {
            sendButton_->setEnabled(true);
            sendButton_->setText(QStringLiteral("Send"));

            const QByteArray responseBody = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                appendMessage(QStringLiteral("Vision Error"),
                              reply->errorString() + QStringLiteral("\n") + QString::fromUtf8(responseBody));
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
                              error.value(QStringLiteral("message")).toString(QString::fromUtf8(responseBody)));
                return;
            }

            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) {
                appendMessage(QStringLiteral("Vision Error"), QStringLiteral("Vision model returned no choices."));
                return;
            }

            const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
            const QString answer = message.value(QStringLiteral("content")).toString().trimmed();
            if (answer.isEmpty()) {
                appendMessage(QStringLiteral("Vision Error"), QStringLiteral("Vision model returned an empty answer."));
                return;
            }

            history_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                        {QStringLiteral("content"), prompt}});
            history_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                        {QStringLiteral("content"), answer}});
            appendMessage(QStringLiteral("国产视觉"), answer);
        }

        QLineEdit *apiKeyEdit_{};
        QLineEdit *apiUrlEdit_{};
        QLineEdit *modelEdit_{};
        QLabel *usageLabel_{};
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

    QString localizedTreeGroupName(const QString &group) {
        if (group.isEmpty()) {
            return group;
        }
        static const QHash<QString, QString> keyByGroup = {
            {QStringLiteral("DEM 重建"), QStringLiteral("tree.group.dem_rebuild")},
            {QStringLiteral("正射影像校正"), QStringLiteral("tree.group.orthorectify")},
            {QStringLiteral("灰度直方图"), QStringLiteral("tree.group.histogram")},
            {QStringLiteral("混淆矩阵精度评价"), QStringLiteral("tree.group.confusion_matrix")},
            {QStringLiteral("多时相指数对比"), QStringLiteral("tree.group.index_temporal")},
        };
        const auto it = keyByGroup.constFind(group);
        if (it != keyByGroup.constEnd()) {
            return Translation::instance().tr(it.value());
        }
        return group;
    }

    QString layerTypeLabel(DataType type) {
        const auto &t = Translation::instance();
        switch (type) {
        case DataType::Raster:
            return t.tr(QStringLiteral("type.raster"));
        case DataType::PointCloud:
            return t.tr(QStringLiteral("type.pointcloud"));
        case DataType::Mesh:
            return t.tr(QStringLiteral("type.mesh"));
        case DataType::Dem:
            return t.tr(QStringLiteral("type.dem"));
        case DataType::Panorama360:
            return t.tr(QStringLiteral("type.panorama360"));
        case DataType::Result:
            return t.tr(QStringLiteral("type.result"));
        }
        return QStringLiteral("Unknown");
    }

    // 生成节点的唯一键（从根到当前节点的路径字符串）
    QString itemKey(const QTreeWidgetItem *item) {
        QStringList parts;          // 用于存储从根到当前节点的各层名称
        const auto *current = item; // 从当前节点开始向上遍历
        while (current) {           // 一直遍历到根节点（parent 为 nullptr）
            const QString folderKey = current->data(0, kFolderKeyRole).toString();
            parts.prepend(folderKey.isEmpty() ? current->text(0) : folderKey);
            current = current->parent(); // 向上移动到父节点
        }
        return parts.join(QLatin1Char('/'));
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
    QTreeWidgetItem *ensureChildFolder(QTreeWidgetItem *parent, const QString &folderKey,
                                       const QString &displayName) {
        for (int i = 0; i < parent->childCount(); ++i) {
            auto *child = parent->child(i);
            if (child->data(0, kFolderKeyRole).toString() == folderKey) {
                child->setText(0, displayName);
                return child;
            }
        }
        auto *folder = new QTreeWidgetItem(parent);
        folder->setText(0, displayName);
        folder->setData(0, kFolderKeyRole, folderKey);
        folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));
        folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
        return folder;
    }

    // 在顶层节点中查找或创建文件夹
    QTreeWidgetItem *ensureTopFolder(QTreeWidget *tree, const QString &folderKey,
                                     const QString &displayName) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *top = tree->topLevelItem(i);
            if (top->data(0, kFolderKeyRole).toString() == folderKey) {
                top->setText(0, displayName);
                return top;
            }
        }
        auto *folder = new QTreeWidgetItem(tree);
        folder->setText(0, displayName);
        folder->setData(0, kFolderKeyRole, folderKey);
        folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));
        folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
        return folder;
    }

    qint32 readInt32LeBytes(const char *data) {
        const auto *b = reinterpret_cast<const unsigned char *>(data);
        const quint32 value = (static_cast<quint32>(b[0])) | (static_cast<quint32>(b[1]) << 8) |
                            (static_cast<quint32>(b[2]) << 16) | (static_cast<quint32>(b[3]) << 24);
        return static_cast<qint32>(value);
    }

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
        createMenus();
        createUi();
        setupSettingsButton();
        connect(&Translation::instance(), &Translation::languageChanged, this, &MainWindow::retranslateUi);
        connect(&AppTheme::instance(), &AppTheme::themeChanged, this, &MainWindow::applyThemeStyles);
        retranslateUi();
        appendLog(QStringLiteral("Starter 已启动：当前版本提供 GDAL "
                                "多波段、参数化算法、DEM/正射流程的工程骨架。"));
        restoreLastSession();
        updateActionStates();
    }

    // 构建菜单栏：数据、影像处理、摄影测量/三维
    void MainWindow::createMenus() {
        translatableMenus_.clear();
        translatableActions_.clear();

        const auto regTopMenu = [this](const QString &key) {
            auto *m = menuBar()->addMenu(QString());
            m->setProperty("trKey", key);
            translatableMenus_.append(m);
            return m;
        };
        const auto regSubMenu = [this](QMenu *parent, const QString &key) {
            auto *m = parent->addMenu(QString());
            m->setProperty("trKey", key);
            translatableMenus_.append(m);
            return m;
        };
        const auto regAction = [this](QMenu *menu, const QString &key) {
            auto *a = menu->addAction(QString());
            a->setProperty("trKey", key);
            translatableActions_.append(a);
            return a;
        };

        dataMenu_ = regTopMenu(QStringLiteral("menu.data"));
        loadRasterAction_ = regAction(dataMenu_, QStringLiteral("action.load_raster"));
        connect(loadRasterAction_, &QAction::triggered, this, &MainWindow::openRasterDatasets);
        loadPointCloudAction_ = regAction(dataMenu_, QStringLiteral("action.load_pointcloud"));
        connect(loadPointCloudAction_, &QAction::triggered, this, &MainWindow::openPointCloud);
        loadMeshAction_ = regAction(dataMenu_, QStringLiteral("action.load_mesh"));
        connect(loadMeshAction_, &QAction::triggered, this, &MainWindow::openMesh);
        loadDemAction_ = regAction(dataMenu_, QStringLiteral("action.load_dem"));
        connect(loadDemAction_, &QAction::triggered, this, &MainWindow::openDem);
        loadPanoramaAction_ = regAction(dataMenu_, QStringLiteral("action.load_panorama360"));
        connect(loadPanoramaAction_, &QAction::triggered, this, &MainWindow::openPanorama360);
        dataMenu_->addSeparator();
        deleteLayerAction_ = regAction(dataMenu_, QStringLiteral("action.delete_layer"));
        connect(deleteLayerAction_, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
        clearProjectAction_ = regAction(dataMenu_, QStringLiteral("action.clear_project"));
        connect(clearProjectAction_, &QAction::triggered, this, &MainWindow::clearProject);

        rasterMenu_ = regTopMenu(QStringLiteral("menu.raster"));
        auto *bandMenu = regSubMenu(rasterMenu_, QStringLiteral("menu.raster.band"));
        renderAction_ = regAction(bandMenu, QStringLiteral("action.render"));
        connect(renderAction_, &QAction::triggered, this, &MainWindow::configureRasterRendering);

        auto *statMenu = regSubMenu(rasterMenu_, QStringLiteral("menu.raster.stat"));
        histogramAction_ = regAction(statMenu, QStringLiteral("action.histogram"));
        connect(histogramAction_, &QAction::triggered, this, &MainWindow::runHistogram);

        auto *enhanceMenu = regSubMenu(rasterMenu_, QStringLiteral("menu.raster.enhance"));
        equalizeAction_ = regAction(enhanceMenu, QStringLiteral("action.equalize"));
        connect(equalizeAction_, &QAction::triggered, this, &MainWindow::runHistogramEqualization);
        connect(regAction(enhanceMenu, QStringLiteral("action.stretch")), &QAction::triggered, this,
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
        connect(regAction(enhanceMenu, QStringLiteral("action.clahe")), &QAction::triggered, this,
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
        connect(regAction(enhanceMenu, QStringLiteral("action.gaussian_filter")), &QAction::triggered, this,
                [this]() {
                    DenoiseFilterAlgorithm algo;
                    ProcessingContext ctx;
                    ctx.bandIndex = selectedBandIndex();
                    ctx.parameters[QStringLiteral("filterType")] = QStringLiteral("gaussian");
                    executeRasterAlgorithm(algo, ctx);
                });
        connect(regAction(enhanceMenu, QStringLiteral("action.median_filter")), &QAction::triggered, this,
                [this]() {
                    DenoiseFilterAlgorithm algo;
                    ProcessingContext ctx;
                    ctx.bandIndex = selectedBandIndex();
                    ctx.parameters[QStringLiteral("filterType")] = QStringLiteral("median");
                    executeRasterAlgorithm(algo, ctx);
                });
        connect(regAction(enhanceMenu, QStringLiteral("action.bilateral_filter")), &QAction::triggered, this,
                [this]() {
                    DenoiseFilterAlgorithm algo;
                    ProcessingContext ctx;
                    ctx.bandIndex = selectedBandIndex();
                    ctx.parameters[QStringLiteral("filterType")] = QStringLiteral("bilateral");
                    executeRasterAlgorithm(algo, ctx);
                });
        connect(regAction(enhanceMenu, QStringLiteral("action.unsharp")), &QAction::triggered, this,
                [this]() {
                    SharpenEnhancementAlgorithm algo;
                    ProcessingContext ctx;
                    ctx.bandIndex = selectedBandIndex();
                    ctx.parameters[QStringLiteral("method")] = QStringLiteral("unsharp");
                    executeRasterAlgorithm(algo, ctx);
                });
        connect(regAction(enhanceMenu, QStringLiteral("action.laplacian_sharpen")), &QAction::triggered, this,
                [this]() {
                    SharpenEnhancementAlgorithm algo;
                    ProcessingContext ctx;
                    ctx.bandIndex = selectedBandIndex();
                    ctx.parameters[QStringLiteral("method")] = QStringLiteral("laplacian");
                    executeRasterAlgorithm(algo, ctx);
                });

        auto *featureMenu = regSubMenu(rasterMenu_, QStringLiteral("menu.raster.feature"));
        featureAction_ = regAction(featureMenu, QStringLiteral("action.feature_extract"));
        connect(featureAction_, &QAction::triggered, this, &MainWindow::runFeatureExtraction);
        connect(regAction(featureMenu, QStringLiteral("action.canny")), &QAction::triggered, this,
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

        auto *classMenu = regSubMenu(rasterMenu_, QStringLiteral("menu.raster.classify"));
        connect(regAction(classMenu, QStringLiteral("action.kmeans")), &QAction::triggered, this,
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
        connect(regAction(classMenu, QStringLiteral("action.svm")), &QAction::triggered, this,
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
        connect(regAction(classMenu, QStringLiteral("action.contour")), &QAction::triggered, this, [this]() {
            ContourDetectionAlgorithm algo;
            executeRasterAlgorithm(algo);
        });
        connect(regAction(classMenu, QStringLiteral("action.connected_components")), &QAction::triggered, this,
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
        connect(regAction(classMenu, QStringLiteral("action.confusion_matrix")), &QAction::triggered, this,
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

        indexMenu_ = regTopMenu(QStringLiteral("menu.index"));
        connect(regAction(indexMenu_, QStringLiteral("action.index_calc")), &QAction::triggered, this,
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
        connect(regAction(indexMenu_, QStringLiteral("action.index_temporal")), &QAction::triggered, this,
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
        connect(regAction(indexMenu_, QStringLiteral("action.index_export_csv")), &QAction::triggered, this,
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

        photogrammetryMenu_ = regTopMenu(QStringLiteral("menu.photogrammetry"));
        demAction_ = regAction(photogrammetryMenu_, QStringLiteral("action.dem_rebuild"));
        connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
        orthoAction_ = regAction(photogrammetryMenu_, QStringLiteral("action.orthorectify"));
        connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);
        demTextureAction_ = regAction(photogrammetryMenu_, QStringLiteral("action.dem_texture"));
        connect(demTextureAction_, &QAction::triggered, this, &MainWindow::runDemTextureMapping);

        pcMenu_ = regTopMenu(QStringLiteral("menu.pointcloud"));
        downsampleAction_ = regAction(pcMenu_, QStringLiteral("action.voxel_downsample"));
        connect(downsampleAction_, &QAction::triggered, this, &MainWindow::runPointCloudDownsample);
        filterAction_ = regAction(pcMenu_, QStringLiteral("action.statistical_filter"));
        connect(filterAction_, &QAction::triggered, this, &MainWindow::runPointCloudFilter);
        pcToDemAction_ = regAction(pcMenu_, QStringLiteral("action.pointcloud_to_dem"));
        connect(pcToDemAction_, &QAction::triggered, this, &MainWindow::runPointCloudToDem);
        exportPlyAction_ = regAction(pcMenu_, QStringLiteral("action.export_ply"));
        connect(exportPlyAction_, &QAction::triggered, this, &MainWindow::exportPly);
    }

    void MainWindow::setupSettingsButton() {
        if (settingsButton_) {
            return;
        }

        auto *menuWrap = new QWidget(this);
        menuWrap->setObjectName(QStringLiteral("menuWrap"));
        auto *row = new QHBoxLayout(menuWrap);
        row->setContentsMargins(0, 0, 12, 0);
        row->setSpacing(8);

        auto *bar = menuBar();
        bar->setParent(menuWrap);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        row->addWidget(bar, 1);

        settingsButton_ = new QPushButton(menuWrap);
        settingsButton_->setObjectName(QStringLiteral("settingsButton"));
        settingsButton_->setFlat(true);
        settingsButton_->setCursor(Qt::PointingHandCursor);
        settingsButton_->setMinimumWidth(72);
        connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::openSettings);
        row->addWidget(settingsButton_, 0, Qt::AlignRight | Qt::AlignVCenter);

        setMenuWidget(menuWrap);
    }

    // 构建界面布局：左侧图层树 + 右侧影像/三维标签页 + 底部日志面板
    void MainWindow::createUi() {
        // 主分割器：水平方向，将窗口分为左侧（图层树）和右侧（影像+日志）
        auto *root = new QSplitter(Qt::Horizontal, this);
        // ---- 左侧：图层树 ----
        layerTree_ = new QTreeWidget(root);                                 // 创建图层树控件
        layerTree_->setHeaderLabel(Translation::instance().tr(QStringLiteral("layer_tree")));
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

        imageView_ = new ZoomableGraphicsView(imageScene_, tabs_); // 创建图形视图（显示场景内容）
        imageView_->setDragMode(QGraphicsView::ScrollHandDrag); // 设置拖拽模式：手型抓手平移
        imageView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse); // 缩放时以鼠标位置为中心
        imageView_->setMouseTracking(true);
        if (imageView_->viewport()) {
            imageView_->viewport()->setMouseTracking(true);
            imageView_->viewport()->installEventFilter(this);
        }

        // 三维场景页：QOpenGLWidget 点云预览
        scene3DWidget_ = new Scene3DWidget(tabs_);
        panorama360Widget_ = new Panorama360Widget(tabs_);

        tabs_->addTab(imageView_, QString());
        tabs_->addTab(scene3DWidget_, QString());
        tabs_->addTab(panorama360Widget_, QString());

        bottomTabs_ = new QTabWidget(right);
        bottomTabs_->setMinimumHeight(180);

        logEdit_ = new QTextEdit(bottomTabs_);
        logEdit_->setReadOnly(true);
        bottomTabs_->addTab(logEdit_, QString());

        auto *aiPanel = new VisionChatPanel([this]() {
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
                case DataType::Panorama360:
                    typeName = QStringLiteral("360 panorama");
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
                } else if (const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer)) {
                    const QImage &image = pano->image();
                    if (!image.isNull()) {
                        lines << QStringLiteral("- Panorama image: %1 x %2").arg(image.width()).arg(image.height());
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
        }, [this]() -> QImage {
            if (const auto raster = selectedRaster()) {
                const QImage &display = raster->currentDisplayImage();
                if (!display.isNull()) {
                    return display;
                }
            }

            if (!imageScene_ || imageScene_->items().isEmpty()) {
                return {};
            }

            const QRectF bounds = imageScene_->itemsBoundingRect();
            if (!bounds.isValid() || bounds.isEmpty()) {
                return {};
            }

            QImage snapshot(bounds.size().toSize(), QImage::Format_ARGB32);
            snapshot.fill(Qt::transparent);
            QPainter painter(&snapshot);
            imageScene_->render(&painter, QRectF(snapshot.rect()), bounds);
            return snapshot;
        }, bottomTabs_);
        bottomTabs_->addTab(aiPanel, QString());

        // 设置分割器拉伸比例（控件随窗口缩放时的比例分配）
        root->setStretchFactor(0, 1);  // 第0个（图层树）：拉伸因子 = 1
        root->setStretchFactor(1, 5);  // 第1个（右侧区域）：拉伸因子 = 5
        right->setStretchFactor(0, 5); // 第0个（影像标签页）：拉伸因子 = 5
        right->setStretchFactor(1, 2); // 第1个（日志面板）：拉伸因子 = 2
        right->setSizes({620, 300});

        setCentralWidget(root); // 将分割器设为窗口的中心控件（填满整个窗口）

        coordLabel_ = new QLabel(QStringLiteral(""), this);
        coordLabel_->setMinimumWidth(260);
        statusBar()->addPermanentWidget(coordLabel_);

        applyThemeStyles();
    }

    void MainWindow::applyThemeStyles() {
        setStyleSheet(AppTheme::instance().mainWindowStyleSheet());
    }

    // 打开文件对话框选择遥感影像，使用 GDAL 读取并加载到图层管理器
    void MainWindow::openRasterDatasets() {
        const auto &t = Translation::instance();
        const QStringList paths = QFileDialog::getOpenFileNames(
            this,
            t.tr(QStringLiteral("dialog.load_raster")),
            QString(),
            QStringLiteral("Remote sensing rasters (*.tif *.tiff *.img *.dat *.jp2 *.jpg *.jpeg *.png "
                        "*.bmp);;All Files (*.*)"));
        if (paths.isEmpty()) {
            return;
        }

        for (const QString &path : paths) {
            const QFileInfo info(path);
            try {
                auto raster = rs::io::loadRasterDataset(path);
                if (raster) {
                    const QString ext = info.suffix().toLower();
                    if ((ext == QStringLiteral("tif") || ext == QStringLiteral("tiff")) &&
                        hasGeoreference(*raster)) {
                        const auto gt = raster->geoTransform();
                        QString body = t.tr(QStringLiteral("geo.detected"));
                        body += QStringLiteral("\n\n");
                        body += t.tr(QStringLiteral("geo.file")) + QStringLiteral(" ") + info.fileName();
                        body += QStringLiteral("\n\nGeoTransform:\n[%1, %2, %3, %4, %5, %6]")
                                    .arg(gt[0])
                                    .arg(gt[1])
                                    .arg(gt[2])
                                    .arg(gt[3])
                                    .arg(gt[4])
                                    .arg(gt[5]);
                        const QString projection = raster->projection().trimmed();
                        if (!projection.isEmpty()) {
                            body += QStringLiteral("\n\n") + t.tr(QStringLiteral("geo.projection_snippet"));
                            body += QStringLiteral("\n") + projection.left(480);
                        }
                        QMessageBox::information(this, t.tr(QStringLiteral("geo.title")), body);
                    }
                    layers_.add(raster);
                    appendLog(QStringLiteral("已加载影像：%1（%2 波段，%3x%4）")
                                .arg(info.fileName())
                                .arg(raster->bandCount())
                                .arg(raster->bandCount() > 0 ? raster->band(0).width : 0)
                                .arg(raster->bandCount() > 0 ? raster->band(0).height : 0));
                }
            } catch (const std::exception &e) {
                appendLog(QStringLiteral("加载失败 [%1]：%2")
                            .arg(info.fileName(), QString::fromUtf8(e.what())));
            }
        }
        refreshLayerTree();
        updateActionStates();
    }

    // 加载点云：支持 PLY、XYZ、LAS 格式
    void MainWindow::openPointCloud() {
        const auto &t = Translation::instance();
        const QString path = QFileDialog::getOpenFileName(
            this, t.tr(QStringLiteral("dialog.load_pointcloud")), QString(),
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
    // 在后台线程解析 Mesh 文件（OBJ/PLY），避免阻塞 UI
    // 在后台线程解析 Mesh 文件（OBJ/PLY），避免阻塞 UI
    void MainWindow::openMesh() {
        const auto &t = Translation::instance();
        const QString path = QFileDialog::getOpenFileName(
            this, t.tr(QStringLiteral("dialog.load_mesh")), QString(),
            QStringLiteral("Mesh (*.obj *.ply);;All Files (*.*)"));
        if (path.isEmpty())
            return;

        const QFileInfo info(path);
        const QString ext = info.suffix().toLower();

        // 显示进度对话框，让用户知道正在加载
        auto *dialog = new QProgressDialog(
            QStringLiteral("正在加载 Mesh：%1 ...").arg(info.fileName()),
            QString(), 0, 0, this);
        dialog->setWindowTitle(QStringLiteral("加载中"));
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setCancelButton(nullptr);
        dialog->show();
        // 粉色风格，匹配三维场景配色
        dialog->setStyleSheet(QStringLiteral("QProgressDialog{background-color:#FFF0F5;}QLabel{color:#8B3A62;font:bold;padding:8px;}QProgressBar{border:1px solid #DDA0DD;border-radius:4px;background-color:#FFE4E9;text-align:center;height:20px;}QProgressBar::chunk{background-color:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0#FFB6C1,stop:0.5#FF69B4,stop:1#DB7093);border-radius:3px;}"));

        // 后台解析文件（不能在后台线程操作 Qt UI）
        auto *watcher = new QFutureWatcher<LoadedMeshData>(this);
        watcher->setFuture(QtConcurrent::run([path, ext]() -> LoadedMeshData {
            QVector<QVector3D> vertices;
            QVector<Face> faces;

            if (ext == QStringLiteral("obj")) {
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

            return {vertices, faces};
        }));

        // 后台解析完成后，切回主线程更新界面
        connect(watcher, &QFutureWatcher<LoadedMeshData>::finished, this,
                [this, watcher, info, path, dialog]() {
            dialog->accept();

            try {
                const LoadedMeshData mesh = watcher->result();
                const auto &vertices = mesh.vertices;
                const auto &faces = mesh.faces;

                auto layer = std::make_shared<MeshLayer>(info.fileName(), path, vertices, faces);
                layers_.add(layer);
                scene3DWidget_->setMeshPreview(vertices, faces);
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
                    appendLog(QStringLiteral("三维场景已完整显示 Mesh，使用 VBO/索引缓冲与平滑法线改善性能和细节。"));
                }
            } catch (const std::exception &e) {
                appendLog(QStringLiteral("Mesh 加载失败 [%1]：%2")
                            .arg(info.fileName(), QString::fromUtf8(e.what())));
            }

            watcher->deleteLater();
            dialog->deleteLater();
            refreshLayerTree();
            updateActionStates();
        });

        // 进入模态事件循环，保持 UI 响应，直到后台解析完成
        dialog->exec();
    }
    void MainWindow::openDem() {
        const auto &t = Translation::instance();
        const QString path = QFileDialog::getOpenFileName(
            this, t.tr(QStringLiteral("dialog.load_dem")), QString(),
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

    void MainWindow::openPanorama360() {
        const auto &t = Translation::instance();
        const QString path = QFileDialog::getOpenFileName(
            this, t.tr(QStringLiteral("dialog.load_panorama")), QString(),
            QStringLiteral("Panorama Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff);;All Files (*.*)"));
        if (path.isEmpty()) {
            return;
        }

        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setDecideFormatFromContent(true);
        QImage image = reader.read();
        QString loadDetail = QStringLiteral("Qt ImageReader");

        if (image.isNull()) {
            const QString qtError = reader.errorString();
            appendLog(QStringLiteral("Qt 读取 360 街景图失败：%1；尝试使用 GDAL 兜底。").arg(qtError));

            try {
                const auto raster = io::loadRasterDataset(path);
                if (raster && raster->bandCount() >= 3) {
                    image = io::renderRgbComposite(*raster, 0, 1, 2);
                    loadDetail = QStringLiteral("GDAL RGB fallback");
                } else if (raster && raster->bandCount() >= 1) {
                    image = io::renderSingleBandGray(*raster, 0);
                    loadDetail = QStringLiteral("GDAL gray fallback");
                }
            } catch (const std::exception &e) {
                appendLog(QStringLiteral("GDAL 兜底读取也失败：%1").arg(QString::fromUtf8(e.what())));
            }

            if (image.isNull()) {
                QStringList formatNames;
                for (const QByteArray &format : QImageReader::supportedImageFormats()) {
                    formatNames << QString::fromLatin1(format);
                }
                const QString formats = formatNames.join(QStringLiteral(", "));
                QMessageBox::warning(
                    this, QStringLiteral("加载失败"),
                    QStringLiteral("无法读取该图片文件。\n\nQt 错误：%1\n\n当前 Qt 可用图片格式：%2\n\n"
                                   "请确认运行目录下存在 imageformats/qjpeg 或 qjpegd 插件。")
                        .arg(qtError, formats));
                return;
            }
        }

        const QFileInfo info(path);
        if (image.height() > 0) {
            const double ratio = static_cast<double>(image.width()) / static_cast<double>(image.height());
            if (std::abs(ratio - 2.0) > 0.25) {
                appendLog(QStringLiteral("提示：当前 360 图片比例为 %1:1，非标准 2:1，仍按全景显示。")
                              .arg(ratio, 0, 'f', 2));
            }
        }

        const int layerIndex = layers_.add(std::make_shared<Panorama360Layer>(info.fileName(), path, image));
        refreshLayerTree();
        revealLayerInTree(layerIndex);
        panorama360Widget_->setPanorama(image, info.fileName());
        tabs_->setCurrentWidget(panorama360Widget_);
        appendLog(QStringLiteral("已加载 360 街景图：%1（%2x%3）")
                      .arg(info.fileName())
                      .arg(image.width())
                      .arg(image.height()));
        appendLog(QStringLiteral("360 街景图读取方式：%1").arg(loadDetail));
    }

    // 删除图层树中选中的图层
    void MainWindow::deleteSelectedLayers() {
        const auto indices = selectedLayerIndices();
        if (indices.empty()) {
            return;
        }

        // 检查是否有被删除的三维图层，如有则清空三维场景
        bool has3DLayer = false;
        bool hasPanoramaLayer = false;
        for (const int idx : indices) {
            try {
                const auto type = layers_.at(idx)->type();
                if (type == DataType::PointCloud || type == DataType::Mesh || type == DataType::Dem) {
                    has3DLayer = true;
                } else if (type == DataType::Panorama360) {
                    hasPanoramaLayer = true;
                }
            } catch (...) {
            }
        }

        layers_.removeMany(indices);
        imageScene_->clear();
        if (has3DLayer) {
            scene3DWidget_->clearData();
        }
        if (hasPanoramaLayer) {
            panorama360Widget_->clearPanorama();
        }
        refreshLayerTree();
        appendLog(QStringLiteral("已删除 %1 个选中图层。").arg(indices.size()));
        updateActionStates();
    }

    // 清空所有图层，重置工程
    void MainWindow::clearProject() {
        layers_.clear();      // 清空所有图层
        imageScene_->clear(); // 清空图像场景
        scene3DWidget_->clearData();
        panorama360Widget_->clearPanorama();
        refreshLayerTree();   // 刷新图层树
        appendLog(QStringLiteral("工程已初始化。"));
        updateActionStates(); // 更新菜单所有操作按钮的状态
    }

    void MainWindow::closeEvent(QCloseEvent *event) {
        saveLastSession();
        QMainWindow::closeEvent(event);
    }

    void MainWindow::saveLastSession() const {
        QSettings settings;
        settings.beginGroup(QStringLiteral("lastSession"));
        settings.remove(QString());
        settings.beginWriteArray(QStringLiteral("layers"));

        int writeIndex = 0;
        for (int i = 0; i < layers_.size(); ++i) {
            std::shared_ptr<DataObject> layer;
            try {
                layer = layers_.at(i);
            } catch (const std::exception &) {
                continue;
            }
            if (!layer || layer->path().trimmed().isEmpty() || !QFileInfo::exists(layer->path())) {
                continue;
            }

            QString type;
            switch (layer->type()) {
            case DataType::Raster:
                type = QStringLiteral("raster");
                break;
            case DataType::PointCloud:
                type = QStringLiteral("pointcloud");
                break;
            case DataType::Mesh:
                type = QStringLiteral("mesh");
                break;
            case DataType::Dem:
                type = QStringLiteral("dem");
                break;
            case DataType::Panorama360:
                type = QStringLiteral("panorama360");
                break;
            case DataType::Result:
                break;
            }
            if (type.isEmpty()) {
                continue;
            }

            settings.setArrayIndex(writeIndex++);
            settings.setValue(QStringLiteral("type"), type);
            settings.setValue(QStringLiteral("path"), layer->path());
            settings.setValue(QStringLiteral("visible"), layer->visible());
        }

        settings.endArray();
        settings.setValue(QStringLiteral("savedAt"), QDateTime::currentDateTime());
        settings.endGroup();
        settings.sync();
    }

    void MainWindow::restoreLastSession() {
        QSettings settings;
        settings.beginGroup(QStringLiteral("lastSession"));
        const int count = settings.beginReadArray(QStringLiteral("layers"));
        if (count <= 0) {
            settings.endArray();
            settings.endGroup();
            appendLog(QStringLiteral("未发现上次加载的数据。"));
            return;
        }

        int restored = 0;
        int failed = 0;
        appendLog(QStringLiteral("正在恢复上次加载的数据（%1 项）...").arg(count));

        for (int i = 0; i < count; ++i) {
            settings.setArrayIndex(i);
            const QString type = settings.value(QStringLiteral("type")).toString();
            const QString path = settings.value(QStringLiteral("path")).toString();
            const bool visible = settings.value(QStringLiteral("visible"), true).toBool();
            const QFileInfo info(path);
            if (path.isEmpty() || !info.exists()) {
                ++failed;
                appendLog(QStringLiteral("恢复失败：文件不存在 [%1]").arg(path));
                continue;
            }

            try {
                std::shared_ptr<DataObject> layer;
                if (type == QStringLiteral("raster")) {
                    layer = io::loadRasterDataset(path);
                } else if (type == QStringLiteral("dem")) {
                    layer = io::loadDemDataset(path, info.fileName());
                } else if (type == QStringLiteral("pointcloud")) {
                    layer = std::make_shared<PointCloudLayer>(info.fileName(), path, loadPointCloudPoints(path));
                } else if (type == QStringLiteral("mesh")) {
                    const LoadedMeshData mesh = loadMeshDataSync(path);
                    layer = std::make_shared<MeshLayer>(info.fileName(), path, mesh.vertices, mesh.faces);
                } else if (type == QStringLiteral("panorama360")) {
                    const QImage image = loadPanoramaImageSync(path);
                    if (image.isNull()) {
                        throw std::runtime_error("无法读取 360 街景图");
                    }
                    layer = std::make_shared<Panorama360Layer>(info.fileName(), path, image);
                }

                if (!layer) {
                    throw std::runtime_error("未知图层类型");
                }
                layer->setVisible(visible);
                layers_.add(layer);
                ++restored;
            } catch (const std::exception &e) {
                ++failed;
                appendLog(QStringLiteral("恢复失败 [%1]：%2").arg(info.fileName(), QString::fromUtf8(e.what())));
            }
        }

        settings.endArray();
        settings.endGroup();

        refreshLayerTree();
        updateActionStates();
        appendLog(QStringLiteral("已恢复上次加载的数据：成功 %1 项，失败 %2 项。").arg(restored).arg(failed));
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

    void MainWindow::runDemTextureMapping() {
        std::shared_ptr<RasterLayer> raster;
        std::shared_ptr<DemLayer> dem;

        for (const int idx : selectedLayerIndices()) {
            try {
                const auto layer = layers_.at(idx);
                if (!raster) {
                    raster = std::dynamic_pointer_cast<RasterLayer>(layer);
                }
                if (!dem) {
                    dem = std::dynamic_pointer_cast<DemLayer>(layer);
                }
            } catch (const std::exception &) {
            }
        }

        if (!raster || !dem) {
            appendLog(QStringLiteral("请同时选中一个遥感影像图层和一个 DEM 图层，再执行 DEM 三维贴图。"));
            return;
        }

        QImage texture = raster->currentDisplayImage();
        QString textureSource = raster->renderDescription();
        if (texture.isNull()) {
            if (raster->bandCount() >= 3) {
                texture = io::renderRgbComposite(*raster, 0, 1, 2);
                textureSource = QStringLiteral("Auto RGB (Band 1/2/3)");
            } else if (raster->bandCount() >= 1) {
                texture = io::renderSingleBandGray(*raster, 0);
                textureSource = QStringLiteral("Gray (Band 1)");
            }
        }

        if (texture.isNull()) {
            appendLog(QStringLiteral("DEM 三维贴图失败：影像 %1 没有可用的渲染图像。").arg(raster->name()));
            return;
        }

        scene3DWidget_->setDem(*dem, texture);
        scene3DWidget_->fitToBounds();
        tabs_->setCurrentWidget(scene3DWidget_);

        QString note;
        if (raster->bandCount() > 0 &&
            (raster->band(0).width != dem->width() || raster->band(0).height != dem->height())) {
            note = QStringLiteral("（影像与 DEM 尺寸不同，已按 DEM 网格范围拉伸贴合）");
        }
        appendLog(QStringLiteral("已完成 DEM 三维贴图：DEM=%1，纹理=%2，来源=%3 %4")
                      .arg(dem->name(), raster->name(), textureSource, note));
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
                        scene3DWidget_->setMeshPreview(mesh->vertices(), mesh->faces());
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
                } else if (layer->type() == DataType::Panorama360) {
                    const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer);
                    if (pano && !pano->image().isNull()) {
                        panorama360Widget_->setPanorama(pano->image(), pano->name());
                        tabs_->setCurrentWidget(panorama360Widget_);
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
                    scene3DWidget_->setMeshPreview(mesh->vertices(), mesh->faces());
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
        } else if (layer->type() == DataType::Panorama360) {
            const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer);
            if (visible && pano && !pano->image().isNull()) {
                panorama360Widget_->setPanorama(pano->image(), pano->name());
                tabs_->setCurrentWidget(panorama360Widget_);
            } else {
                panorama360Widget_->clearPanorama();
            }
        }
        appendLog(QStringLiteral("%1：%2").arg(item->text(0), visible ? QStringLiteral("显示")
                                                                      : QStringLiteral("隐藏")));
    } catch (const std::exception &) {
    }
}

// 右键点击图层树时弹出上下文菜单
void MainWindow::showLayerContextMenu(const QPoint &position) {
    const auto &tr = Translation::instance();
    QTreeWidgetItem *item = layerTree_->itemAt(position);
    if (!item) {
        return;
    }
    const QVariant layerIndexVar = item->data(0, kLayerIndexRole);
    const int nodeKind = item->data(0, kNodeKindRole).toInt();
    QMenu menu(this);

    if (!layerIndexVar.isValid() || nodeKind != static_cast<int>(NodeKind::Layer)) {
        const auto folderIndices = collectLayerIndicesUnder(item);
        std::vector<int> exportableIndices;
        for (const int idx : folderIndices) {
            if (canExportLayer(idx)) {
                exportableIndices.push_back(idx);
            }
        }

        if (!exportableIndices.empty()) {
            QAction *exportAction = menu.addAction(tr.tr(QStringLiteral("action.export_group")));
            connect(exportAction, &QAction::triggered, this, [this, exportableIndices]() {
                const QString dirPath = QFileDialog::getExistingDirectory(
                    this, Translation::instance().tr(QStringLiteral("dialog.select_export_dir")));
                if (dirPath.isEmpty()) {
                    return;
                }

                QDir dir(dirPath);
                int okCount = 0;
                for (const int idx : exportableIndices) {
                    try {
                        const auto layer = layers_.at(idx);
                        const QString suffix = layer->type() == DataType::Dem ? QStringLiteral(".tif")
                                             : QStringLiteral(".png");
                        const QString path = dir.filePath(safeFileBaseName(layer->name()) + suffix);
                        if (exportLayerToPath(idx, path)) {
                            ++okCount;
                        }
                    } catch (const std::exception &) {
                    }
                }
                appendLog(QStringLiteral("已从树状分组导出 %1/%2 个图层。")
                              .arg(okCount)
                              .arg(exportableIndices.size()));
            });
            menu.addSeparator();
        }

        const auto indices = selectedLayerIndices();
        QAction *deleteAction =
            menu.addAction(tr.tr(QStringLiteral("action.delete_layer")));
        deleteAction->setEnabled(!indices.empty());
        connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
        menu.exec(layerTree_->viewport()->mapToGlobal(position));
        return;
    }

    layerTree_->setCurrentItem(item);

    const int layerIndex = layerIndexVar.toInt();
    std::shared_ptr<DataObject> layer;
    try {
        layer = layers_.at(layerIndex);
    } catch (const std::exception &) {
        return;
    }

    QAction *deleteAction = menu.addAction(tr.tr(QStringLiteral("action.delete_single_layer")));
    connect(deleteAction, &QAction::triggered, this, [this, layerIndex]() {
        try {
            const auto type = layers_.at(layerIndex)->type();
            if (type == DataType::PointCloud || type == DataType::Mesh || type == DataType::Dem) {
                scene3DWidget_->clearData();
            }
            if (type == DataType::Raster) {
                imageScene_->clear();
            }
            if (type == DataType::Panorama360) {
                panorama360Widget_->clearPanorama();
            }
        } catch (...) {
        }
        layers_.removeMany({layerIndex});
        refreshLayerTree();
        appendLog(QStringLiteral("已删除图层。"));
        updateActionStates();
    });

    // ── 导出 ──
    if (canExportLayer(layerIndex)) {
        menu.addSeparator();
        QAction *exportAction = menu.addAction(tr.tr(QStringLiteral("action.export_layer")));
        connect(exportAction, &QAction::triggered, this, [this, layerIndex]() {
            exportLayerImage(layerIndex);
        });
    }

    QAction *zoomAction = menu.addAction(tr.tr(QStringLiteral("action.zoom_to_extent")));
    connect(zoomAction, &QAction::triggered, this, [this, layer]() {
        if (layer->type() == DataType::Raster) {
            appendLog(QStringLiteral("TODO: 缩放至 %1 的影像范围。").arg(layer->name()));
        } else if (layer->type() == DataType::PointCloud || layer->type() == DataType::Mesh) {
            scene3DWidget_->fitToBounds();
            appendLog(QStringLiteral("缩放至 %1 的范围。").arg(layer->name()));
        } else if (layer->type() == DataType::Panorama360) {
            if (const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer)) {
                panorama360Widget_->setPanorama(pano->image(), pano->name());
                tabs_->setCurrentWidget(panorama360Widget_);
            }
            appendLog(QStringLiteral("已切换到 360 街景图层：%1。").arg(layer->name()));
        } else {
            appendLog(QStringLiteral("TODO: 缩放至 %1 的范围。").arg(layer->name()));
        }
    });

    // ── 属性对话框 ──
    QAction *propAction = menu.addAction(tr.tr(QStringLiteral("action.properties")));
    connect(propAction, &QAction::triggered, this, [this, layer]() {
        const auto &t = Translation::instance();
        const QString typeName = layerTypeLabel(layer->type());

        QString info;
        info += t.tr(QStringLiteral("prop.name")) + QStringLiteral(" %1\n").arg(layer->name());
        info += t.tr(QStringLiteral("prop.path")) + QStringLiteral(" %1\n").arg(layer->path());
        info += t.tr(QStringLiteral("prop.type")) + QStringLiteral(" %1\n").arg(typeName);
        info += t.tr(QStringLiteral("prop.visible")) +
                QStringLiteral(" %1\n")
                    .arg(layer->visible() ? t.tr(QStringLiteral("prop.yes"))
                                          : t.tr(QStringLiteral("prop.no")));

        if (const auto raster = std::dynamic_pointer_cast<RasterLayer>(layer)) {
            info += t.tr(QStringLiteral("prop.bands")) + QStringLiteral(" %1\n").arg(raster->bandCount());
            if (raster->bandCount() > 0) {
                const auto &b = raster->band(0);
                info += t.tr(QStringLiteral("prop.size_pixels")).arg(b.width).arg(b.height) + QStringLiteral("\n");
            }
            info += t.tr(QStringLiteral("prop.projection")) +
                    QStringLiteral(" %1\n")
                        .arg(raster->projection().isEmpty() ? t.tr(QStringLiteral("prop.unknown"))
                                                            : raster->projection());
        } else if (const auto pc = std::dynamic_pointer_cast<PointCloudLayer>(layer)) {
            info += t.tr(QStringLiteral("prop.point_count")) + QStringLiteral(" %1\n").arg(pc->points().size());
        } else if (const auto mesh = std::dynamic_pointer_cast<MeshLayer>(layer)) {
            info += t.tr(QStringLiteral("prop.vertices")) + QStringLiteral(" %1\n").arg(mesh->vertices().size());
            info += t.tr(QStringLiteral("prop.triangles")) + QStringLiteral(" %1\n").arg(mesh->faces().size());
        } else if (const auto dem = std::dynamic_pointer_cast<DemLayer>(layer)) {
            info += t.tr(QStringLiteral("prop.size")).arg(dem->width()).arg(dem->height()) + QStringLiteral("\n");
        } else if (const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer)) {
            info += t.tr(QStringLiteral("prop.size_pixels")).arg(pano->image().width()).arg(pano->image().height()) +
                    QStringLiteral("\n");
        }

        info += QStringLiteral("\n") + t.tr(QStringLiteral("prop.summary")) + QStringLiteral(" %1").arg(layer->summary());
        QMessageBox::information(nullptr, t.tr(QStringLiteral("prop.title")).arg(layer->name()), info);
    });

    menu.exec(layerTree_->viewport()->mapToGlobal(position));
}

// 根据 LayerManager 中的数据重建图层树，保持展开/折叠状态
void MainWindow::refreshLayerTree() {
    const auto &t = Translation::instance();
    QSet<QString> expandedKeys;
    for (int i = 0; i < layerTree_->topLevelItemCount(); ++i) {
        collectExpandedKeys(layerTree_->topLevelItem(i), expandedKeys);
    }

    rebuildingTree_ = true;
    layerTree_->clear();

    auto *sourceRoot = ensureTopFolder(layerTree_, QStringLiteral("source"),
                                       t.tr(QStringLiteral("tree.source_data")));
    auto *resultRoot = ensureTopFolder(layerTree_, QStringLiteral("results"),
                                       t.tr(QStringLiteral("tree.results")));
    auto *rasterFolder = ensureChildFolder(sourceRoot, QStringLiteral("source/raster"),
                                           t.tr(QStringLiteral("tree.raster")));
    auto *pointFolder = ensureChildFolder(sourceRoot, QStringLiteral("source/pointcloud"),
                                          t.tr(QStringLiteral("tree.pointcloud")));
    auto *meshFolder = ensureChildFolder(sourceRoot, QStringLiteral("source/mesh"), QStringLiteral("Mesh"));
    auto *demFolder = ensureChildFolder(sourceRoot, QStringLiteral("source/dem"), QStringLiteral("DEM"));
    auto *panoramaFolder = ensureChildFolder(sourceRoot, QStringLiteral("source/panorama360"),
                                             t.tr(QStringLiteral("tree.panorama360")));

    for (int i = 0; i < layers_.size(); ++i) {
        const auto layer = layers_.at(i);
        QTreeWidgetItem *parent = nullptr;
        if (!layer->treeGroup().isEmpty()) {
            const QString groupKey = QStringLiteral("results/") + layer->treeGroup();
            parent = ensureChildFolder(resultRoot, groupKey, localizedTreeGroupName(layer->treeGroup()));
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
            case DataType::Panorama360:
                parent = panoramaFolder;
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

    for (auto *root : {sourceRoot, resultRoot}) {
        for (int i = root->childCount() - 1; i >= 0; --i) {
            auto *child = root->child(i);
            if (child && child->childCount() == 0) {
                delete root->takeChild(i);
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
    const auto &t = Translation::instance();
    imageScene_->clear();
    activeRasterForCoords_ = raster;
    activeDisplaySizeForCoords_ = QSize();
    if (!raster) {
        imageScene_->addText(t.tr(QStringLiteral("view.select_layer")));
        if (coordLabel_) {
            coordLabel_->setText(QString());
        }
        return;
    }

    QImage image;
    if (bandIndex >= 0 && bandIndex < raster->bandCount()) {
        image = io::renderSingleBandGray(*raster, bandIndex);
    } else {
        image = raster->currentDisplayImage();
    }

    if (image.isNull()) {
        imageScene_->addText(t.tr(QStringLiteral("view.no_render")).arg(raster->name()));
        if (coordLabel_) {
            coordLabel_->setText(QString());
        }
        return;
    }
    activeDisplaySizeForCoords_ = image.size();

    imageScene_->addPixmap(QPixmap::fromImage(image));
    imageScene_->setSceneRect(image.rect());
    imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (!imageView_ || !coordLabel_ || !event) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (watched == imageView_->viewport()) {
        if (event->type() == QEvent::Leave) {
            coordLabel_->setText(QString());
            return QMainWindow::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseMove) {
            const auto raster = activeRasterForCoords_;
            if (!raster || imageScene_->sceneRect().isEmpty()) {
                coordLabel_->setText(QString());
                return QMainWindow::eventFilter(watched, event);
            }

            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint viewPos = mouseEvent->pos();
            const QPointF scenePos = imageView_->mapToScene(viewPos);
            const QRectF rect = imageScene_->sceneRect();
            if (!rect.contains(scenePos)) {
                coordLabel_->setText(QString());
                return QMainWindow::eventFilter(watched, event);
            }

            const int displayW = activeDisplaySizeForCoords_.isEmpty()
                                     ? static_cast<int>(rect.width())
                                     : activeDisplaySizeForCoords_.width();
            const int displayH = activeDisplaySizeForCoords_.isEmpty()
                                     ? static_cast<int>(rect.height())
                                     : activeDisplaySizeForCoords_.height();
            if (displayW <= 0 || displayH <= 0 || raster->bandCount() <= 0) {
                coordLabel_->setText(QString());
                return QMainWindow::eventFilter(watched, event);
            }

            const int rasterW = raster->band(0).width;
            const int rasterH = raster->band(0).height;
            if (rasterW <= 0 || rasterH <= 0) {
                coordLabel_->setText(QString());
                return QMainWindow::eventFilter(watched, event);
            }

            const double sx = static_cast<double>(rasterW) / static_cast<double>(displayW);
            const double sy = static_cast<double>(rasterH) / static_cast<double>(displayH);
            const double px = scenePos.x() * sx;
            const double py = scenePos.y() * sy;

            const auto gt = raster->geoTransform();
            const double geoX = gt[0] + px * gt[1] + py * gt[2];
            const double geoY = gt[3] + px * gt[4] + py * gt[5];

            QString text = QStringLiteral("Pixel: (%1, %2)").arg(static_cast<int>(px)).arg(static_cast<int>(py));
            if (hasGeoreference(*raster)) {
                text += QStringLiteral("  Geo: (%1, %2)").arg(geoX, 0, 'f', 3).arg(geoY, 0, 'f', 3);
#ifdef RS_WITH_GDAL
                double lon = 0.0;
                double lat = 0.0;
                if (tryProjectToLonLat(*raster, geoX, geoY, lon, lat)) {
                    text += QStringLiteral("  Lon/Lat: (%1, %2)").arg(lon, 0, 'f', 6).arg(lat, 0, 'f', 6);
                }
#endif
            }
            coordLabel_->setText(text);
            return QMainWindow::eventFilter(watched, event);
        }
    }

    return QMainWindow::eventFilter(watched, event);
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
    if (demTextureAction_) {
        demTextureAction_->setEnabled(selectedRasters >= 1 && selectedDems >= 1);
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

void MainWindow::refreshStartupLog() {
    if (!logEdit_) {
        return;
    }

    const QString message = Translation::instance().tr(QStringLiteral("log.startup"));
    QString text = logEdit_->toPlainText();

    if (!startupLogPresent_ || text.isEmpty()) {
        logEdit_->setPlainText(
            QStringLiteral("[%1] %2")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
        startupLogPresent_ = true;
        return;
    }

    const int newline = text.indexOf(QLatin1Char('\n'));
    const QString firstLine = newline >= 0 ? text.left(newline) : text;
    QString timestamp;
    if (firstLine.startsWith(QLatin1Char('['))) {
        const int end = firstLine.indexOf(QLatin1Char(']'));
        if (end > 0) {
            timestamp = firstLine.left(end + 1);
        }
    }
    if (timestamp.isEmpty()) {
        timestamp = QStringLiteral("[%1]").arg(
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
    }

    const QString newFirstLine = QStringLiteral("%1 %2").arg(timestamp, message);
    if (newline >= 0) {
        logEdit_->setPlainText(newFirstLine + text.mid(newline));
    } else {
        logEdit_->setPlainText(newFirstLine);
    }
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

bool MainWindow::canExportLayer(int layerIndex) const {
    std::shared_ptr<DataObject> layer;
    try {
        layer = layers_.at(layerIndex);
    } catch (const std::exception &) {
        return false;
    }

    return layer->type() == DataType::Raster || layer->type() == DataType::Dem ||
           layer->type() == DataType::Panorama360;
}

bool MainWindow::exportLayerToPath(int layerIndex, const QString &path) {
    std::shared_ptr<DataObject> layer;
    try {
        layer = layers_.at(layerIndex);
    } catch (const std::exception &) {
        return false;
    }

    if (path.isEmpty()) {
        return false;
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
            return false;
        }
        if (!image.save(path)) {
            appendLog(QStringLiteral("导出失败：无法写入 %1").arg(path));
            return false;
        }
        appendLog(QStringLiteral("已导出影像：%1 → %2").arg(raster->name(), path));
        return true;
    }

    if (const auto dem = std::dynamic_pointer_cast<DemLayer>(layer)) {
        try {
            io::exportDemAsGeoTiff(*dem, path);
            appendLog(QStringLiteral("已导出 DEM：%1 → %2").arg(dem->name(), path));
            return true;
        } catch (const std::exception &e) {
            appendLog(QStringLiteral("DEM 导出失败：%1").arg(QString::fromUtf8(e.what())));
            return false;
        }
    }

    if (const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer)) {
        if (pano->image().isNull()) {
            appendLog(QStringLiteral("导出失败 [%1]：没有可导出的街景图。").arg(pano->name()));
            return false;
        }
        if (!pano->image().save(path)) {
            appendLog(QStringLiteral("导出失败：无法写入 %1").arg(path));
            return false;
        }
        appendLog(QStringLiteral("已导出 360 街景图：%1 → %2").arg(pano->name(), path));
        return true;
    }

    appendLog(QStringLiteral("导出失败 [%1]：该图层类型暂不支持导出。").arg(layer->name()));
    return false;
}

std::vector<int> MainWindow::collectLayerIndicesUnder(QTreeWidgetItem *item) const {
    std::vector<int> indices;
    if (!item) {
        return indices;
    }

    const auto collect = [&indices](QTreeWidgetItem *node, const auto &self) -> void {
        if (!node) {
            return;
        }
        if (static_cast<NodeKind>(node->data(0, kNodeKindRole).toInt()) == NodeKind::Layer) {
            const QVariant value = node->data(0, kLayerIndexRole);
            if (value.isValid()) {
                const int idx = value.toInt();
                if (std::find(indices.begin(), indices.end(), idx) == indices.end()) {
                    indices.push_back(idx);
                }
            }
        }
        for (int i = 0; i < node->childCount(); ++i) {
            self(node->child(i), self);
        }
    };

    collect(item, collect);
    return indices;
}

void MainWindow::exportLayerImage(int layerIndex) {
    std::shared_ptr<DataObject> layer;
    try {
        layer = layers_.at(layerIndex);
    } catch (const std::exception &) {
        return;
    }

    const auto &t = Translation::instance();
    QString title = t.tr(QStringLiteral("dialog.export_layer"));
    QString defaultPath = layer->name();
    QString filters = QStringLiteral("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;TIFF (*.tif *.tiff)");
    if (layer->type() == DataType::Dem) {
        title = t.tr(QStringLiteral("dialog.export_dem"));
        defaultPath = safeFileBaseName(layer->name()) + QStringLiteral(".tif");
        filters = QStringLiteral("GeoTIFF (*.tif *.tiff)");
    } else {
        defaultPath = safeFileBaseName(layer->name()) + QStringLiteral(".png");
    }

    const QString path = QFileDialog::getSaveFileName(this, title, defaultPath, filters);
    if (path.isEmpty()) {
        return;
    }
    exportLayerToPath(layerIndex, path);
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

void MainWindow::openSettings() {
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::retranslateUi() {
    const auto &t = Translation::instance();
    setWindowTitle(t.tr(QStringLiteral("window_title")));

    for (QMenu *menu : translatableMenus_) {
        const QString key = menu->property("trKey").toString();
        if (!key.isEmpty()) {
            menu->setTitle(t.tr(key));
        }
    }
    for (QAction *action : translatableActions_) {
        const QString key = action->property("trKey").toString();
        if (!key.isEmpty()) {
            action->setText(t.tr(key));
        }
    }

    if (settingsButton_) {
        settingsButton_->setText(t.tr(QStringLiteral("settings.button")));
        settingsButton_->setToolTip(t.tr(QStringLiteral("settings.title")));
    }

    if (layerTree_) {
        layerTree_->setHeaderLabel(t.tr(QStringLiteral("layer_tree")));
    }
    if (tabs_) {
        if (tabs_->count() >= 3) {
            tabs_->setTabText(0, t.tr(QStringLiteral("tab.2d")));
            tabs_->setTabText(1, t.tr(QStringLiteral("tab.3d")));
            tabs_->setTabText(2, t.tr(QStringLiteral("tab.panorama")));
        }
    }
    if (bottomTabs_) {
        if (bottomTabs_->count() >= 2) {
            bottomTabs_->setTabText(0, t.tr(QStringLiteral("tab.log")));
            bottomTabs_->setTabText(1, t.tr(QStringLiteral("tab.ai")));
        }
    }
    refreshStartupLog();
    refreshLayerTree();

    const auto raster = selectedRaster();
    displayRaster(raster, selectedBandIndex());
}

} // namespace rs

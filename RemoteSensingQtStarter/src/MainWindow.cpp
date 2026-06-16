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

    // 图层树节点的类型枚举
    enum class NodeKind {
        Folder, // 文件夹节点（如"源数据/遥感影像"），不可选中
        Layer,  // 图层节点，可选中/勾选
        Band    // 波段子节点，仅信息展示
    };

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
        createMenus();
        createUi();
        setupSettingsButton();
        connect(&Translation::instance(), &Translation::languageChanged, this, &MainWindow::retranslateUi);
        retranslateUi();
        appendLog(QStringLiteral("Starter 已启动：当前版本提供 GDAL "
                                "多波段、参数化算法、DEM/正射流程的工程骨架。"));
        updateActionStates();
    }

    // 构建菜单栏：数据、影像处理、摄影测量/三维
    void MainWindow::createMenus() {
        // ---- "数据" 菜单 ----
        auto *dataMenu = menuBar()->addMenu(QString());
        dataMenu_ = dataMenu;
        loadRasterAction_ = dataMenu->addAction(QString());
        connect(loadRasterAction_, &QAction::triggered, this, &MainWindow::openRasterDatasets);
        loadPointCloudAction_ = dataMenu->addAction(QString());
        connect(loadPointCloudAction_, &QAction::triggered, this, &MainWindow::openPointCloud);
        loadMeshAction_ = dataMenu->addAction(QString());
        connect(loadMeshAction_, &QAction::triggered, this, &MainWindow::openMesh);
        loadDemAction_ = dataMenu->addAction(QString());
        connect(loadDemAction_, &QAction::triggered, this, &MainWindow::openDem);
        loadPanoramaAction_ = dataMenu->addAction(QStringLiteral("加载 360 街景图..."));
        connect(loadPanoramaAction_, &QAction::triggered, this, &MainWindow::openPanorama360);
        dataMenu->addSeparator();      // 添加分隔线，将加载与删除操作分开
        deleteLayerAction_ = dataMenu->addAction(
            QStringLiteral("删除选中图层")); // 添加"删除选中图层"并保存指针以便控制启用/禁用
        connect(deleteLayerAction_, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);
        clearProjectAction_ =
            dataMenu->addAction(QStringLiteral("初始化/清空工程")); // 添加"清空工程"并保存指针
        connect(clearProjectAction_, &QAction::triggered, this, &MainWindow::clearProject);

        // ---- "影像处理" 菜单 ----
        auto *rasterMenu = menuBar()->addMenu(QString());
        rasterMenu_ = rasterMenu;
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

        auto *indexMenu = menuBar()->addMenu(QString());
        indexMenu_ = indexMenu;
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
        auto *photogrammetryMenu = menuBar()->addMenu(QString());
        photogrammetryMenu_ = photogrammetryMenu;
        demAction_ =
            photogrammetryMenu->addAction(QStringLiteral("DEM 重建...")); // 添加"DEM 重建"并保存指针
        connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
        orthoAction_ = photogrammetryMenu->addAction(
            QStringLiteral("正射影像校正...")); // 添加"正射影像校正"并保存指针
        connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);
        demTextureAction_ = photogrammetryMenu->addAction(
            QStringLiteral("DEM 三维贴图..."));
        connect(demTextureAction_, &QAction::triggered, this, &MainWindow::runDemTextureMapping);

        // ---- "点云处理" 菜单 ----
        auto *pcMenu = menuBar()->addMenu(QString());
        pcMenu_ = pcMenu;
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

        aiMenu_ = menuBar()->addMenu(QString());
        showAiAction_ = aiMenu_->addAction(QString());
        connect(showAiAction_, &QAction::triggered, this, [this, aiPanel]() {
            bottomTabs_->setCurrentWidget(aiPanel);
        });

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
            QWidget#menuWrap {
                background-color: #ffffff;
                border-bottom: 2px solid #f4b8c8;
            }
            QPushButton#settingsButton {
                background: transparent;
                color: #5a4a4a;
                border: none;
                border-radius: 4px;
                padding: 6px 16px;
                font-size: 13px;
                font-weight: normal;
            }
            QPushButton#settingsButton:hover {
                background-color: #fce4ec;
                color: #8b5a6a;
            }
            QPushButton#settingsButton:pressed {
                background-color: #fce4ec;
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
                    const QString ext = info.suffix().toLower();
                    if ((ext == QStringLiteral("tif") || ext == QStringLiteral("tiff")) &&
                        hasGeoreference(*raster)) {
                        appendLog(QStringLiteral("提示：该 GeoTIFF/GeoRaster 包含坐标信息（可用于经纬度显示）。"));
                    }
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
    // 在后台线程解析 Mesh 文件（OBJ/PLY），避免阻塞 UI
    // 在后台线程解析 Mesh 文件（OBJ/PLY），避免阻塞 UI
    void MainWindow::openMesh() {
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("加载 Mesh"), QString(),
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

    void MainWindow::openPanorama360() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("加载 360 街景图"), QString(),
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
            QAction *exportAction = menu.addAction(QStringLiteral("导出该分组..."));
            connect(exportAction, &QAction::triggered, this, [this, exportableIndices]() {
                const QString dirPath = QFileDialog::getExistingDirectory(this, QStringLiteral("选择导出文件夹"));
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

        // 文件夹或波段节点：允许删除当前选中的图层
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
        case DataType::Panorama360:
            typeName = QStringLiteral("360 街景图");
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
        } else if (const auto pano = std::dynamic_pointer_cast<Panorama360Layer>(layer)) {
            info += QStringLiteral("尺寸: %1 x %2像素\n").arg(pano->image().width()).arg(pano->image().height());
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
    auto *panoramaFolder = ensureChildFolder(sourceRoot, QStringLiteral("360街景"));

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
    imageScene_->clear();
    activeRasterForCoords_ = raster;
    activeDisplaySizeForCoords_ = QSize();
    if (!raster) {
        imageScene_->addText(QStringLiteral("请选择一个遥感影像图层或波段。"));
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
        imageScene_->addText(
            QStringLiteral("当前影像没有可显示的渲染结果。\n当前图层：%1").arg(raster->name()));
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

    QString title = QStringLiteral("导出图层");
    QString defaultPath = layer->name();
    QString filters = QStringLiteral("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;TIFF (*.tif *.tiff)");
    if (layer->type() == DataType::Dem) {
        title = QStringLiteral("导出 DEM");
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

    if (dataMenu_) {
        dataMenu_->setTitle(t.tr(QStringLiteral("menu.data")));
    }
    if (loadRasterAction_) {
        loadRasterAction_->setText(t.tr(QStringLiteral("action.load_raster")));
    }
    if (loadPointCloudAction_) {
        loadPointCloudAction_->setText(t.tr(QStringLiteral("action.load_pointcloud")));
    }
    if (loadMeshAction_) {
        loadMeshAction_->setText(t.tr(QStringLiteral("action.load_mesh")));
    }
    if (loadDemAction_) {
        loadDemAction_->setText(t.tr(QStringLiteral("action.load_dem")));
    }
    if (loadPanoramaAction_) {
        loadPanoramaAction_->setText(QStringLiteral("加载 360 街景图..."));
    }
    if (deleteLayerAction_) {
        deleteLayerAction_->setText(t.tr(QStringLiteral("action.delete_layer")));
    }
    if (clearProjectAction_) {
        clearProjectAction_->setText(t.tr(QStringLiteral("action.clear_project")));
    }
    if (rasterMenu_) {
        rasterMenu_->setTitle(t.tr(QStringLiteral("menu.raster")));
    }
    if (indexMenu_) {
        indexMenu_->setTitle(t.tr(QStringLiteral("menu.index")));
    }
    if (photogrammetryMenu_) {
        photogrammetryMenu_->setTitle(t.tr(QStringLiteral("menu.photogrammetry")));
    }
    if (pcMenu_) {
        pcMenu_->setTitle(t.tr(QStringLiteral("menu.pointcloud")));
    }
    if (aiMenu_) {
        aiMenu_->setTitle(t.tr(QStringLiteral("menu.ai")));
    }
    if (settingsButton_) {
        settingsButton_->setText(t.tr(QStringLiteral("settings.button")));
        settingsButton_->setToolTip(t.tr(QStringLiteral("settings.title")));
    }
    if (showAiAction_) {
        showAiAction_->setText(t.tr(QStringLiteral("action.show_ai")));
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
}

} // namespace rs

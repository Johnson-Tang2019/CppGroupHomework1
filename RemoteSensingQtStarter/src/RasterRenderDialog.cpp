#include "rs/RasterRenderDialog.h"
#include <QDialog>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace rs {

std::optional<RasterRenderRequest> askRasterRenderRequest(QWidget* parent, const RasterLayer& raster) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("\u6CE2\u6BB5\u7EC4\u5408/\u8BBE\u8272\u8BBE\u7F6E"));
    dialog.setMinimumWidth(400);

    auto* mainLayout = new QVBoxLayout(&dialog);

    auto* modeGroup = new QGroupBox(QStringLiteral("\u6E32\u67D3\u6A21\u5F0F"), &dialog);
    auto* modeLayout = new QVBoxLayout(modeGroup);

    auto* autoRgbRadio = new QRadioButton(QStringLiteral("\u81EA\u52A8 RGB (Band 1/2/3)"));
    auto* manualRgbRadio = new QRadioButton(QStringLiteral("\u624B\u52A8\u6307\u5B9A RGB \u6CE2\u6BB5"));
    auto* grayRadio = new QRadioButton(QStringLiteral("\u5355\u6CE2\u6BB5\u7070\u5EA6"));
    auto* pseudoRadio = new QRadioButton(QStringLiteral("\u4F2A\u5F69\u8272"));

    modeLayout->addWidget(autoRgbRadio);
    modeLayout->addWidget(manualRgbRadio);
    modeLayout->addWidget(grayRadio);
    modeLayout->addWidget(pseudoRadio);

    autoRgbRadio->setChecked(true);
    mainLayout->addWidget(modeGroup);

    auto* bandGroup = new QGroupBox(QStringLiteral("\u6CE2\u6BB5\u9009\u62E9"), &dialog);
    auto* bandLayout = new QFormLayout(bandGroup);

    auto* redCombo = new QComboBox();
    auto* greenCombo = new QComboBox();
    auto* blueCombo = new QComboBox();
    auto* grayCombo = new QComboBox();

    const int bandCount = raster.bandCount();
    for (int i = 0; i < bandCount; ++i) {
        const QString label = QStringLiteral("Band %1").arg(i + 1);
        redCombo->addItem(label);
        greenCombo->addItem(label);
        blueCombo->addItem(label);
        grayCombo->addItem(label);
    }
    if (bandCount > 0) redCombo->setCurrentIndex(0);
    if (bandCount > 1) greenCombo->setCurrentIndex(1);
    if (bandCount > 2) blueCombo->setCurrentIndex(2);

    bandLayout->addRow(QStringLiteral("\u7EA2\u8272\u6CE2\u6BB5 (R):"), redCombo);
    bandLayout->addRow(QStringLiteral("\u7EFF\u8272\u6CE2\u6BB5 (G):"), greenCombo);
    bandLayout->addRow(QStringLiteral("\u84DD\u8272\u6CE2\u6BB5 (B):"), blueCombo);
    bandLayout->addRow(QStringLiteral("\u7070\u5EA6\u6CE2\u6BB5:"), grayCombo);

    bandGroup->setEnabled(false);
    mainLayout->addWidget(bandGroup);

    auto* stretchCheck = new QCheckBox(QStringLiteral("\u62C9\u4F38\u5230 0-255\uFF08\u6539\u5584\u663E\u793A\u6548\u679C\uFF09"));
    stretchCheck->setChecked(true);
    mainLayout->addWidget(stretchCheck);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QObject::connect(manualRgbRadio, &QRadioButton::toggled, bandGroup, &QGroupBox::setEnabled);
    QObject::connect(grayRadio, &QRadioButton::toggled, [&](bool checked) {
        grayCombo->setEnabled(checked);
        redCombo->setEnabled(!checked);
        greenCombo->setEnabled(!checked);
        blueCombo->setEnabled(!checked);
    });
    QObject::connect(autoRgbRadio, &QRadioButton::toggled, [&](bool checked) {
        if (checked && !manualRgbRadio->isChecked()) {
            bandGroup->setEnabled(false);
        }
    });
    QObject::connect(pseudoRadio, &QRadioButton::toggled, [&](bool checked) {
        if (checked) {
            bandGroup->setEnabled(true);
            grayCombo->setEnabled(true);
            redCombo->setEnabled(false);
            greenCombo->setEnabled(false);
            blueCombo->setEnabled(false);
        }
    });

    grayCombo->setEnabled(false);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    RasterRenderRequest request;
    request.stretchToByte = stretchCheck->isChecked();

    if (autoRgbRadio->isChecked()) {
        request.mode = RasterRenderMode::AutoRgb;
    } else if (manualRgbRadio->isChecked()) {
        request.mode = RasterRenderMode::RgbBands;
        request.redBand = redCombo->currentIndex();
        request.greenBand = greenCombo->currentIndex();
        request.blueBand = blueCombo->currentIndex();
    } else if (grayRadio->isChecked()) {
        request.mode = RasterRenderMode::SingleBandGray;
        request.grayBand = grayCombo->currentIndex();
    } else if (pseudoRadio->isChecked()) {
        request.mode = RasterRenderMode::PseudoColor;
        request.grayBand = grayCombo->currentIndex();
    }

    return request;
}

} // namespace rs


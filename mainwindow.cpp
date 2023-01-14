#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "qndsimage.h"
#include "crc.h"
#include "ndsbanneranimplayer.h"

#include <QVector>
#include <QMessageBox>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMimeData>
#include <QStandardItemModel>
#include <QTemporaryFile>
#include <QTranslator>
#include <QSettings>

extern QTranslator *translator;
extern bool translationLoaded;

static const char* NameForTranslation[] = { "en", "ja", "pt" };

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->gfx_gv->scale(4, 4);
    ui->gfx_gv->setScene(&this->gfx_scene);
    this->lastDirPath = QDir::homePath();

    setProgramState(ProgramState::Closed);

    QSettings settings;
    bool langOk;
    int language = settings.value("language", 0).toInt(&langOk);
    if (langOk)
        changeLanguage(language);
    else
        settings.setValue("language", 0);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setProgramState(ProgramState mode)
{
    bool closed = mode == ProgramState::Closed;

    this->setAcceptDrops(closed);
    ui->gfx_gv->setAcceptDrops(!closed);

    ui->gfx_gb->setEnabled(!closed);
    ui->gameTitle_gb->setEnabled(!closed);
    ui->bannerVersion_gb->setEnabled(!closed);
    ui->anim_gb->setEnabled(!closed);

    ui->actionNew->setEnabled(closed);
    ui->actionOpen->setEnabled(closed);
    ui->actionSave->setEnabled(mode == ProgramState::KnowsPath);
    ui->actionSave_As->setEnabled(!closed);
    ui->actionClose->setEnabled(!closed);

    ui->gfxBmp_sb->blockSignals(closed);
    ui->gfxPal_sb->blockSignals(closed);
    ui->gameTitle_cb->blockSignals(closed);
    ui->gameTitle_pte->blockSignals(closed);
    ui->bannerVersion_cb->blockSignals(closed);

    setAnimGroupBlockSignals(closed);

    if(closed)
    {
        this->gfx_scene.clear();
        this->openedFileName.clear();

        ui->gfxBmp_sb->setValue(0);
        ui->gfxPal_sb->setValue(0);
        ui->gfxPal_sb->setMinimum(0);

        ui->gameTitle_cb->setCurrentIndex(0);
        ui->gameTitle_pte->setPlainText("");

        ui->bannerVersion_cb->setCurrentIndex(3);

        ui->animFrame_cb->clear();
        setAnimGroupEnabled(false);

        this->gfxBmp_lastValue = 0;
        this->animFrame_lastSize = 0;
    }

    ui->gfxPal_sb->setEnabled(false);

}

void MainWindow::changeLanguage(int language)
{
    if (translationLoaded)
        QApplication::removeTranslator(translator);
    if (language != 0) // if not English
    {
        if (translator->load(QLocale(NameForTranslation[language]), QLatin1String("nbe"), QLatin1String("_"), QLatin1String(":/resources/i18n")))
        {
            QApplication::installTranslator(translator);
            translationLoaded = true;
        }
    }
    QSettings settings;
    settings.setValue("language", language);
}

void MainWindow::getBinaryIconPtr(u8*& ncg, u16*& ncl, int bmpID, int palID)
{
    if (bmpID == -1)
    {
        ncg = this->bannerBin.iconNCG;
        ncl = this->bannerBin.iconNCL;
    }
    else
    {
        ncg = this->bannerBin.iconExtraNCG[bmpID];
        ncl = this->bannerBin.iconExtraNCL[palID];
    }
}

QImage MainWindow::getCurrentImage(int bmpID, int palID)
{
    u8* ncg;
    u16* ncl;
    getBinaryIconPtr(ncg, ncl, bmpID, palID);

    QVector<u8> ncgV = QVector<u8>(ncg, ncg + 0x200);
    QVector<u16> nclV = QVector<u16>(ncl, ncl + 0x10);

    QNDSImage ndsImg(ncgV, nclV, true);
    return ndsImg.toImage(4);
}

QPixmap MainWindow::getCurrentPixmap(int bmpID, int palID)
{
    return QPixmap::fromImage(getCurrentImage(bmpID, palID));
}

void MainWindow::updateIconView(int bmpID, int palID)
{
    QPixmap pixmap = getCurrentPixmap(bmpID, palID);

    gfx_scene.clear();
    gfx_scene.addPixmap(pixmap);
}

bool MainWindow::checkIfAllowClose()
{
    //If possible, check if file was modified
    QFile openFile(this->openedFileName);
    if(openFile.open(QIODevice::ReadOnly))
    {
        int bannerSize;
        if(this->bannerBin.version & (1 << 8))
            bannerSize = sizeof(NDSBanner);
        else if((this->bannerBin.version & 3) == 3)
            bannerSize = 0xA40;
        else if(this->bannerBin.version & 2)
            bannerSize = 0x940;
        else
            bannerSize = 0x840;

        QByteArray fileData = openFile.readAll();
        bool isDifferent = memcmp(fileData.data(), &this->bannerBin, bannerSize);
        openFile.close();

        if(isDifferent)
        {
            QMessageBox::StandardButton btn = QMessageBox::question(this, tr("You sure?"), tr("There are unsaved changes!\nAre you sure you want to close?"));
            if(btn == QMessageBox::No)
                return false;
        }
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    checkIfAllowClose() ? event->accept() : event->ignore();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    if(mimeData->hasUrls())
    {
        QStringList paths;
        QList<QUrl> urls = mimeData->urls();

        // Try to load the dropped files
        for (int i = 0; i < urls.size() && i < 32; ++i) {
            if (loadFile(urls[i].toLocalFile(), false))
                break;
        }
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if(QEvent::LanguageChange == event->type())
    {
        ui->retranslateUi(this);
    }
}

/* ======== MENU BAR ACTIONS GROUP ======== */

void MainWindow::on_actionNew_triggered()
{
    loadFile(":/resources/default.bin", true);
}

void MainWindow::on_actionOpen_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, "", this->lastDirPath, tr("Banner Files") + " (*.bin)");
    if(fileName == "")
        return;

    loadFile(fileName, false);
}

bool MainWindow::loadFile(const QString& path, bool isNew)
{
    if(!isNew)
    {
        this->openedFileName = path;

        QFileInfo fileInfo(path);
        this->lastDirPath = fileInfo.dir().path();

        if(fileInfo.size() != sizeof(NDSBanner) && fileInfo.size() != 0x840 && fileInfo.size() != 0x940 && fileInfo.size() != 0xA40)
        {
            QMessageBox::information(this, tr("Oops!"), tr("Invalid banner size.\nMake sure this is a valid banner file."));
            return false;
        }
    }

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, tr("Big OOF"), tr("Could not open file for reading."));
        return false;
    }

    // Check if valid banner.bin size
    size_t fileSize = file.size();
    if(fileSize != sizeof(NDSBanner) && fileSize != 0x840 && fileSize != 0x940 && fileSize != 0xA40)
        return false;

    QByteArray fileData = file.readAll();

    memset(&this->bannerBin, 0, sizeof(NDSBanner));
    memcpy(&this->bannerBin, fileData.data(), fileSize);
    file.close();

    setProgramState(isNew ? ProgramState::NewFile : ProgramState::KnowsPath);

    /* ======== ICON SETUP ======== */

    updateIconView(-1, 0);

    /* ======== TEXT SETUP ======== */

    on_gameTitle_cb_currentIndexChanged(0);

    /* ======== VERSION SETUP ======== */

    if(this->bannerBin.version & (1 << 8))
        ui->bannerVersion_cb->setCurrentIndex(3);
    else if((this->bannerBin.version & 3) == 3)
        ui->bannerVersion_cb->setCurrentIndex(2);
    else if(this->bannerBin.version & 2)
        ui->bannerVersion_cb->setCurrentIndex(1);
    else
        ui->bannerVersion_cb->setCurrentIndex(0);

    /* ======== ANIMATION SETUP ======== */

    for(int i = 0; i < 64; i++)
    {
        NDSBanner::AnimSeq* animData = &this->bannerBin.animData[i];
        if(animData->frameDuration)
            ui->animFrame_cb->addItem(tr("Frame %0").arg(i + 1));
        else
            break;
    }
    if(this->bannerBin.animData[0].frameDuration)
        setAnimGroupEnabled(true);
    on_animFrame_cb_currentIndexChanged(0);

    return true;
}

void MainWindow::saveFile(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, tr("Big OOF"), tr("Could not open file for writing."));
        return;
    }

    u8* u16BannerBin = reinterpret_cast<u8*>(&this->bannerBin);

    this->bannerBin.crc[0] = crc16(&u16BannerBin[0x20], 0x840 - 0x20);
    this->bannerBin.crc[1] = (this->bannerBin.version & 2) ? crc16(&u16BannerBin[0x20], 0x940 - 0x20) : 0;
    this->bannerBin.crc[2] = ((this->bannerBin.version & 3) == 3) ? crc16(&u16BannerBin[0x20], 0xA40 - 0x20) : 0;
    this->bannerBin.crc[3] = (this->bannerBin.version & (1 << 8)) ? crc16(&u16BannerBin[0x1240], 0x23C0 - 0x1240) : 0;

    int bannerSize;
    if(this->bannerBin.version & (1 << 8))
        bannerSize = sizeof(NDSBanner);
    else if((this->bannerBin.version & 3) == 3)
        bannerSize = 0xA40;
    else if(this->bannerBin.version & 2)
        bannerSize = 0x940;
    else
        bannerSize = 0x840;

    QByteArray out(bannerSize, 0);
    memcpy(out.data(), &this->bannerBin, bannerSize);

    file.write(out);
    file.close();
}

void MainWindow::on_actionSave_triggered() {
    saveFile(this->openedFileName);
}

void MainWindow::on_actionSave_As_triggered()
{
    QString fileName = QFileDialog::getSaveFileName(this, "", this->lastDirPath, tr("Banner Files") + " (*.bin)");
    if(fileName == "")
        return;
    this->openedFileName = fileName;
    QFileInfo fileInfo(fileName);
    this->lastDirPath = fileInfo.dir().path();

    setProgramState(ProgramState::KnowsPath);

    saveFile(fileName);
}

void MainWindow::on_actionClose_triggered()
{
    if(!checkIfAllowClose())
        return;

    //Clear everything
    setProgramState(ProgramState::Closed);
}

void MainWindow::on_actionCredits_triggered()
{
    QString bodyText = "<p><string>" + tr("Nintendo DS Banner Editor") + "</strong></p>"
        + "<p>" + tr("Copyright &copy; 2020-2023 TheGameratorT") + "</p>"
        + R"(<p><span style="text-decoration: underline;">)" + tr("Special thanks:") + "</span></p>"
        + R"(<p style="padding-left: 30px;">)"
        + tr(R"(Banner format research by <a href="https://problemkaputt.de/gbatek-ds-cartridge-icon-title.htm">GBATEK</a>)") + "<br />"
        + tr(R"(Image conversion by <a href="https://github.com/Ed-1T">Ed_IT</a>)") + "<br />"
        + tr(R"(Development contribution and Japanese translation by <a href="https://github.com/Epicpkmn11">Epicpkmn11</a>)") + "</p>"
        + R"(<p><span style="text-decoration: underline;">)" + tr("License:") + "</span></p>"
        + R"(<p style="padding-left: 30px;">This application is licensed under the GNU General Public License v3.</p>)"
        + R"(<p style="padding-left: 30px;">This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.</p>)"
        + R"(<p style="padding-left: 30px;">This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.<br />See the GNU General Public License for more details.</p>)"
        + R"(<p style="padding-left: 30px;">For details read the LICENSE file bundled with the program or visit:</p>)"
        + R"(<p style="padding-left: 30px;"><a href="https://www.gnu.org/licenses/">https://www.gnu.org/licenses/</a></p>)";

    QMessageBox::about(this, tr("About NDS Banner Editor"), bodyText);
}

void MainWindow::on_actionQt_triggered()
{
    QMessageBox::aboutQt(this);
}

void MainWindow::on_actionEnglish_triggered()
{
    changeLanguage(0);
}

void MainWindow::on_actionJapanese_triggered()
{
    changeLanguage(1);
}

void MainWindow::on_actionPortugu_s_triggered()
{
    changeLanguage(2);
}

/* ======== ICON/GRAPHICS GROUP ======== */

void MainWindow::on_gfxBmp_sb_valueChanged(int arg1)
{
    if(arg1 == 0)
    {
        ui->gfxPal_sb->blockSignals(true);
        ui->gfxPal_sb->setMinimum(0);
        ui->gfxPal_sb->setValue(0);
        ui->gfxPal_sb->setEnabled(false);
    }
    else if (this->gfxBmp_lastValue == 0)
    {
        ui->gfxPal_sb->blockSignals(false);
        ui->gfxPal_sb->setMinimum(1);
        ui->gfxPal_sb->setEnabled(true);
    }

    int bmpID = arg1 - 1;
    int palID = ui->gfxPal_sb->value() - 1;
    updateIconView(bmpID, palID);

    this->gfxBmp_lastValue = arg1;
}

void MainWindow::on_gfxPal_sb_valueChanged(int arg1)
{
    int bmpID = ui->gfxBmp_sb->value() - 1;
    int palID = arg1 - 1;
    updateIconView(bmpID, palID);
}

/* ======== GAME TITLE GROUP ======== */

void MainWindow::on_gameTitle_pte_textChanged()
{
    QString txt = ui->gameTitle_pte->toPlainText();
    if(txt.length() > 0x80)
    {
        txt.resize(0x80);
        ui->gameTitle_pte->setPlainText(txt);
    }

    int current = ui->gameTitle_cb->currentIndex();

    int bytes = txt.length() * 2;
    int chars = bytes / 2;
    memcpy(this->bannerBin.title[current], txt.data(), bytes);
    memset(&this->bannerBin.title[current][chars], 0, 0x100 - bytes);
}

void MainWindow::on_gameTitle_cb_currentIndexChanged(int index) {
    ui->gameTitle_pte->setPlainText(QString(this->bannerBin.title[index]));
}

void MainWindow::on_gameTitle_pb_clicked()
{
    QMessageBox::StandardButton btn = QMessageBox::question(this, tr("You sure?"), tr("Do you really want to replace all language titles with the current one?"));
    if(btn == QMessageBox::No)
        return;

    int current = ui->gameTitle_cb->currentIndex();

    for(int i = 0; i < 8; i++)
    {
        if(i == current)
            continue;

        memcpy(this->bannerBin.title[i], this->bannerBin.title[current], 0x100);
    }
}

/* ======== BANNER VERSION GROUP ======== */

void MainWindow::on_bannerVersion_cb_currentIndexChanged(int index)
{
    constexpr int bannerVersions[] = {
        0x0001, // Normal
        0x0002, // Chinese
        0x0003, // Korean
        0x0103  // DSi
    };

    this->bannerBin.version = bannerVersions[index];

    // Block animation settings if not DSi banner
    bool dsiBanner = this->bannerBin.version & (1 << 8);
    ui->anim_gb->setEnabled(dsiBanner);

    // Only banner 0 exists for non-DSi banners
    if(!dsiBanner)
        ui->gfxBmp_sb->setValue(0);
    ui->gfxBmp_sb->setEnabled(dsiBanner);

    // Ensure the the selected language is valid for this version
    int maxLanguage;
    if((this->bannerBin.version & 3) == 3)
        maxLanguage = 7;
    else if((this->bannerBin.version & 2))
        maxLanguage = 6;
    else
        maxLanguage = 5;

    if(ui->gameTitle_cb->currentIndex() > maxLanguage) {
        ui->gameTitle_cb->setCurrentIndex(0);
    }
    QStandardItemModel *model = qobject_cast<QStandardItemModel *>(ui->gameTitle_cb->model());
    Q_ASSERT(model != nullptr);
    for(int i = 6; i < 8; i++)
        model->item(i)->setEnabled(i <= maxLanguage);
}

/* ======== ANIMATION GROUP ======== */

void MainWindow::setAnimGroupBlockSignals(bool flag)
{
    ui->animFrame_cb->blockSignals(flag);
    ui->animDur_sb->blockSignals(flag);
    ui->animBmp_sb->blockSignals(flag);
    ui->animPal_sb->blockSignals(flag);
    ui->animFlipX_cb->blockSignals(flag);
    ui->animFlipY_cb->blockSignals(flag);
}

void MainWindow::setAnimGroupEnabled(bool flag)
{
    ui->animFrame_cb->setEnabled(flag);
    ui->animFrameRem_pb->setEnabled(flag);

    ui->animDur_label->setEnabled(flag);
    ui->animBmp_label->setEnabled(flag);
    ui->animPal_label->setEnabled(flag);
    ui->animFlip_label->setEnabled(flag);

    int val = flag ? 1 : 0;
    ui->animDur_sb->setMinimum(val);
    ui->animBmp_sb->setMinimum(val);
    ui->animPal_sb->setMinimum(val);

    if(!flag)
    {
        ui->animDur_sb->setValue(0);
        ui->animBmp_sb->setValue(0);
        ui->animPal_sb->setValue(0);
        ui->animFlipX_cb->setChecked(false);
        ui->animFlipY_cb->setChecked(false);
    }

    ui->animDur_sb->setEnabled(flag);
    ui->animBmp_sb->setEnabled(flag);
    ui->animPal_sb->setEnabled(flag);
    ui->animFlipX_cb->setEnabled(flag);
    ui->animFlipY_cb->setEnabled(flag);
}

void MainWindow::on_animFrame_cb_currentIndexChanged(int index)
{
    setAnimGroupBlockSignals(true);

    NDSBanner::AnimSeq* animData = &this->bannerBin.animData[index];
    ui->animDur_sb->setValue(animData->frameDuration);
    ui->animBmp_sb->setValue(animData->ncgID + 1);
    ui->animPal_sb->setValue(animData->nclID + 1);
    ui->animFlipX_cb->setChecked(animData->flipH);
    ui->animFlipY_cb->setChecked(animData->flipV);

    setAnimGroupBlockSignals(false);
}

void MainWindow::on_animFrameAdd_pb_clicked()
{
    int index = ui->animFrame_cb->count();
    if(index != 64)
    {
        ui->animFrame_cb->addItem(tr("Frame %0").arg(index + 1));

        NDSBanner::AnimSeq* animData = &this->bannerBin.animData[index];
        animData->frameDuration = 1;

        if(this->animFrame_lastSize == 0)
        {
            setAnimGroupBlockSignals(true);
            setAnimGroupEnabled(true);
            setAnimGroupBlockSignals(false);
        }

        this->animFrame_lastSize = index + 1;
    }
}

void MainWindow::on_animFrameRem_pb_clicked()
{
    int index = ui->animFrame_cb->currentIndex();
    ui->animFrame_cb->removeItem(index);

    int new_count = ui->animFrame_cb->count();
    if(new_count != 0)
    {
        for(int i = index; i < new_count; i++)
        {
            ui->animFrame_cb->setItemText(i, tr("Frame %0").arg(i + 1));
            this->bannerBin.animData[i] = this->bannerBin.animData[i + 1];
        }

        int offset = index == new_count ? 1 : 0;
        on_animFrame_cb_currentIndexChanged(index - offset);
    }
    else
    {
        setAnimGroupBlockSignals(true);
        setAnimGroupEnabled(false);
        setAnimGroupBlockSignals(false);
    }

    *reinterpret_cast<u16*>(&this->bannerBin.animData[new_count]) = 0; //Clear last entry
    this->animFrame_lastSize = new_count;
}

void MainWindow::on_animDur_sb_valueChanged(int arg1)
{
    int index = ui->animFrame_cb->currentIndex();
    this->bannerBin.animData[index].frameDuration = arg1;
}

void MainWindow::on_animBmp_sb_valueChanged(int arg1)
{
    int index = ui->animFrame_cb->currentIndex();
    this->bannerBin.animData[index].ncgID = arg1 - 1;
}

void MainWindow::on_animPal_sb_valueChanged(int arg1)
{
    int index = ui->animFrame_cb->currentIndex();
    this->bannerBin.animData[index].nclID = arg1 - 1;
}

void MainWindow::on_animFlipX_cb_stateChanged(int arg1)
{
    int index = ui->animFrame_cb->currentIndex();
    this->bannerBin.animData[index].flipH = arg1 == Qt::Checked;
}

void MainWindow::on_animFlipY_cb_stateChanged(int arg1)
{
    int index = ui->animFrame_cb->currentIndex();
    this->bannerBin.animData[index].flipV = arg1 == Qt::Checked;
}

void MainWindow::on_actionAnimation_Player_triggered()
{
    int frameCount = ui->animFrame_cb->count();
    if(frameCount != 0)
    {
        NDSBannerAnimPlayer animPlayer(this, &this->bannerBin);
        animPlayer.exec();
    }
    else
    {
        QMessageBox::information(this, tr("Oops!"), tr("This banner has no frames yet."));
    }
}

void MainWindow::on_gfxImport_pb_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "", this->lastDirPath, tr("PNG Files") + " (*.png)");
    if(fileName == "")
        return;
    QFileInfo fileInfo(fileName);
    this->lastDirPath = fileInfo.dir().path();

    importImage(fileName);
}

void MainWindow::on_gfxExport_pb_clicked()
{
    QImage image = exportImage();

    QString fileName = QFileDialog::getSaveFileName(this, "", this->lastDirPath, tr("PNG Files") + " (*.png)");
    if(fileName == "")
        return;
    QFileInfo fileInfo(fileName);
    this->lastDirPath = fileInfo.dir().path();

    QFile file(fileName);
    if(!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "Big OOF", tr("Could not open file for writing."));
        return;
    }
    image.save(&file, "PNG");
    file.close();
}

bool MainWindow::importImage(const QString &fileName)
{
    QImage img(fileName);
    if(img.width() != 32 || img.height() != 32)
    {
        if(img.width() != 0 && img.height() != 0) // Only show error if given a real image
            QMessageBox::critical(this, tr("faTal mEga eRrOR"), tr("Unfortunately??\nyes, unfortunately, the imported image is not 32x32 pixels."));
        return false;
    }

    int bmpID = ui->gfxBmp_sb->value() - 1;
    int palID = ui->gfxPal_sb->value() - 1;

    u8* ncg;
    u16* ncl;
    getBinaryIconPtr(ncg, ncl, bmpID, palID);

    bool newPalette = true;
    if(bmpID != -1)
    {
        QMessageBox::StandardButton btn = QMessageBox::question(this, tr("Palette Replacement"), tr("Do you with to recreate the selected palette?"));
        if(btn == QMessageBox::No)
            newPalette = false;
    }

    QNDSImage ndsImg;

    if (newPalette)
    {
        ndsImg.replace(img, 16, 0x80);
    }
    else
    {
        QVector<u16> pltt = QVector<u16>(ncl, ncl + 0x10);
        ndsImg.replace(img, pltt, 0x80);
    }

    QVector<u8> ncgV;
    QVector<u16> nclV;
    ndsImg.toNitro(ncgV, nclV, true);
    memcpy(ncg, ncgV.data(), 0x200);
    memcpy(ncl, nclV.data(), 0x20);

    updateIconView(bmpID, palID);

    return true;
}

QImage MainWindow::exportImage()
{
    int bmpID = ui->gfxBmp_sb->value() - 1;
    int palID = ui->gfxPal_sb->value() - 1;
    return getCurrentImage(bmpID, palID);
}

int MainWindow::getSelectedBitmapID()
{
    return ui->gfxBmp_sb->value();
}

int MainWindow::getSelectedPaletteID()
{
    return ui->gfxPal_sb->value();
}

/* ======== GRAPHICS VIEW ======== */

extern MainWindow *w;

void IconGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void IconGraphicsView::dropEvent(QDropEvent *event)
{
    if (!w)
        return;

    const QMimeData *mimeData = event->mimeData();

    if(mimeData->hasUrls())
    {
        QList<QUrl> urls = mimeData->urls();

        // Try to load the dropped files
        for (int i = 0; i < urls.size() && i < 32; ++i) {
            if (w->importImage(urls[i].toLocalFile()))
                break;
        }
    }
}

void IconGraphicsView::mousePressEvent(QMouseEvent *event)
{
    this->clicked = (event->button() == Qt::LeftButton);
}
void IconGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    (void)event; // ignore event
    this->clicked = false;
}

void IconGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    (void)event; // ignore event
    if (!w || !clicked)
        return;

    clicked = false; // Prevent double triggering

    QImage image = w->exportImage();
    int bmpID = w->getSelectedBitmapID();
    int palID = w->getSelectedPaletteID();

    QString tempFileName = QDir::tempPath() + "/BannerIcon_BMP" + QString::number(bmpID) + "_PAL" + QString::number(palID) + ".png";
    QFile tempFile(tempFileName);
    if (!tempFile.open(QIODevice::WriteOnly))
        return;
    image.save(&tempFile, "PNG");
    tempFile.close();

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    mimeData->setUrls({QUrl::fromLocalFile(tempFileName)});

    drag->setMimeData(mimeData);
    drag->setPixmap(QPixmap::fromImage(image));

    Qt::DropAction dropAction = drag->exec(Qt::MoveAction);
    if(dropAction == Qt::IgnoreAction)
        tempFile.remove();
}

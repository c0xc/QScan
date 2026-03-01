/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (p@c0xc.net)
** This file is part of QScan.
**
** QScan is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** QScan is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with QScan. If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>
#include <QMessageBox>
#include <QTimer>
#include <QVector>

#include "gui/mainwindow.hpp"
#include "core/profilesettings.hpp"
#include "scan/document_size.hpp"

namespace
{

static QString
formatPx(const QSize &px)
{
    if (!px.isValid() || px.isEmpty())
        return QString();

    return QStringLiteral("%1×%2 px").arg(px.width()).arg(px.height());
}

static QString
formatMmCompact(const QSizeF &mm)
{
    if (mm.isEmpty())
        return QString();

    return QStringLiteral("%1×%2 mm")
        .arg(mm.width(), 0, 'f', 0)
        .arg(mm.height(), 0, 'f', 0);
}

static ScanParameters
computeSingleImagePreviewParams(ScanSource *source)
{
    ScanParameters preview_params;
    //Preview should be fast; prefer 75 DPI gray, but some devices expose a restricted set
    preview_params.resolution = 75;
    preview_params.color_mode = QStringLiteral("Gray");
    preview_params.auto_page_size = true;

    if (!source)
        return preview_params;

    const ScanCapabilities caps = source->capabilities();

    if (!caps.supported_resolutions.isEmpty())
    {
        int chosen_res = preview_params.resolution;
        if (!caps.supported_resolutions.contains(chosen_res))
        {
            //Prefer the smallest supported resolution for speed
            chosen_res = caps.supported_resolutions.first();
            for (int r : caps.supported_resolutions)
                chosen_res = (r < chosen_res ? r : chosen_res);
        }
        preview_params.resolution = chosen_res;
    }

    if (!caps.supported_color_modes.isEmpty() && !caps.supported_color_modes.contains(preview_params.color_mode))
    {
        //Prefer Gray if present, otherwise take first advertised mode
        QString chosen_mode = caps.supported_color_modes.first();
        if (caps.supported_color_modes.contains(QStringLiteral("Gray")))
            chosen_mode = QStringLiteral("Gray");
        preview_params.color_mode = chosen_mode;
    }

    return preview_params;
}

} //namespace

MainWindow::MainWindow(ScanSource *source, QWidget *parent)
          : QMainWindow(parent),
            m_scan_source(source),
            m_document(0),
            m_exporter(0),
            m_border_detector(0),
            m_crop_processor(0),
            m_rotate_processor(0),
            m_smart_capture_processor(0),
            m_preview(0),
            m_page_list(0),
            m_control_panel(0),
            m_splitter(0),
            m_toolbar(0),
            m_status_bar(0),
            m_scanner_status_label(0),
            m_page_status_label(0),
            m_size_status_label(0),
            m_crop_mode(ScanControlPanel::CropOff),
            m_current_page_index(-1),
            m_last_preview_crop_rect_norm(QRectF(0.0, 0.0, 1.0, 1.0)),
            m_scan_session_base_index(0),
            m_scan_session_precreated_blank_index(-1),
            m_scan_session_has_received_page(false),
            m_scan_session_params(),
            m_scan_session_params_valid(false),
            m_branding_label(0)
{
    setWindowTitle(tr("QScan"));
    resize(1200, 700);

    //Create document
    m_document = new Document(this);

    //Create exporter
    m_exporter = new DocumentExporter;

    //Create processors
    m_border_detector = new BorderDetector;
    m_crop_processor = new CropProcessor;
    m_rotate_processor = new RotateProcessor;
    m_smart_capture_processor = new SmartCaptureProcessor;

    //Setup UI
    setupUi();
    createActions();
    createToolbar();

    //Update status bar
    updateStatusBar();

    //Connect scan source signals if available
    if (m_scan_source)
    {
        connect(m_scan_source, SIGNAL(pageScanned(const QImage&, int, const qscan::ScanPageInfo&)),
            this, SLOT(onPageScanned(const QImage&, int, const qscan::ScanPageInfo&)));
        connect(m_scan_source, &ScanSource::scanStarted, this, [this]()
        {
            if (m_act_scan) m_act_scan->setEnabled(false);
            if (m_act_preview) m_act_preview->setEnabled(false);
            if (m_act_cancel) m_act_cancel->setEnabled(true);

            if (m_scanner_status_label)
                m_scanner_status_label->setText(tr("\u231b Scanning..."));
        });
        connect(m_scan_source, SIGNAL(scanComplete()),
                this, SLOT(onScanComplete()));
        connect(m_scan_source, SIGNAL(scanCanceled()),
            this, SLOT(onScanCanceled()));
        connect(m_scan_source, SIGNAL(scanError(const QString&)),
                this, SLOT(onScanError(const QString&)));
        connect(m_scan_source, &ScanSource::scanCancelRequested, this, [this]()
        {
            if (m_act_cancel) m_act_cancel->setEnabled(false);
        });
        connect(m_scan_source, SIGNAL(scanStatusMessage(const QString&)),
            this, SLOT(onScanStatusMessage(const QString&)));
        connect(m_scan_source, SIGNAL(progressChanged(int)),
                this, SLOT(onProgressChanged(int)));
        connect(m_scan_source, SIGNAL(previewFrameReady(const QImage&)),
                this, SLOT(onPreviewFrameReady(const QImage&)));
    }
}

MainWindow::~MainWindow()
{
    //Stop live preview if active
    if (m_scan_source && m_scan_source->isPreviewActive())
    {
        m_scan_source->stopPreview();
    }

    delete m_exporter;
    delete m_border_detector;
    delete m_crop_processor;
    delete m_rotate_processor;
    delete m_smart_capture_processor;
}

void
MainWindow::updateCameraWarp(ScannedPage &page)
{
    //Warp mode is for camera input only
    if (page.backendKind() != QStringLiteral("Webcam"))
        return;

    //Get raw source image
    const QImage base = page.hasOriginalImage() ? page.originalImage() : page.rawImage();
    if (base.isNull())
        return;

    //Crop Off => show original (unwarped) image
    if (m_crop_mode == ScanControlPanel::CropOff)
    {
        if (page.rawImage() != base)
            page.setImage(base);
        return;
    }

    //No manual crop (polygon crop not supported)
    if (m_crop_mode != ScanControlPanel::CropAuto)
        return;

    //Reuse cached warp if already computed
    if (page.hasWarpedImage())
    {
        if (page.rawImage() != page.warpedImage())
            page.setImage(page.warpedImage());
        return;
    }

    //TODO smart? open!
    if (!m_smart_capture_processor || !m_smart_capture_processor->isAvailable())
    {
        if (page.rawImage() != base)
            page.setImage(base);
        return;
    }

    //Detect perspective-warped document
    const DetectedRegion region = m_smart_capture_processor->detectDocument(base);
    if (!region.isValid())
    {
        if (page.rawImage() != base)
            page.setImage(base);
        return;
    }

    //Extract perspective-warped document
    const QImage warped = m_smart_capture_processor->extractDocument(base, region);
    if (warped.isNull())
    {
        if (page.rawImage() != base)
            page.setImage(base);
        return;
    }

    page.setWarpedImage(warped);
    page.setImage(warped);
}

void
MainWindow::onScanClicked()
{
    if (!m_scan_source)
    {
        QMessageBox::warning(this, tr("No Scanner"),
            tr("No scanner connected."));
        return;
    }

    //Stop live preview before scanning and uncheck button
    if (m_scan_source->isPreviewActive())
    {
        m_scan_source->stopPreview();
        m_act_preview->setChecked(false);
        m_act_preview->setText(tr("&Preview"));
    }

    //Get scan parameters from control panel
    ScanParameters params = m_control_panel->getScanParameters();

    //Start a new scan session
    m_scan_session_params = params;
    m_scan_session_params_valid = true;
    m_scan_session_has_received_page = false;

    //Each manual (non-ADF) scan creates a new page immediately and selects it
    if (!params.use_adf)
    {
        ScannedPage placeholder;
        if (m_scan_source)
        {
            placeholder.setSourceName(m_scan_source->deviceName());
            placeholder.setSourceDescription(m_scan_source->deviceDescription());
        }
        placeholder.setScanResolutionDpi(params.resolution);
        placeholder.setScanColorMode(params.color_mode);
        placeholder.setAutoPageSize(params.auto_page_size);
        placeholder.setScanAreaMm(params.scan_area);
        placeholder.setUsedAdf(params.use_adf);
        placeholder.setUsedDuplex(params.use_duplex);
        placeholder.setAcquisitionKind(ScannedPage::AcquisitionKind::Scan);

        m_document->addPage(placeholder);
        const int idx = m_document->pageCount() - 1;
        m_scan_session_base_index = idx;
        m_scan_session_precreated_blank_index = idx;

        selectPage(idx);
        m_preview->showPlaceholder();

        if (m_document->pageCount() >= 2)
            showPageList();
    }
    else
    {
        //ADF scan: pages will be appended as they arrive
        m_scan_session_base_index = m_document->pageCount();
        m_scan_session_precreated_blank_index = -1;
    }

    if (m_scanner_status_label)
        m_scanner_status_label->setText(tr("\u231b Scanning..."));
    else
        statusBar()->showMessage(tr("Scanning..."));
    m_scan_source->startScan(params);
}

void
MainWindow::onSaveClicked()
{
    if (m_document->pageCount() == 0)
    {
        QMessageBox::warning(this, tr("No Pages"),
            tr("No pages to save. Scan something first."));
        return;
    }

    //Pick output filename
    QString filename = getSaveFileName();
    if (filename.isEmpty())
        return;

    //Default extension
    QString suffix = QFileInfo(filename).suffix().trimmed().toLower();
    if (suffix.isEmpty())
    {
        //Default to PDF
        if (filename.endsWith(QLatin1Char('.')))
            filename.chop(1);

        filename += QStringLiteral(".pdf");
        suffix = QStringLiteral("pdf");
    }

    //Select exporter module
    DocumentExporter::ExportFormat format;
    if (suffix == QStringLiteral("pdf"))
        format = DocumentExporter::PDF;
    else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        format = DocumentExporter::JPG;
    else
    {
        QMessageBox::warning(this, tr("Save"),
            tr("Unsupported file type: %1").arg(suffix));
        return;
    }
    if (!m_exporter)
    {
        QMessageBox::critical(this, tr("Save"), tr("Exporter not available."));
        return;
    }

    //Call exporter - multipage (if supported) or current page
    bool ok = false;
    if (format == DocumentExporter::PDF)
    {
        ok = m_exporter->exportDocument(*m_document, filename, format);
    }
    else
    {
        int idx = m_current_page_index;
        if (idx < 0 && m_document->pageCount() > 0)
            idx = 0;

        if (idx < 0 || idx >= m_document->pageCount())
        {
            QMessageBox::warning(this, tr("Save"), tr("No page selected."));
            return;
        }

        ok = m_exporter->exportSingleImage(m_document->page(idx), filename, format);
    }

    //Export failure dialog
    if (!ok)
    {
        const QString err = m_exporter->lastError();
        QMessageBox::critical(this, tr("Save Failed"), err.isEmpty() ? tr("Unknown error") : err);
        return;
    }

    //Export success dialog
    QMessageBox::information(this, tr("Save"), tr("Saved successfully."));
}

void
MainWindow::onQuitClicked()
{
    close();
}

void
MainWindow::onPageScanned(const QImage &image, int page_number, const qscan::ScanPageInfo &page_info)
{
    bool autocrop_was_applied = false;

    m_scan_session_has_received_page = true;

    const int target_index = m_scan_session_base_index + page_number;

    //Ensure the target index exists (ADF pages arrive during a single scan)
    while (m_document->pageCount() <= target_index)
    {
        ScannedPage placeholder;
        placeholder.setAcquisitionKind(ScannedPage::AcquisitionKind::Scan);
        if (m_scan_source)
        {
            placeholder.setSourceName(m_scan_source->deviceName());
            placeholder.setSourceDescription(m_scan_source->deviceDescription());
        }
        if (m_scan_session_params_valid)
        {
            placeholder.setScanResolutionDpi(m_scan_session_params.resolution);
            placeholder.setScanColorMode(m_scan_session_params.color_mode);
            placeholder.setAutoPageSize(m_scan_session_params.auto_page_size);
            placeholder.setScanAreaMm(m_scan_session_params.scan_area);
            placeholder.setUsedAdf(m_scan_session_params.use_adf);
            placeholder.setUsedDuplex(m_scan_session_params.use_duplex);
        }
        m_document->addPage(placeholder);
    }

    //Warp control: Store original camera frame
    ScannedPage &page = m_document->page(target_index);
    const bool is_webcam = (page_info.backend_kind == QStringLiteral("Webcam"));
    if (is_webcam)
    {
        page.setOriginalImage(image);
        page.setWarpedImage(QImage());
    }

    //Store image in page object
    page.setImage(image);
    page.setScanTime(QDateTime::currentDateTime());
    page.setAcquisitionKind(ScannedPage::AcquisitionKind::Scan);
    page.setAutoCropApplied(false);
    page.setCropRect(QRectF(0.0, 0.0, 1.0, 1.0));

    //Store metadata
    page.setBackendKind(page_info.backend_kind);
    page.setBackendDetails(page_info.backend_details);
    if (page_info.has_effective_resolution_dpi)
        page.setEffectiveResolutionDpi(page_info.effective_resolution_dpi);
    if (page_info.has_effective_color_mode)
        page.setEffectiveColorMode(page_info.effective_color_mode);
    if (m_scan_source)
    {
        page.setSourceName(m_scan_source->deviceName());
        page.setSourceDescription(m_scan_source->deviceDescription());

        if (page_info.has_reported_paper_size)
        {
            page.setReportedDocumentSizeMm(page_info.reported_paper_size_mm, page_info.reported_paper_name);
        }
        else
        {
            const ScanSource::ReportedDocumentSize rep = m_scan_source->reportedDocumentSize();
            if (rep.valid)
                page.setReportedDocumentSizeMm(rep.mm_size, rep.paper_name);
        }
    }
    if (m_scan_session_params_valid)
    {
        page.setScanResolutionDpi(m_scan_session_params.resolution);
        page.setScanColorMode(m_scan_session_params.color_mode);
        page.setAutoPageSize(m_scan_session_params.auto_page_size);
        page.setScanAreaMm(m_scan_session_params.scan_area);
        page.setUsedAdf(m_scan_session_params.use_adf);
        page.setUsedDuplex(m_scan_session_params.use_duplex);
    }

    //Auto mode: use warped image for camera pages
    if (is_webcam)
        updateCameraWarp(page);

    //Auto-crop: store normalized crop rect on page
    if (m_crop_mode == ScanControlPanel::CropAuto)
    {
        //Detect crop rect on the displayed image
        const QImage display = page.currentImage();
        const QRectF crop_norm = detectAutoCropRectNorm(display);
        if (crop_norm != QRectF(0.0, 0.0, 1.0, 1.0))
        {
            page.setCropRect(crop_norm);
            page.setAutoCropApplied(true);
            autocrop_was_applied = true;

            if (m_control_panel)
                m_control_panel->setAutoCropWarning(QString(), false);
        }
        else
        {
            if (m_control_panel)
                m_control_panel->setAutoCropWarning(tr("No border detected"), true);
            statusBar()->showMessage(tr("Auto-crop: no border detected"), 3500);
        }
    }

    page.setAutoCropApplied(autocrop_was_applied);

    //Thumbnails and selection state
    m_page_list->refresh();
    selectPage(target_index);

    //Preview uses full image plus crop overlay
    showPreviewWithCrop(page.currentImage(), page.cropRect());

    //Show page list (2+ pages)
    if (m_document->pageCount() >= 2)
    {
        showPageList();
    }

    //Update size label: show configured area + reported doc size (if any) + received image pixels
    if (m_size_status_label)
    {
        QStringList parts;

        QString area = tr("N/A");
        if (m_scan_source && m_scan_source->capabilities().supports_scan_settings)
        {
            const ScanParameters params = m_control_panel ? m_control_panel->getScanParameters() : ScanParameters();
            if (params.auto_page_size)
            {
                area = tr("Auto");
            }
            else if (!params.scan_area.isEmpty())
            {
                const QString paper = documentSizePaperNameForMm(params.scan_area);
                area = paper.isEmpty() ? formatMmCompact(params.scan_area) : paper;
            }
        }
        parts << tr("Area: %1").arg(area);

        QString doc = tr("N/A");
        if (m_scan_source)
        {
            const ScanSource::ReportedDocumentSize rep = m_scan_source->reportedDocumentSize();
            if (rep.valid && !rep.mm_size.isEmpty())
            {
                if (!rep.paper_name.isEmpty())
                    doc = tr("%1 (reported) %2").arg(rep.paper_name, formatMmCompact(rep.mm_size));
                else
                    doc = tr("%1 (reported)").arg(formatMmCompact(rep.mm_size));
            }
        }
        parts << tr("Doc: %1").arg(doc);

        parts << tr("Img: %1").arg(formatPx(image.size()));
        m_size_status_label->setText(parts.join(tr(" · ")));
    }

    statusBar()->showMessage(tr("Page %1 scanned").arg(target_index + 1), 3000);
    updatePageStatusLabel();
}

void
MainWindow::onScanComplete()
{
    statusBar()->showMessage(tr("Scan complete"), 3000);

    m_scan_session_precreated_blank_index = -1;
    m_scan_session_has_received_page = false;
    m_scan_session_params_valid = false;

    if (m_act_cancel) m_act_cancel->setEnabled(false);
    if (m_act_scan) m_act_scan->setEnabled(true);
    if (m_act_preview) m_act_preview->setEnabled(true);

    updateStatusBar();
}

void
MainWindow::onScanCanceled()
{
    statusBar()->showMessage(tr("Scan canceled"), 3000);

    if (m_scan_session_precreated_blank_index >= 0 && !m_scan_session_has_received_page)
        removePendingBlankPage(m_scan_session_precreated_blank_index);

    m_scan_session_precreated_blank_index = -1;
    m_scan_session_has_received_page = false;
    m_scan_session_params_valid = false;

    if (m_act_cancel) m_act_cancel->setEnabled(false);
    if (m_act_scan) m_act_scan->setEnabled(true);
    if (m_act_preview) m_act_preview->setEnabled(true);

    updateStatusBar();
}

void
MainWindow::onScanError(const QString &error)
{
    QMessageBox::critical(this, tr("Scan Error"), error);
    statusBar()->showMessage(tr("Scan failed"), 3000);

    //This slot is also used for async preview failures (ScannerSource emits scanError on preview failure)
    if (m_scan_session_precreated_blank_index >= 0 && !m_scan_session_has_received_page)
        removePendingBlankPage(m_scan_session_precreated_blank_index);

    m_scan_session_precreated_blank_index = -1;
    m_scan_session_has_received_page = false;
    m_scan_session_params_valid = false;

    if (m_act_cancel) m_act_cancel->setEnabled(false);
    if (m_act_scan) m_act_scan->setEnabled(true);
    if (m_act_preview)
    {
        m_act_preview->setEnabled(true);
        m_act_preview->setChecked(false);
        m_act_preview->setText(tr("&Preview"));
    }

    updateStatusBar();
}

void
MainWindow::onScanStatusMessage(const QString &message)
{
    if (m_scan_source && m_scan_source->isScanning() && m_scanner_status_label)
    {
        m_scanner_status_label->setText(tr("\u231b %1").arg(message));
        return;
    }

    statusBar()->showMessage(message);
}

void
MainWindow::onProgressChanged(int percent)
{
    //Progress percentages are not possible
    Q_UNUSED(percent);
}

void
MainWindow::onPageSelected(int index)
{
    if (index >= 0 && index < m_document->pageCount())
    {
        ScannedPage &page = m_document->page(index);
        m_current_page_index = index;

        //Ensure displayed image matches current crop mode
        updateCameraWarp(page);
        const QImage img = page.currentImage();
        if (img.isNull())
            m_preview->showPlaceholder();
        else
            showPreviewWithCrop(img, page.cropRect());

        updatePageStatusLabel();
    }
}

void
MainWindow::onDeletePageRequested(int index)
{
    m_document->removePage(index);

    //Adjust current page index
    if (m_current_page_index == index)
        m_current_page_index = -1;
    else if (m_current_page_index > index)
        m_current_page_index--;

    //Hide page list if <=1 page
    if (m_document->pageCount() <= 1)
    {
        hidePageList();
    }

    //Clear preview when empty
    if (m_document->pageCount() == 0)
    {
        m_current_page_index = -1;
        m_preview->showPlaceholder();
    }
    else if (m_document->pageCount() == 1)
    {
        selectPage(0);
    }
    else
    {
        //Select a valid page after deletion
        int next = m_current_page_index;
        if (next < 0)
            next = qMin(index, m_document->pageCount() - 1);
        next = qBound(0, next, m_document->pageCount() - 1);
        selectPage(next);
    }

    updatePageStatusLabel();
}

void
MainWindow::onRotateLeftRequested()
{
    applyRotation(-90);
}

void
MainWindow::onRotateRightRequested()
{
    applyRotation(90);
}

void
MainWindow::onNextCropRequested()
{
    //Cycle through border detection candidates
    //TODO do not expect this to work, it's experimental
    if (!m_preview || !m_border_detector)
        return;

    const QRectF unit(0.0, 0.0, 1.0, 1.0);
    const qreal eps = 0.002;

    const bool have_page = m_document && m_page_list
        && m_current_page_index >= 0
        && m_current_page_index < m_document->pageCount();

    auto approxEqual = [eps](const QRectF &a, const QRectF &b) -> bool
    {
        return qAbs(a.x() - b.x()) <= eps
            && qAbs(a.y() - b.y()) <= eps
            && qAbs(a.width() - b.width()) <= eps
            && qAbs(a.height() - b.height()) <= eps;
    };

    //Pick display image and crop
    QImage display;
    QRectF current = unit;

    ScannedPage *page_ptr = nullptr;
    if (have_page)
    {
        page_ptr = &m_document->page(m_current_page_index);
        display = page_ptr->currentImage();
        current = page_ptr->cropRect();
    }
    else
    {
        display = m_last_preview_image;
        current = m_last_preview_crop_rect_norm.isValid() ? m_last_preview_crop_rect_norm : unit;
    }

    if (display.isNull())
    {
        statusBar()->showMessage(tr("No image available"), 2500);
        return;
    }

    //Run border detection and map candidates
    const BorderDetector::ContentBounds r = m_border_detector->detectBorders(display);
    if (r.candidates.isEmpty())
    {
        if (page_ptr)
        {
            page_ptr->setCropRect(unit);
            page_ptr->setAutoCropApplied(false);
            showPreviewWithCrop(display, page_ptr->cropRect());
            m_page_list->refresh();
        }
        else
        {
            m_last_preview_crop_rect_norm = unit;
            showPreviewWithCrop(display, unit);
        }

        if (m_control_panel)
        {
            m_control_panel->setAutoCropWarning(tr("No border detected"), true);
        }
        statusBar()->showMessage(tr("No border detected"), 2500);
        return;
    }

    //Convert candidates to normalized rects
    QVector<QRectF> cand_norm;
    cand_norm.reserve(r.candidates.size());
    const QRect full = display.rect();
    for (const QRect &c : r.candidates)
    {
        const QRect crop = c.intersected(full);
        if (!crop.isValid() || crop.isNull() || crop == full)
            continue;

        QRectF norm;
        norm.setX((qreal)crop.x() / (qreal)display.width());
        norm.setY((qreal)crop.y() / (qreal)display.height());
        norm.setWidth((qreal)crop.width() / (qreal)display.width());
        norm.setHeight((qreal)crop.height() / (qreal)display.height());
        norm = norm.intersected(unit);
        if (!norm.isValid() || norm.isNull() || norm == unit)
            continue;

        bool dup = false;
        for (const QRectF &ex : cand_norm)
        {
            if (approxEqual(ex, norm))
            {
                dup = true;
                break;
            }
        }
        if (!dup)
            cand_norm.push_back(norm);
    }

    const bool can_cycle = (cand_norm.size() >= 2);

    if (!can_cycle)
    {
        statusBar()->showMessage(tr("No alternate crop candidates"), 2500);
        return;
    }

    //Find current candidate and cycle to next
    int idx = -1;
    for (int i = 0; i < cand_norm.size(); i++)
    {
        if (approxEqual(cand_norm[i], current))
        {
            idx = i;
            break;
        }
    }

    const int next = (idx < 0) ? 0 : ((idx + 1) % cand_norm.size());
    const QRectF next_rect = cand_norm[next];
    if (approxEqual(next_rect, current))
    {
        statusBar()->showMessage(tr("No alternate crop candidates"), 2500);
        return;
    }

    if (page_ptr)
    {
        page_ptr->setCropRect(next_rect);
        page_ptr->setAutoCropApplied(false);
        showPreviewWithCrop(display, page_ptr->cropRect());
        m_page_list->refresh();
    }
    else
    {
        m_last_preview_crop_rect_norm = next_rect;
        showPreviewWithCrop(display, next_rect);
    }

    if (m_control_panel)
        m_control_panel->setAutoCropWarning(QString(), false);

    statusBar()->showMessage(tr("Crop candidate %1/%2").arg(next + 1).arg(cand_norm.size()), 2500);
}

void
MainWindow::onCropModeChanged(int mode)
{
    //Normalize crop mode from UI
    if (mode == (int)ScanControlPanel::CropAuto)
        m_crop_mode = ScanControlPanel::CropAuto;
    else if (mode == (int)ScanControlPanel::CropManual)
        m_crop_mode = ScanControlPanel::CropManual;
    else
        m_crop_mode = ScanControlPanel::CropOff;

    //Clear previous crop warning
    if (m_control_panel)
        m_control_panel->setAutoCropWarning(QString(), false);

    //Camera sources: Auto vs Off only
    if (m_scan_source && m_scan_source->capabilities().hasLivePreview() && m_crop_mode == ScanControlPanel::CropManual)
    {
        m_crop_mode = ScanControlPanel::CropOff;
        if (m_control_panel)
            m_control_panel->setCropMode(ScanControlPanel::CropOff);
        statusBar()->showMessage(tr("Manual crop not available for camera input"), 3000);
    }

    //Persist only the requested scanner preference: Auto vs Off
    if (m_scan_source && !m_scan_source->capabilities().hasLivePreview())
    {
        if (m_crop_mode == ScanControlPanel::CropAuto || m_crop_mode == ScanControlPanel::CropOff)
        {
            ProfileSettings *settings = ProfileSettings::useDefaultProfile();
            if (settings)
            {
                settings->setVariant("ui.crop_mode.scanner", (int)m_crop_mode);
                settings->save();
            }
        }
    }

    //Always reflect crop overlay state immediately
    //Even if no page is currently selected and we return early later
    if (m_preview)
    {
        const bool overlay_enabled = (m_crop_mode != ScanControlPanel::CropOff);
        m_preview->setCropOverlayEnabled(overlay_enabled);

        if (!overlay_enabled)
        {
            //Reset to full-frame in normalized coordinates to clear the overlay
            m_preview->setCropRectNormalized(QRectF(0.0, 0.0, 1.0, 1.0));
        }
    }

    //If no document page is selected, re-apply crop overlay to the last preview image
    //This keeps Off/Auto/Manual meaningful in preview-only workflows
    if (!m_last_preview_image.isNull())
    {
        const bool have_page = m_document && m_current_page_index >= 0 && m_current_page_index < m_document->pageCount();
        if (!have_page)
        {
            const QRectF unit(0.0, 0.0, 1.0, 1.0);

            if (m_crop_mode == ScanControlPanel::CropOff)
            {
                m_last_preview_crop_rect_norm = unit;
                showPreviewWithCrop(m_last_preview_image, unit);
                statusBar()->showMessage(tr("Crop disabled"), 2000);
                return;
            }

            if (m_crop_mode == ScanControlPanel::CropAuto)
            {
                const QRectF crop_norm = detectAutoCropRectNorm(m_last_preview_image);
                m_last_preview_crop_rect_norm = crop_norm;
                showPreviewWithCrop(m_last_preview_image, crop_norm);
                if (crop_norm == unit && m_control_panel)
                    m_control_panel->setAutoCropWarning(tr("No border detected"), true);
                statusBar()->showMessage(tr("Auto-crop updated"), 2000);
                return;
            }

            const QRectF crop_norm = m_last_preview_crop_rect_norm.isValid() ? m_last_preview_crop_rect_norm : unit;
            showPreviewWithCrop(m_last_preview_image, crop_norm);
            statusBar()->showMessage(tr("Manual crop: drag on image to set crop"), 3000);
            return;
        }
    }

    if (!m_document || !m_preview || !m_page_list)
        return;

    if (m_current_page_index < 0 || m_current_page_index >= m_document->pageCount())
        return;

    //Load display image used for crop computation/overlay
    ScannedPage &page = m_document->page(m_current_page_index);
    updateCameraWarp(page);
    const QImage display = page.currentImage();
    if (display.isNull())
        return;

    if (m_crop_mode == ScanControlPanel::CropOff)
    {
        //Crop off: reset to full image
        page.setCropRect(QRectF(0.0, 0.0, 1.0, 1.0));
        page.setAutoCropApplied(false);
        showPreviewWithCrop(display, page.cropRect());
        m_page_list->refresh();
        statusBar()->showMessage(tr("Crop disabled"), 2000);
        return;
    }

    if (m_crop_mode == ScanControlPanel::CropAuto)
    {
        //Auto mode: detect border and apply crop rect
        //detectAutoCropRectNorm returns full rect when no border is found
        const QRectF crop_norm = detectAutoCropRectNorm(display);
        if (crop_norm != QRectF(0.0, 0.0, 1.0, 1.0))
        {
            page.setCropRect(crop_norm);
            page.setAutoCropApplied(true);
            showPreviewWithCrop(display, page.cropRect());
            m_page_list->refresh();
            statusBar()->showMessage(tr("Auto-crop applied"), 2000);
            return;
        }

        //Auto mode fallback: reset to full image and show warning
        page.setCropRect(QRectF(0.0, 0.0, 1.0, 1.0));
        page.setAutoCropApplied(false);
        showPreviewWithCrop(display, page.cropRect());
        m_page_list->refresh();
        if (m_control_panel)
            m_control_panel->setAutoCropWarning(tr("No border detected"), true);
        statusBar()->showMessage(tr("Auto-crop: no border detected"), 3500);
        return;
    }

    //Manual mode: keep current crop rect
    showPreviewWithCrop(display, page.cropRect());
    m_page_list->refresh();
    statusBar()->showMessage(tr("Manual crop"), 2000);
}

void
MainWindow::onAdfModeChanged(bool enabled)
{
    //Disable preview button when ADF is enabled
    if (!m_act_preview)
        return;

    if (enabled && m_scan_source && m_scan_source->isPreviewActive())
    {
        m_scan_source->stopPreview();
        m_act_preview->setChecked(false);
        m_act_preview->setText(tr("&Preview"));
    }

    m_act_preview->setEnabled(!enabled);
}

void
MainWindow::onPreviewFrameReady(const QImage &image)
{
    m_last_preview_image = image;

    //Preview frame -> optional overlay
    if (m_scan_source && m_scan_source->isPreviewActive())
    {
        if (m_crop_mode == ScanControlPanel::CropAuto)
        {
            //Auto-crop hint on preview frame
            const QRectF crop_norm = detectAutoCropRectNorm(image);
            m_last_preview_crop_rect_norm = crop_norm;
            showPreviewWithCrop(image, crop_norm);
        }
        else
        {
            const QRectF unit(0.0, 0.0, 1.0, 1.0);
            const QRectF crop_norm = m_last_preview_crop_rect_norm.isValid() ? m_last_preview_crop_rect_norm : unit;
            showPreviewWithCrop(image, crop_norm);
        }
    }
    else
    {
        //Preview completed
        if (m_crop_mode == ScanControlPanel::CropAuto)
        {
            const QRectF crop_norm = detectAutoCropRectNorm(image);
            m_last_preview_crop_rect_norm = crop_norm;
            showPreviewWithCrop(image, crop_norm);
        }
        else
        {
            const QRectF unit(0.0, 0.0, 1.0, 1.0);
            const QRectF crop_norm = m_last_preview_crop_rect_norm.isValid() ? m_last_preview_crop_rect_norm : unit;
            showPreviewWithCrop(image, crop_norm);
        }
        m_act_preview->setEnabled(true);
        m_act_preview->setChecked(false);
        m_act_preview->setText(tr("&Preview"));

        //Update size label based on preview settings (preview uses auto area)
        if (m_size_status_label)
        {
            QStringList parts;
            QString area = tr("N/A");
            if (m_scan_source && m_scan_source->capabilities().supports_scan_settings)
                area = tr("Auto");
            parts << tr("Area: %1").arg(area);

            QString doc = tr("N/A");
            if (m_scan_source)
            {
                const ScanSource::ReportedDocumentSize rep = m_scan_source->reportedDocumentSize();
                if (rep.valid && !rep.mm_size.isEmpty())
                {
                    if (!rep.paper_name.isEmpty())
                        doc = tr("%1 (reported) %2").arg(rep.paper_name, formatMmCompact(rep.mm_size));
                    else
                        doc = tr("%1 (reported)").arg(formatMmCompact(rep.mm_size));
                }
            }
            parts << tr("Doc: %1").arg(doc);
            parts << tr("Img: %1").arg(formatPx(image.size()));
            m_size_status_label->setText(parts.join(tr(" · ")));
        }

        statusBar()->showMessage(tr("Preview ready"), 3000);
        updatePageStatusLabel();
    }
}

void
MainWindow::onPreviewClicked()
{
    if (!m_scan_source)
        return;

    ScanCapabilities caps = m_scan_source->capabilities();

    //Video input (camera/mobile)
    if (caps.hasLivePreview())
    {
        //Toggle live preview stream
        if (m_scan_source->isPreviewActive())
        {
            m_scan_source->stopPreview();
            m_act_preview->setText(tr("&Preview"));
            m_act_preview->setChecked(false);
            statusBar()->showMessage(tr("Preview stopped"));
        }
        else
        {
            if (m_scan_source->startPreview())
            {
                m_act_preview->setText(tr("Stop &Preview"));
                m_act_preview->setChecked(true);
                statusBar()->showMessage(tr("Preview started"));
            }
            else
            {
                m_act_preview->setChecked(false);
                QMessageBox::warning(this, tr("Preview Failed"),
                    tr("Failed to start live preview. The camera may be in use by another application."));
                statusBar()->showMessage(tr("Preview failed"), 3000);
            }
        }
    }
    //Photo input (scanner)
    else if (caps.preview_mode == PreviewMode::SingleImage)
    {
        //Trigger async preview scan
        statusBar()->showMessage(tr("Requesting preview..."));

        m_preview->showPlaceholder();

        if (m_scan_source->startPreview())
        {
            //Preview image will arrive via previewFrameReady signal
            m_act_preview->setChecked(false);
            m_act_preview->setText(tr("&Preview"));
            m_act_preview->setEnabled(false);
        }
        else
        {
            m_act_preview->setChecked(false);
            m_act_preview->setText(tr("&Preview"));
            QMessageBox::warning(this, tr("Preview Failed"),
                tr("Failed to start preview scan."));
            statusBar()->showMessage(tr("Preview not available"), 3000);
        }
    }
}

void
MainWindow::setupUi()
{
    //Create 3-pane splitter
    m_splitter = new QSplitter(Qt::Horizontal);

    //Create page list widget
    m_page_list = new PageListWidget(m_document);
    connect(m_page_list, SIGNAL(pageSelected(int)),
            this, SLOT(onPageSelected(int)));
    connect(m_page_list, SIGNAL(deletePageRequested(int)),
            this, SLOT(onDeletePageRequested(int)));

    //Create preview widget
    m_preview = new ScanPreviewWidget;
        connect(m_preview, SIGNAL(cropRectEdited(const QRectF&)),
            this, SLOT(onPreviewCropRectEdited(const QRectF&)));

    //Create control panel
    m_control_panel = new ScanControlPanel;
    if (m_scan_source)
    {
        m_control_panel->setCapabilities(m_scan_source->capabilities());

        //Default crop mode: Auto for scanners, Off for live-stream sources
        const bool is_scanner = !m_scan_source->capabilities().hasLivePreview();
        ScanControlPanel::CropMode initial = is_scanner ? ScanControlPanel::CropAuto : ScanControlPanel::CropOff;
        if (is_scanner)
        {
            ProfileSettings *settings = ProfileSettings::useDefaultProfile();
            if (settings)
            {
                const QVariant v = settings->variant("ui.crop_mode.scanner");
                if (v.isValid())
                {
                    const int raw = v.toInt();
                    if (raw == (int)ScanControlPanel::CropOff || raw == (int)ScanControlPanel::CropAuto)
                        initial = (ScanControlPanel::CropMode)raw;
                }
            }
        }

        m_crop_mode = initial;
        m_control_panel->setCropMode(m_crop_mode);
    }
    connect(m_control_panel, SIGNAL(rotateLeftRequested()),
            this, SLOT(onRotateLeftRequested()));
    connect(m_control_panel, SIGNAL(rotateRightRequested()),
            this, SLOT(onRotateRightRequested()));
        connect(m_control_panel, SIGNAL(cropModeChanged(int)),
            this, SLOT(onCropModeChanged(int)));
        connect(m_control_panel, SIGNAL(nextCropRequested()),
            this, SLOT(onNextCropRequested()));
    connect(m_control_panel, SIGNAL(adfModeChanged(bool)),
            this, SLOT(onAdfModeChanged(bool)));

    //Add widgets to splitter
    m_splitter->addWidget(m_page_list);
    m_splitter->addWidget(m_preview);
    m_splitter->addWidget(m_control_panel);

    //Set initial splitter sizes
    QList<int> sizes;
    sizes << 0 << 800 << 250;
    m_splitter->setSizes(sizes);

    //Set minimum widths
    m_page_list->setMinimumWidth(0);
    m_preview->setMinimumWidth(400);
    m_control_panel->setMinimumWidth(220);
    m_control_panel->setMaximumWidth(350);

    setCentralWidget(m_splitter);

    //Create status bar
    m_status_bar = statusBar();

    //Create scanner status label and document size label for status bar
    m_scanner_status_label = new QLabel;
    m_status_bar->addWidget(m_scanner_status_label, 1);

    //Create current page label for status bar (right side)
    m_page_status_label = new QLabel;
    m_status_bar->addPermanentWidget(m_page_status_label);

    //Create document size label for status bar
    m_size_status_label = new QLabel(tr("Area: N/A · Doc: N/A · Img: N/A"));
    m_status_bar->addPermanentWidget(m_size_status_label);

    updatePageStatusLabel();
}

void
MainWindow::createActions()
{
    m_act_preview = new QAction(tr("&Preview"), this);
    m_act_preview->setStatusTip(tr("Start preview"));
    m_act_preview->setCheckable(true);  //Makes button appear pressed when checked
    connect(m_act_preview, SIGNAL(triggered()), this, SLOT(onPreviewClicked()));

    m_act_scan = new QAction(tr("&Scan"), this);
    m_act_scan->setStatusTip(tr("Scan"));
    connect(m_act_scan, SIGNAL(triggered()), this, SLOT(onScanClicked()));

    m_act_cancel = new QAction(tr("&Cancel"), this);
    m_act_cancel->setStatusTip(tr("Cancel current scan"));
    m_act_cancel->setEnabled(false);
    connect(m_act_cancel, &QAction::triggered, this, [this]()
    {
        if (m_scan_source)
            m_scan_source->cancelScan();
    });

    //Disable preview and scan if no source
    if (!m_scan_source)
    {
        m_act_preview->setEnabled(false);
        m_act_scan->setEnabled(false);
        m_act_cancel->setEnabled(false);
    }

    m_act_save = new QAction(tr("S&ave"), this);
    m_act_save->setShortcut(QKeySequence::Save);
    m_act_save->setStatusTip(tr("Save scanned document"));
    connect(m_act_save, SIGNAL(triggered()), this, SLOT(onSaveClicked()));

    m_act_quit = new QAction(tr("&Quit"), this);
    m_act_quit->setShortcut(QKeySequence::Quit);
    m_act_quit->setStatusTip(tr("Exit application"));
    connect(m_act_quit, SIGNAL(triggered()), this, SLOT(onQuitClicked()));
}

void
MainWindow::createToolbar()
{
    m_toolbar = addToolBar(tr("Main Toolbar"));
    m_toolbar->addAction(m_act_preview);
    m_toolbar->addAction(m_act_scan);
    m_toolbar->addAction(m_act_cancel);
    m_toolbar->addAction(m_act_save);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_act_quit);

    QWidget *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    QWidget *branding_widget = new QWidget(this);
    QVBoxLayout *branding_layout = new QVBoxLayout(branding_widget);
    branding_layout->setContentsMargins(0, 0, 0, 0);
    branding_layout->setSpacing(0);

    m_branding_label = new QLabel(tr("QSCAN"), branding_widget);
    m_branding_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont qscan_font = m_branding_label->font();
    qscan_font.setItalic(true);
    qscan_font.setBold(true);
    if (qscan_font.pointSize() > 0)
        qscan_font.setPointSize(qscan_font.pointSize() + 2);
    m_branding_label->setFont(qscan_font);

    QLabel *by_label = new QLabel(tr("by c0xc@PingIT"), branding_widget);
    by_label->setAlignment(Qt::AlignRight | Qt::AlignTop);
    QFont by_font = by_label->font();
    if (by_font.pointSize() > 0)
        by_font.setPointSize(qMax(1, by_font.pointSize() - 1));
    by_label->setFont(by_font);
    by_label->setEnabled(false);

    branding_layout->addWidget(m_branding_label);
    branding_layout->addWidget(by_label);
    m_toolbar->addWidget(branding_widget);
}

void
MainWindow::updateStatusBar()
{
    const QString text = m_scan_source
        ? tr("Ready - %1").arg(m_scan_source->deviceDescription())
        : tr("No scanner connected");

    if (m_scanner_status_label)
        m_scanner_status_label->setText(text);
    else
        statusBar()->showMessage(text);

    updatePageStatusLabel();
}

void
MainWindow::updatePageStatusLabel()
{
    if (!m_page_status_label)
        return;

    const int total = m_document ? m_document->pageCount() : 0;
    if (total <= 0)
    {
        m_page_status_label->setText(QString());
        return;
    }

    int current = m_current_page_index;
    if (current < 0 || current >= total)
        current = 0;

    m_page_status_label->setText(tr("Page %1/%2").arg(current + 1).arg(total));
}

void
MainWindow::selectPage(int index)
{
    //Validate inputs
    if (!m_document)
        return;

    if (index < 0 || index >= m_document->pageCount())
        return;

    //Update selection and status
    m_current_page_index = index;
    if (m_page_list)
        m_page_list->selectPageIndex(index);

    updatePageStatusLabel();
}

void
MainWindow::removePendingBlankPage(int index)
{
    if (!m_document)
        return;
    if (index < 0 || index >= m_document->pageCount())
        return;

    const ScannedPage &page = m_document->page(index);
    if (!page.rawImage().isNull())
        return;

    //Only remove trailing blank pages to avoid shifting earlier indices unexpectedly
    if (index != (m_document->pageCount() - 1))
        return;

    m_document->removePage(index);
    m_page_list->refresh();

    if (m_document->pageCount() <= 1)
        hidePageList();

    if (m_document->pageCount() == 0)
    {
        m_current_page_index = -1;
        m_preview->showPlaceholder();
    }
    else
    {
        selectPage(qBound(0, m_current_page_index, m_document->pageCount() - 1));
    }

    updatePageStatusLabel();
}

QString
MainWindow::getSaveFileName()
{
    QString filter;
    QString default_filter;

    //Smart defaults based on page count
    if (m_document->pageCount() > 1)
    {
        //Multiple pages: default PDF
        filter = tr("PDF Files (*.pdf);;JPEG Images (*.jpg *.jpeg)");
        default_filter = tr("PDF Files (*.pdf)");
    }
    else
    {
        //Single page: default JPG
        filter = tr("JPEG Images (*.jpg *.jpeg);;PDF Files (*.pdf)");
        default_filter = tr("JPEG Images (*.jpg *.jpeg)");
    }

    return QFileDialog::getSaveFileName(this, tr("Save Document"), QString(), filter, &default_filter);
}

void
MainWindow::showPageList()
{
    //Expand page list
    QList<int> sizes = m_splitter->sizes();
    if (sizes[0] == 0)
    {
        //If hidden, show page list
        sizes[0] = 180;  //Page list width
        sizes[1] = sizes[1] - 180;  //Reduce preview width
        m_splitter->setSizes(sizes);
    }
}

void
MainWindow::hidePageList()
{
    //Collapse page list
    QList<int> sizes = m_splitter->sizes();
    if (sizes[0] > 0)
    {
        sizes[1] = sizes[1] + sizes[0];  //Give width back to preview
        sizes[0] = 0;  //Hide page list
        m_splitter->setSizes(sizes);
    }
}

void
MainWindow::applyRotation(int degrees)
{
    //Choose page index
    int current_index = m_page_list->selectedPageIndex();
    if (current_index < 0)
    {
        //Rotate only page when single
        if (m_document->pageCount() == 1)
        {
            current_index = 0;
        }
        else
        {
            statusBar()->showMessage(tr("No page selected"), 2000);
            return;
        }
    }

    //Apply rotation and reset crop
    if (current_index >= 0 && current_index < m_document->pageCount())
    {
        ScannedPage &page = m_document->page(current_index);
        int new_rotation = (page.rotation() + degrees) % 360;
        if (new_rotation < 0) new_rotation += 360;
        page.setRotation(new_rotation);

        //Reset crop after rotation
        page.setCropRect(QRectF(0.0, 0.0, 1.0, 1.0));
        page.setAutoCropApplied(false);

        if (m_crop_mode == ScanControlPanel::CropAuto)
        {
            //Auto-crop hint for new rotation
            const QRectF crop_norm = detectAutoCropRectNorm(page.currentImage());
            if (crop_norm != QRectF(0.0, 0.0, 1.0, 1.0))
            {
                page.setCropRect(crop_norm);
                page.setAutoCropApplied(true);
                if (m_control_panel)
                    m_control_panel->setAutoCropWarning(QString(), false);
            }
            else
            {
                if (m_control_panel)
                    m_control_panel->setAutoCropWarning(tr("No border detected"), true);
            }
        }

        //Update preview and thumbnails
        showPreviewWithCrop(page.currentImage(), page.cropRect());

        //Update thumbnail
        m_page_list->refresh();

        statusBar()->showMessage(tr("Page rotated"), 2000);
    }
}

void
MainWindow::onPreviewCropRectEdited(const QRectF &normalized_rect)
{
    //Manual crop edit from preview overlay
    const QRectF unit(0.0, 0.0, 1.0, 1.0);
    m_last_preview_crop_rect_norm = normalized_rect.isValid() ? normalized_rect : unit;

    if (m_document && m_page_list && m_current_page_index >= 0 && m_current_page_index < m_document->pageCount())
    {
        ScannedPage &page = m_document->page(m_current_page_index);
        if (!page.rawImage().isNull())
        {
            page.setCropRect(normalized_rect);
            page.setAutoCropApplied(false);
        }
    }

    //Manual edit while in Auto switches to Manual mode
    if (m_crop_mode == ScanControlPanel::CropAuto)
    {
        if (m_control_panel)
        {
            m_control_panel->setAutoCropWarning(QString(), false);
            m_control_panel->setCropMode(ScanControlPanel::CropManual);
        }
        m_crop_mode = ScanControlPanel::CropManual;
        statusBar()->showMessage(tr("Switched to Manual crop"), 2500);
    }
    if (m_page_list)
        m_page_list->refresh();
}

QRectF
MainWindow::detectAutoCropRectNorm(const QImage &image) const
{
    //Map content bounds to normalized rect
    if (!m_border_detector)
        return QRectF(0.0, 0.0, 1.0, 1.0);

    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return QRectF(0.0, 0.0, 1.0, 1.0);

    const QRect full = image.rect();
    const BorderDetector::ContentBounds r = m_border_detector->detectBorders(image);
    if (!r.confident)
        return QRectF(0.0, 0.0, 1.0, 1.0);

    //Detect content bounds in pixels
    QRect crop = r.best.intersected(full);
    if (!crop.isValid() || crop.isNull() || crop == full)
        return QRectF(0.0, 0.0, 1.0, 1.0);

    const int trim_l = crop.left();
    const int trim_t = crop.top();
    const int trim_r = full.right() - crop.right();
    const int trim_b = full.bottom() - crop.bottom();
    //Ignore tiny trims
    if (trim_l <= 2 && trim_t <= 2 && trim_r <= 2 && trim_b <= 2)
        return QRectF(0.0, 0.0, 1.0, 1.0);

    QRectF norm;
    norm.setX((qreal)crop.x() / (qreal)image.width());
    norm.setY((qreal)crop.y() / (qreal)image.height());
    norm.setWidth((qreal)crop.width() / (qreal)image.width());
    norm.setHeight((qreal)crop.height() / (qreal)image.height());

    const QRectF unit(0.0, 0.0, 1.0, 1.0);
    norm = norm.intersected(unit);
    if (!norm.isValid() || norm.isNull())
        return QRectF(0.0, 0.0, 1.0, 1.0);

    return norm;
}

void
MainWindow::showPreviewWithCrop(const QImage &image, const QRectF &crop_rect_norm)
{
    //Validate widget and image
    if (!m_preview)
        return;

    if (image.isNull())
    {
        m_preview->showPlaceholder();
        return;
    }

    //Show image and apply overlay
    m_preview->showImage(image);

    const bool overlay_enabled = (m_crop_mode != ScanControlPanel::CropOff);

    //Enable crop overlay for manual edits
    m_preview->setCropOverlayEnabled(overlay_enabled);
    m_preview->setCropRectNormalized(overlay_enabled ? crop_rect_norm : QRectF(0.0, 0.0, 1.0, 1.0));
}

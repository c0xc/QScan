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

#include "gui/mainwindow.hpp"
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>

MainWindow::MainWindow(ScanSource *source,
                       QWidget *parent)
          : QMainWindow(parent),
            m_scan_source(source),
            m_document(0),
            m_exporter(0),
            m_border_detector(0),
            m_crop_processor(0),
            m_rotate_processor(0),
            m_preview(0),
            m_page_list(0),
            m_control_panel(0),
            m_splitter(0),
            m_toolbar(0),
            m_status_bar(0),
            m_size_status_label(0),
            m_autocrop_enabled(false)
{
    setWindowTitle(tr("QScan"));
    resize(1200, 700);
    
    // Create document
    m_document = new Document(this);
    
    // Create exporter
    m_exporter = new DocumentExporter;
    
    // Create processors
    m_border_detector = new BorderDetector;
    m_crop_processor = new CropProcessor;
    m_rotate_processor = new RotateProcessor;
    
    // Setup UI
    setupUi();
    createActions();
    createToolbar();
    
    // Update status bar
    updateStatusBar();
    
    //Connect scan source signals if available
    if (m_scan_source)
    {
        connect(m_scan_source, SIGNAL(pageScanned(const QImage&, int)),
                this, SLOT(onPageScanned(const QImage&, int)));
        connect(m_scan_source, SIGNAL(scanComplete()),
                this, SLOT(onScanComplete()));
        connect(m_scan_source, SIGNAL(scanError(const QString&)),
                this, SLOT(onScanError(const QString&)));
        connect(m_scan_source, SIGNAL(progressChanged(int)),
                this, SLOT(onProgressChanged(int)));
        connect(m_scan_source, SIGNAL(previewFrameReady(const QImage&)),
                this, SLOT(onPreviewFrameReady(const QImage&)));

        // Defer device initialization to avoid blocking GUI startup
        QTimer::singleShot(0, this, [this]() {
            if (m_scan_source && !m_scan_source->initialize())
            {
                QMessageBox::warning(this, tr("Device Initialization Failed"),
                    tr("Failed to initialize %1. The device may be in use or unavailable.")
                    .arg(m_scan_source->deviceDescription()));
                m_act_preview->setEnabled(false);
                m_act_scan->setEnabled(false);
            }
        });
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
    
    QString filename = getSaveFileName();
    if (filename.isEmpty())
        return;
    
    // TODO: Implement save
    QMessageBox::information(this, tr("Save"),
                           tr("Save functionality not yet implemented."));
}

void
MainWindow::onQuitClicked()
{
    close();
}

void
MainWindow::onPageScanned(const QImage &image, int page_number)
{
    QImage processed_image = image;
    
    //Apply auto-crop if enabled
    if (m_autocrop_enabled)
    {
        applyAutoCropToImage(processed_image);
    }
    
    //Add page to document
    ScannedPage page(processed_image);
    m_document->addPage(page);

    //Show in preview
    m_preview->showImage(processed_image);
    
    //Show page list if we now have 2+ pages
    if (m_document->pageCount() >= 2)
    {
        showPageList();
    }

    //Update document size in status bar
    if (m_scan_source)
    {
        QSizeF size = m_scan_source->currentDocumentSize();
        if (!size.isEmpty())
        {
            QString size_text = QString("%1 x %2 mm")
                                .arg(size.width(), 0, 'f', 1)
                                .arg(size.height(), 0, 'f', 1);

            QString note;
            if (m_scan_source->documentSizeWasAutoDetected())
                note = tr(" (autodetected)");
            else if (m_scan_source->documentSizeIsReported())
                note = tr(" (reported by device)");

            m_size_status_label->setText(tr("Document size: %1%2").arg(size_text, note));
        }
    }

    statusBar()->showMessage(tr("Page %1 scanned").arg(page_number + 1), 3000);
}

void
MainWindow::onScanComplete()
{
    statusBar()->showMessage(tr("Scan complete"), 3000);
}

void
MainWindow::onScanError(const QString &error)
{
    QMessageBox::critical(this, tr("Scan Error"), error);
    statusBar()->showMessage(tr("Scan failed"), 3000);
}

void
MainWindow::onProgressChanged(int percent)
{
    statusBar()->showMessage(tr("Scanning... %1%").arg(percent));
}

void
MainWindow::onPageSelected(int index)
{
    if (index >= 0 && index < m_document->pageCount())
    {
        const ScannedPage &page = m_document->page(index);
        m_preview->showImage(page.processedImage());
    }
}

void
MainWindow::onDeletePageRequested(int index)
{
    m_document->removePage(index);

    // Hide page list if we're back to 1 or 0 pages
    if (m_document->pageCount() <= 1)
    {
        hidePageList();
    }

    // Clear preview if no pages left
    if (m_document->pageCount() == 0)
    {
        m_preview->showPlaceholder();
    }
    else if (m_document->pageCount() == 1)
    {
        // Show the remaining page
        m_preview->showImage(m_document->page(0).processedImage());
    }
}

void
MainWindow::onRotateLeftRequested()
{
    applyRotationToCurrentPage(-90);
}

void
MainWindow::onRotateRightRequested()
{
    applyRotationToCurrentPage(90);
}

void
MainWindow::onManualCropRequested()
{
    // TODO: Implement manual crop mode
    statusBar()->showMessage(tr("Manual crop not yet implemented"), 3000);
}

void
MainWindow::onAutoCropChanged(bool enabled)
{
    m_autocrop_enabled = enabled;
    statusBar()->showMessage(enabled ? tr("Auto-crop enabled") : tr("Auto-crop disabled"), 2000);
}

void
MainWindow::onAdfModeChanged(bool enabled)
{
    // Disable preview button when ADF is enabled
    if (m_act_preview)
    {
        m_act_preview->setEnabled(!enabled);
    }
}

void
MainWindow::onPreviewFrameReady(const QImage &image)
{
    //Update preview with live frame while live preview is active
    if (m_scan_source && m_scan_source->isPreviewActive())
    {
        m_preview->showImage(image);
    }
    else
    {
        //SingleImage preview completed - show image and re-enable button
        m_preview->showImage(image);
        m_act_preview->setEnabled(true);
        statusBar()->showMessage(tr("Preview ready"), 3000);
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
        if (m_scan_source->startPreview())
        {
            //Preview image will arrive via previewFrameReady signal
            m_act_preview->setEnabled(false);
        }
        else
        {
            QMessageBox::warning(this, tr("Preview Failed"),
                tr("Failed to start preview scan."));
            statusBar()->showMessage(tr("Preview not available"), 3000);
        }
    }
}

void
MainWindow::setupUi()
{
    // Create 3-pane splitter: page list | preview | control panel
    m_splitter = new QSplitter(Qt::Horizontal);
    
    // Create page list widget (hidden initially)
    m_page_list = new PageListWidget(m_document);
    connect(m_page_list, SIGNAL(pageSelected(int)),
            this, SLOT(onPageSelected(int)));
    connect(m_page_list, SIGNAL(deletePageRequested(int)),
            this, SLOT(onDeletePageRequested(int)));
    
    // Create preview widget
    m_preview = new ScanPreviewWidget;
    
    // Create control panel
    m_control_panel = new ScanControlPanel;
    if (m_scan_source)
    {
        m_control_panel->setCapabilities(m_scan_source->capabilities());
    }
    connect(m_control_panel, SIGNAL(rotateLeftRequested()),
            this, SLOT(onRotateLeftRequested()));
    connect(m_control_panel, SIGNAL(rotateRightRequested()),
            this, SLOT(onRotateRightRequested()));
    connect(m_control_panel, SIGNAL(manualCropRequested()),
            this, SLOT(onManualCropRequested()));
    connect(m_control_panel, SIGNAL(autoCropChanged(bool)),
            this, SLOT(onAutoCropChanged(bool)));
    connect(m_control_panel, SIGNAL(adfModeChanged(bool)),
            this, SLOT(onAdfModeChanged(bool)));
    
    // Add widgets to splitter
    m_splitter->addWidget(m_page_list);
    m_splitter->addWidget(m_preview);
    m_splitter->addWidget(m_control_panel);
    
    // Set initial sizes: page list hidden (0 width), preview stretches, control panel fixed
    QList<int> sizes;
    sizes << 0 << 800 << 250;
    m_splitter->setSizes(sizes);
    
    // Set minimum widths
    m_page_list->setMinimumWidth(0);
    m_preview->setMinimumWidth(400);
    m_control_panel->setMinimumWidth(220);
    m_control_panel->setMaximumWidth(350);
    
    setCentralWidget(m_splitter);
    
    //Create status bar
    m_status_bar = statusBar();
    
    //Create document size label for status bar
    m_size_status_label = new QLabel(tr("Document size: N/A"));
    m_status_bar->addPermanentWidget(m_size_status_label);
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

    //Disable preview and scan if no source
    if (!m_scan_source)
    {
        m_act_preview->setEnabled(false);
        m_act_scan->setEnabled(false);
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
    m_toolbar->addAction(m_act_save);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_act_quit);
}

void
MainWindow::updateStatusBar()
{
    if (m_scan_source)
    {
        QString device = m_scan_source->deviceDescription();
        statusBar()->showMessage(tr("Ready - %1").arg(device));
    }
    else
    {
        statusBar()->showMessage(tr("No scanner connected"));
    }
}

QString
MainWindow::getSaveFileName()
{
    QString filter;
    QString default_filter;
    
    // Smart defaults based on page count
    if (m_document->pageCount() > 1)
    {
        // Multiple pages: default to PDF
        filter = tr("PDF Files (*.pdf);;JPEG Images (*.jpg);;PNG Images (*.png)");
        default_filter = tr("PDF Files (*.pdf)");
    }
    else
    {
        // Single page: default to JPG
        filter = tr("JPEG Images (*.jpg);;PNG Images (*.png);;PDF Files (*.pdf)");
        default_filter = tr("JPEG Images (*.jpg)");
    }
    
    return QFileDialog::getSaveFileName(this, tr("Save Document"), QString(), filter, &default_filter);
}

void
MainWindow::showPageList()
{
    // Expand page list to reasonable width
    QList<int> sizes = m_splitter->sizes();
    if (sizes[0] == 0)
    {
        // Page list is hidden, show it
        sizes[0] = 180;  // Page list width
        sizes[1] = sizes[1] - 180;  // Reduce preview width
        m_splitter->setSizes(sizes);
    }
}

void
MainWindow::hidePageList()
{
    // Collapse page list to zero width
    QList<int> sizes = m_splitter->sizes();
    if (sizes[0] > 0)
    {
        sizes[1] = sizes[1] + sizes[0];  // Give width back to preview
        sizes[0] = 0;  // Hide page list
        m_splitter->setSizes(sizes);
    }
}

void
MainWindow::applyRotationToCurrentPage(int degrees)
{
    int current_index = m_page_list->selectedPageIndex();
    if (current_index < 0)
    {
        // No page selected, try to rotate the only page if there's exactly one
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
    
    if (current_index >= 0 && current_index < m_document->pageCount())
    {
        ScannedPage &page = m_document->page(current_index);
        int new_rotation = (page.rotation() + degrees) % 360;
        if (new_rotation < 0) new_rotation += 360;
        page.setRotation(new_rotation);
        
        // Update preview
        m_preview->showImage(page.processedImage());
        
        // Update thumbnail in page list
        m_page_list->refresh();
        
        statusBar()->showMessage(tr("Page rotated"), 2000);
    }
}

void
MainWindow::applyAutoCropToImage(QImage &image)
{
    // Use border detector to find document edges
    QRect crop_rect = m_border_detector->detectContentBounds(image);
    
    if (!crop_rect.isNull() && crop_rect.isValid())
    {
        // Apply crop
        image = m_crop_processor->crop(image, crop_rect);
        statusBar()->showMessage(tr("Auto-crop applied"), 2000);
    }
}

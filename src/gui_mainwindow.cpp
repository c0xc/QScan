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

MainWindow::MainWindow(ScanSource *source,
                       Document::ScanMode mode,
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
            m_splitter(0),
            m_toolbar(0),
            m_status_bar(0)
{
    setWindowTitle(tr("QScan"));
    resize(1000, 700);
    
    // Create document
    m_document = new Document(this);
    m_document->setScanMode(mode);
    
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

        //Start live preview if source supports it
        if (m_scan_source->capabilities().hasLivePreview())
        {
            m_scan_source->startLivePreview();
        }
    }
}

MainWindow::~MainWindow()
{
    //Stop live preview if active
    if (m_scan_source && m_scan_source->isLivePreviewActive())
    {
        m_scan_source->stopLivePreview();
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

    //Stop live preview before scanning
    if (m_scan_source->isLivePreviewActive())
    {
        m_scan_source->stopLivePreview();
    }

    ScanParameters params;
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
    // Add page to document
    ScannedPage page(image);
    m_document->addPage(page);

    // Show in preview
    m_preview->showImage(image);

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
MainWindow::onAddPageRequested()
{
    // TODO: Trigger another scan
    onScanClicked();
}

void
MainWindow::onDeletePageRequested(int index)
{
    m_document->removePage(index);

    //Clear preview if no pages left
    if (m_document->pageCount() == 0)
    {
        m_preview->showPlaceholder();
    }
}

void
MainWindow::onPreviewFrameReady(const QImage &image)
{
    //Update preview with live frame while live preview is active
    if (m_scan_source && m_scan_source->isLivePreviewActive())
    {
        m_preview->showImage(image);
    }
}

void
MainWindow::onPreviewClicked()
{
    if (!m_scan_source)
        return;

    ScanCapabilities caps = m_scan_source->capabilities();

    if (caps.hasLivePreview())
    {
        //Start live preview stream
        if (!m_scan_source->isLivePreviewActive())
        {
            m_scan_source->startLivePreview();
            statusBar()->showMessage(tr("Preview started"));
        }
    }
    else if (caps.preview_mode == PreviewMode::SingleImage)
    {
        //Request a single preview frame
        statusBar()->showMessage(tr("Requesting preview..."));
        QImage frame = m_scan_source->requestPreviewFrame();
        if (!frame.isNull())
        {
            m_preview->showImage(frame);
            statusBar()->showMessage(tr("Preview ready"), 3000);
        }
        else
        {
            statusBar()->showMessage(tr("Preview not available"), 3000);
        }
    }
}

void
MainWindow::setupUi()
{
    // Create preview widget
    m_preview = new ScanPreviewWidget;
    
    // Create page list widget (only in DOCUMENT_MODE)
    if (m_document->scanMode() == Document::DOCUMENT_MODE)
    {
        m_page_list = new PageListWidget(m_document);
        connect(m_page_list, SIGNAL(pageSelected(int)),
                this, SLOT(onPageSelected(int)));
        connect(m_page_list, SIGNAL(addPageRequested()),
                this, SLOT(onAddPageRequested()));
        connect(m_page_list, SIGNAL(deletePageRequested(int)),
                this, SLOT(onDeletePageRequested(int)));
        
        // Create splitter with preview and page list
        m_splitter = new QSplitter(Qt::Horizontal);
        m_splitter->addWidget(m_preview);
        m_splitter->addWidget(m_page_list);
        m_splitter->setStretchFactor(0, 3);  // Preview gets more space
        m_splitter->setStretchFactor(1, 1);
        
        setCentralWidget(m_splitter);
    }
    else
    {
        // IMAGE_MODE: just preview
        setCentralWidget(m_preview);
    }
    
    // Create status bar
    m_status_bar = statusBar();
}

void
MainWindow::createActions()
{
    m_act_preview = new QAction(tr("&Preview"), this);
    m_act_preview->setStatusTip(tr("Start preview"));
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
        statusBar()->showMessage(tr("No scanner connected (backend not implemented)"));
    }
}

QString
MainWindow::getSaveFileName()
{
    QString filter;
    if (m_document->scanMode() == Document::DOCUMENT_MODE)
    {
        filter = tr("PDF Files (*.pdf);;All Files (*)");
    }
    else
    {
        filter = tr("Images (*.jpg *.png);;All Files (*)");
    }
    
    return QFileDialog::getSaveFileName(this, tr("Save Document"), QString(), filter);
}

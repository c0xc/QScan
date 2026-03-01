/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (philip@c0xc.net)
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

#ifndef GUI_MAINWINDOW_HPP
#define GUI_MAINWINDOW_HPP

#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QSplitter>
#include <QImage>
#include <QRectF>

#include "scan/scan_source.hpp"
#include "scan/scan_manager.hpp"
#include "document/document.hpp"
#include "document/documentexporter.hpp"
#include "processing/image_processor.hpp"
#include "processing/crop_processor.hpp"
#include "processing/rotate_processor.hpp"
#include "processing/border_detector.hpp"
#include "processing/smart_capture_processor.hpp"
#include "gui/scanpreviewwidget.hpp"
#include "gui/pagelistwidget.hpp"
#include "gui/scancontrolpanel.hpp"

/**
 * Main scanning window.
 * NO direct SANE or OpenCV calls - uses abstraction layers.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    MainWindow(ScanSource *source,
               QWidget *parent = 0);

    ~MainWindow() override;

private slots:

    void
    onScanClicked();

    void
    onSaveClicked();

    void
    onQuitClicked();

    void
    onPageScanned(const QImage &image, int page_number, const qscan::ScanPageInfo &page_info);

    void
    onScanComplete();

    void
    onScanCanceled();

    void
    onScanError(const QString &error);

    void
    onScanStatusMessage(const QString &message);

    void
    onProgressChanged(int percent);

    void
    onPageSelected(int index);

    void
    onDeletePageRequested(int index);

    void
    onRotateLeftRequested();

    void
    onRotateRightRequested();

    void
    onCropModeChanged(int mode);

    void
    onNextCropRequested();

    void
    onAdfModeChanged(bool enabled);

    void
    onPreviewFrameReady(const QImage &image);

    void
    onPreviewClicked();

    void
    onPreviewCropRectEdited(const QRectF &normalized_rect);

private:

    //Backend components
    ScanSource *m_scan_source;
    Document *m_document;
    DocumentExporter *m_exporter;
    
    //Processing components
    BorderDetector *m_border_detector;
    CropProcessor *m_crop_processor;
    RotateProcessor *m_rotate_processor;
    SmartCaptureProcessor *m_smart_capture_processor;

    //GUI components
    ScanPreviewWidget *m_preview;
    PageListWidget *m_page_list;
    ScanControlPanel *m_control_panel;
    QSplitter *m_splitter;
    QToolBar *m_toolbar;
    QStatusBar *m_status_bar;
    QLabel *m_scanner_status_label;
    QLabel *m_page_status_label;
    QLabel *m_size_status_label;
    QLabel *m_branding_label;
    
    //State
    ScanControlPanel::CropMode m_crop_mode;
    int m_current_page_index;

    //Preview state (for sources where no document page exists yet)
    QImage m_last_preview_image;
    QRectF m_last_preview_crop_rect_norm;

    //Scan-session state: map backend page_number -> document page index
    int m_scan_session_base_index;
    int m_scan_session_precreated_blank_index;
    bool m_scan_session_has_received_page;
    ScanParameters m_scan_session_params;
    bool m_scan_session_params_valid;

    //Actions
    QAction *m_act_preview;
    QAction *m_act_scan;
    QAction *m_act_cancel;
    QAction *m_act_save;
    QAction *m_act_quit;

    void
    setupUi();

    void
    createActions();

    void
    createToolbar();

    void
    updateStatusBar();

    void
    updatePageStatusLabel();

    void
    selectPage(int index);

    void
    removePendingBlankPage(int index);

    QString
    getSaveFileName();

    void
    showPageList();

    void
    hidePageList();

    void
    applyRotation(int degrees);

    QRectF
    detectAutoCropRectNorm(const QImage &image) const;

    void
    updateCameraWarp(ScannedPage &page);

    void
    showPreviewWithCrop(const QImage &image, const QRectF &crop_rect_norm);

};

#endif //GUI_MAINWINDOW_HPP

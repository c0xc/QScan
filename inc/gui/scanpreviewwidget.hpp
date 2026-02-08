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

#ifndef GUI_SCANPREVIEWWIDGET_HPP
#define GUI_SCANPREVIEWWIDGET_HPP

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QImage>

/**
 * QGraphicsView-based widget for displaying scanned images.
 * Supports zoom, pan, and shows A4 placeholder before scanning.
 */
class ScanPreviewWidget : public QGraphicsView
{
    Q_OBJECT

public:

    enum ZoomMode
    {
        FIT_TO_WINDOW,
        FIT_TO_WIDTH,
        ACTUAL_SIZE,
        CUSTOM
    };

    explicit
    ScanPreviewWidget(QWidget *parent = 0);

    ~ScanPreviewWidget() override;

    /**
     * Show A4 aspect ratio placeholder before scanning.
     */
    void
    showPlaceholder();

    /**
     * Show a scanned image.
     */
    void
    showImage(const QImage &image);

    /**
     * Clear the display.
     */
    void
    clear();

    /**
     * Set zoom mode.
     */
    void
    setZoomMode(ZoomMode mode);

    /**
     * Get current zoom mode.
     */
    ZoomMode
    zoomMode() const;

    /**
     * Set custom zoom factor (for CUSTOM mode).
     */
    void
    setZoomFactor(qreal factor);

protected:

    void
    resizeEvent(QResizeEvent *event) override;

private:

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_image_item;
    QGraphicsRectItem *m_placeholder_rect;
    ZoomMode m_zoom_mode;
    qreal m_zoom_factor;

    void
    applyZoom();

    void
    createPlaceholder();

};

#endif // GUI_SCANPREVIEWWIDGET_HPP

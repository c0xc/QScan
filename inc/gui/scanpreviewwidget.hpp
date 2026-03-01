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

#ifndef GUI_SCANPREVIEWWIDGET_HPP
#define GUI_SCANPREVIEWWIDGET_HPP

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QImage>
#include <QRectF>

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

    /**
     * Enable/disable the crop overlay (visual + interactive).
     */
    void
    setCropOverlayEnabled(bool enabled);

    bool
    cropOverlayEnabled() const;

    /**
     * Set/get crop rect in normalized coordinates (0..1) relative to the displayed image.
     * When set while an image is shown, an overlay is drawn.
     */
    void
    setCropRectNormalized(const QRectF &rect);

    QRectF
    cropRectNormalized() const;

signals:

    void
    cropRectEdited(const QRectF &normalized_rect);

protected:

    void
    changeEvent(QEvent *event) override;

    void
    resizeEvent(QResizeEvent *event) override;

    void
    mousePressEvent(QMouseEvent *event) override;

    void
    mouseMoveEvent(QMouseEvent *event) override;

    void
    mouseReleaseEvent(QMouseEvent *event) override;

    void
    leaveEvent(QEvent *event) override;

private:

    enum DragMode
    {
        DragNone,
        DragCreate,
        DragMove,
        DragResizeTL,
        DragResizeTR,
        DragResizeBL,
        DragResizeBR,
        DragResizeL,
        DragResizeR,
        DragResizeT,
        DragResizeB
    };

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_image_item;
    QGraphicsRectItem *m_placeholder_rect;
    QGraphicsRectItem *m_crop_rect_item;
    QGraphicsPathItem *m_crop_mask_item;
    QGraphicsPathItem *m_crop_mask_pattern_item;
    ZoomMode m_zoom_mode;
    qreal m_zoom_factor;

    bool m_crop_overlay_enabled;
    bool m_crop_hovered;
    bool m_crop_dragging;
    QRectF m_crop_rect_norm;
    QRectF m_drag_start_rect_scene;
    QPointF m_drag_start_scene;
    Qt::CursorShape m_prev_cursor;
    DragMode m_drag_mode;
    QGraphicsView::DragMode m_prev_drag_mode;

    void
    applyZoom();

    void
    createPlaceholder();

    QRectF
    imageSceneRect() const;

    void
    ensureCropOverlayItem();

    void
    ensureCropMaskItem();

    void
    updateCropOverlayGeometry();

    void
    updateCropMaskGeometry();

    void
    updateCropOverlayPen();

    bool
    updateHoverState(const QPoint &view_pos);

    DragMode
    pickDragMode(const QPointF &scene_pos, qreal tol_scene) const;

    QRectF
    clampRectToImage(const QRectF &scene_rect) const;

};

#endif //GUI_SCANPREVIEWWIDGET_HPP

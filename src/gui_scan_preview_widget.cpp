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

#include <QResizeEvent>
#include <QPen>
#include <QBrush>
#include <QMouseEvent>
#include <QEvent>
#include <QLineF>
#include <QPainterPath>
#include <QPalette>

#include "gui/scanpreviewwidget.hpp"

ScanPreviewWidget::ScanPreviewWidget(QWidget *parent)
                 : QGraphicsView(parent),
                   m_scene(0),
                   m_image_item(0),
                   m_placeholder_rect(0),
                   m_crop_rect_item(0),
                   m_crop_mask_item(0),
                   m_crop_mask_pattern_item(0),
                   m_zoom_mode(FIT_TO_WINDOW),
                   m_zoom_factor(1.0),
                   m_crop_overlay_enabled(false),
                   m_crop_hovered(false),
                   m_crop_dragging(false),
                   m_crop_rect_norm(0.0, 0.0, 1.0, 1.0),
                   m_drag_start_rect_scene(),
                   m_drag_start_scene(),
                   m_prev_cursor(Qt::ArrowCursor),
                   m_drag_mode(ScanPreviewWidget::DragNone),
                   m_prev_drag_mode(QGraphicsView::ScrollHandDrag)
{
    //Create scene
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    //Configure view
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    //Non-white background so page edges remain visible
    setBackgroundBrush(palette().color(QPalette::Mid));

    //Show placeholder initially
    showPlaceholder();
}

void
ScanPreviewWidget::changeEvent(QEvent *event)
{
    QGraphicsView::changeEvent(event);

    if (!event)
        return;

    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
    {
        //Keep background in sync with the active theme
        setBackgroundBrush(palette().color(QPalette::Mid));
    }
}

ScanPreviewWidget::~ScanPreviewWidget()
{
}

void
ScanPreviewWidget::showPlaceholder()
{
    clear();
    createPlaceholder();
    applyZoom();
}

void
ScanPreviewWidget::showImage(const QImage &image)
{
    clear();

    if (!image.isNull())
    {
        //Base pixmap item
        m_image_item = m_scene->addPixmap(QPixmap::fromImage(image));
        m_scene->setSceneRect(m_scene->itemsBoundingRect());

        //Overlay item when enabled
        ensureCropOverlayItem();
        updateCropOverlayGeometry();
        updateCropOverlayPen();

        applyZoom();
    }
}

void
ScanPreviewWidget::clear()
{
    m_scene->clear();
    m_image_item = 0;
    m_placeholder_rect = 0;
    m_crop_rect_item = 0;
    m_crop_mask_item = 0;
    m_crop_mask_pattern_item = 0;
    m_crop_hovered = false;
    m_crop_dragging = false;
    m_drag_mode = ScanPreviewWidget::DragNone;
}

void
ScanPreviewWidget::setCropOverlayEnabled(bool enabled)
{
    //Toggle overlay visibility and interaction
    m_crop_overlay_enabled = enabled;

    if (!m_crop_overlay_enabled)
    {
        m_crop_hovered = false;
        m_crop_dragging = false;
        m_drag_mode = ScanPreviewWidget::DragNone;
    }

    ensureCropOverlayItem();
    updateCropOverlayGeometry();
    updateCropOverlayPen();
    viewport()->update();
}

bool
ScanPreviewWidget::cropOverlayEnabled() const
{
    return m_crop_overlay_enabled;
}

void
ScanPreviewWidget::setCropRectNormalized(const QRectF &rect)
{
    //Persist crop rect in normalized coordinates
    QRectF r = rect;
    if (!r.isValid() || r.isNull())
        r = QRectF(0.0, 0.0, 1.0, 1.0);

    //Clamp to [0..1]
    const QRectF unit(0.0, 0.0, 1.0, 1.0);
    r = r.intersected(unit);
    if (!r.isValid() || r.isNull())
        r = QRectF(0.0, 0.0, 1.0, 1.0);

    m_crop_rect_norm = r;

    ensureCropOverlayItem();
    updateCropOverlayGeometry();
    updateCropOverlayPen();
    viewport()->update();
}

QRectF
ScanPreviewWidget::cropRectNormalized() const
{
    return m_crop_rect_norm;
}

void
ScanPreviewWidget::setZoomMode(ZoomMode mode)
{
    m_zoom_mode = mode;
    applyZoom();
}

ScanPreviewWidget::ZoomMode
ScanPreviewWidget::zoomMode() const
{
    return m_zoom_mode;
}

void
ScanPreviewWidget::setZoomFactor(qreal factor)
{
    m_zoom_factor = factor;
    if (m_zoom_mode == CUSTOM)
    {
        applyZoom();
    }
}

void
ScanPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    applyZoom();
}

void
ScanPreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event && (event->button() == Qt::LeftButton) && m_crop_overlay_enabled && m_crop_rect_item && m_image_item)
    {
        const QPoint view_pos = event->pos();
        const QPointF scene_pos = mapToScene(view_pos);

        const QRectF rect_scene = m_crop_rect_item->rect();
        const QRectF img_scene = imageSceneRect();

        //Hit tolerance from view pixels -> scene units
        const QPointF s0 = mapToScene(QPoint(0, 0));
        const QPointF s1 = mapToScene(QPoint(10, 0));
        const qreal tol_scene = qMax<qreal>(3.0, QLineF(s0, s1).length());

        const QRectF unit(0.0, 0.0, 1.0, 1.0);
        const bool have_rect = m_crop_rect_item->isVisible() && (m_crop_rect_norm != unit);

        if (!img_scene.isEmpty() && have_rect && rect_scene.isValid() && rect_scene.adjusted(-tol_scene, -tol_scene, tol_scene, tol_scene).contains(scene_pos))
        {
            const DragMode mode = pickDragMode(scene_pos, tol_scene);
            if (mode != DragNone)
            {
                //Begin crop drag
                m_prev_drag_mode = dragMode();
                setDragMode(QGraphicsView::NoDrag);

                m_crop_dragging = true;
                m_drag_mode = mode;
                m_drag_start_scene = scene_pos;
                m_drag_start_rect_scene = rect_scene;

                event->accept();
                return;
            }
        }

        if (!img_scene.isEmpty() && img_scene.contains(scene_pos))
        {
            //Drag-create when there is no crop rect yet
            if (!have_rect)
            {
                m_prev_drag_mode = dragMode();
                setDragMode(QGraphicsView::NoDrag);

                m_crop_dragging = true;
                m_drag_mode = DragCreate;
                m_drag_start_scene = scene_pos;
                m_drag_start_rect_scene = QRectF(scene_pos, scene_pos);

                //Make overlay visible immediately
                m_crop_rect_norm = QRectF(0.0, 0.0, 1.0, 1.0);
                m_crop_rect_item->setVisible(true);
                m_crop_rect_item->setRect(clampRectToImage(m_drag_start_rect_scene.normalized()));
                updateCropOverlayPen();
                updateCropMaskGeometry();

                event->accept();
                return;
            }
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void
ScanPreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!event)
        return;

    if (m_crop_dragging && m_crop_rect_item && m_image_item)
    {
        //Drag delta in scene space
        const QPointF scene_pos = mapToScene(event->pos());
        const QPointF delta = scene_pos - m_drag_start_scene;

        QRectF r = m_drag_start_rect_scene;

        //Min size from view pixels -> scene units
        const QPointF s0 = mapToScene(QPoint(0, 0));
        const QPointF s1 = mapToScene(QPoint(12, 0));
        const qreal min_scene = qMax<qreal>(6.0, QLineF(s0, s1).length());

        //Resize or move by active drag mode
        switch (m_drag_mode)
        {
            case DragCreate:
                r = QRectF(m_drag_start_scene, m_drag_start_scene + delta).normalized();
                break;

            case DragMove:
                r.translate(delta);
                break;

            case DragResizeL:
                r.setLeft(r.left() + delta.x());
                break;
            case DragResizeR:
                r.setRight(r.right() + delta.x());
                break;
            case DragResizeT:
                r.setTop(r.top() + delta.y());
                break;
            case DragResizeB:
                r.setBottom(r.bottom() + delta.y());
                break;

            case DragResizeTL:
                r.setLeft(r.left() + delta.x());
                r.setTop(r.top() + delta.y());
                break;
            case DragResizeTR:
                r.setRight(r.right() + delta.x());
                r.setTop(r.top() + delta.y());
                break;
            case DragResizeBL:
                r.setLeft(r.left() + delta.x());
                r.setBottom(r.bottom() + delta.y());
                break;
            case DragResizeBR:
                r.setRight(r.right() + delta.x());
                r.setBottom(r.bottom() + delta.y());
                break;

            case DragNone:
                break;
        }

        if (m_drag_mode != DragCreate && r.width() < min_scene)
        {
            //Clamp width
            if (m_drag_mode == DragResizeL || m_drag_mode == DragResizeTL || m_drag_mode == DragResizeBL)
                r.setLeft(r.right() - min_scene);
            else
                r.setRight(r.left() + min_scene);
        }

        if (m_drag_mode != DragCreate && r.height() < min_scene)
        {
            //Clamp height
            if (m_drag_mode == DragResizeT || m_drag_mode == DragResizeTL || m_drag_mode == DragResizeTR)
                r.setTop(r.bottom() - min_scene);
            else
                r.setBottom(r.top() + min_scene);
        }

        if (m_drag_mode == DragCreate)
        {
            if (r.width() < min_scene)
                r.setRight(r.left() + min_scene);
            if (r.height() < min_scene)
                r.setBottom(r.top() + min_scene);
        }

        //Apply scene rect and update normalized rect
        r = clampRectToImage(r);
        m_crop_rect_item->setRect(r);

        const QRectF img_scene = imageSceneRect();
        if (!img_scene.isEmpty())
        {
            QRectF norm;
            norm.setX((r.x() - img_scene.x()) / img_scene.width());
            norm.setY((r.y() - img_scene.y()) / img_scene.height());
            norm.setWidth(r.width() / img_scene.width());
            norm.setHeight(r.height() / img_scene.height());

            const QRectF unit(0.0, 0.0, 1.0, 1.0);
            m_crop_rect_norm = norm.intersected(unit);
        }

        updateCropMaskGeometry();

        viewport()->update();
        event->accept();
        return;
    }

    //Hover state changes pen style
    updateHoverState(event->pos());
    QGraphicsView::mouseMoveEvent(event);
}

void
ScanPreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event && (event->button() == Qt::LeftButton) && m_crop_dragging)
    {
        //End crop drag
        m_crop_dragging = false;
        m_drag_mode = DragNone;

        setDragMode(m_prev_drag_mode);

        emit cropRectEdited(m_crop_rect_norm);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void
ScanPreviewWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    if (m_crop_hovered)
    {
        m_crop_hovered = false;
        updateCropOverlayPen();
        viewport()->update();
    }

    if (viewport())
        viewport()->unsetCursor();
}

void
ScanPreviewWidget::applyZoom()
{
    if (!m_scene)
        return;

    QRectF scene_rect = m_scene->sceneRect();
    if (scene_rect.isEmpty())
        return;

    resetTransform();

    switch (m_zoom_mode)
    {
        case FIT_TO_WINDOW:
            fitInView(scene_rect, Qt::KeepAspectRatio);
            break;

        case FIT_TO_WIDTH:
        {
            qreal scale = viewport()->width() / scene_rect.width();
            QTransform transform;
            transform.scale(scale, scale);
            setTransform(transform);
            break;
        }

        case ACTUAL_SIZE:
            //No scaling
            break;

        case CUSTOM:
        {
            QTransform transform;
            transform.scale(m_zoom_factor, m_zoom_factor);
            setTransform(transform);
            break;
        }
    }
}

void
ScanPreviewWidget::createPlaceholder()
{
    //A4 aspect ratio (1:1.414)
    qreal width = 400;
    qreal height = width * 1.414;

    //Create light gray dashed rectangle
    QPen pen(Qt::gray, 2, Qt::DashLine);
    QBrush brush(QColor(240, 240, 240));

    m_placeholder_rect = m_scene->addRect(0, 0, width, height, pen, brush);

    //Add text
    QGraphicsTextItem *text = m_scene->addText("No scan yet\n\nClick Scan to begin");
    text->setDefaultTextColor(Qt::gray);
    QFont font = text->font();
    font.setPointSize(14);
    text->setFont(font);

    //Center text
    QRectF text_rect = text->boundingRect();
    text->setPos((width - text_rect.width()) / 2,
                 (height - text_rect.height()) / 2);

    //Set scene rect
    m_scene->setSceneRect(0, 0, width, height);
}

QRectF
ScanPreviewWidget::imageSceneRect() const
{
    if (!m_image_item)
        return QRectF();
    return m_image_item->sceneBoundingRect();
}

void
ScanPreviewWidget::ensureCropOverlayItem()
{
    if (!m_scene || !m_image_item)
        return;

    if (!m_crop_overlay_enabled)
        return;

    if (!m_crop_rect_item)
    {
        //Scene overlay item above image
        m_crop_rect_item = m_scene->addRect(QRectF());
        m_crop_rect_item->setZValue(10.0);
        m_crop_rect_item->setBrush(Qt::NoBrush);
    }

    ensureCropMaskItem();
}

void
ScanPreviewWidget::ensureCropMaskItem()
{
    if (!m_scene || !m_image_item)
        return;

    if (!m_crop_overlay_enabled)
        return;

    if (!m_crop_mask_item)
    {
        m_crop_mask_item = m_scene->addPath(QPainterPath());
        m_crop_mask_item->setZValue(9.0);
        m_crop_mask_item->setPen(Qt::NoPen);
        m_crop_mask_item->setBrush(QColor(0, 0, 0, 120));
        m_crop_mask_item->setVisible(false);
        m_crop_mask_item->setAcceptedMouseButtons(Qt::NoButton);
    }

    if (!m_crop_mask_pattern_item)
    {
        m_crop_mask_pattern_item = m_scene->addPath(QPainterPath());
        m_crop_mask_pattern_item->setZValue(9.5);
        m_crop_mask_pattern_item->setPen(Qt::NoPen);

        //Make the outside-of-crop region more obvious without touching pixels
        //A subtle/medium hatch over the dimmed mask is more visible than brightness alone
        QBrush brush;
        brush.setStyle(Qt::DiagCrossPattern);
        brush.setColor(QColor(255, 255, 255, 50));
        m_crop_mask_pattern_item->setBrush(brush);

        m_crop_mask_pattern_item->setVisible(false);
        m_crop_mask_pattern_item->setAcceptedMouseButtons(Qt::NoButton);
    }
}

void
ScanPreviewWidget::updateCropOverlayGeometry()
{
    if (!m_crop_rect_item)
        return;

    if (!m_crop_overlay_enabled || !m_image_item)
    {
        m_crop_rect_item->setVisible(false);
        updateCropMaskGeometry();
        return;
    }

    const QRectF unit(0.0, 0.0, 1.0, 1.0);
    const QRectF r_norm = m_crop_rect_norm.intersected(unit);
    if (!r_norm.isValid() || r_norm.isNull() || r_norm == unit)
    {
        //Full-rect means no crop overlay
        m_crop_rect_item->setVisible(false);
        updateCropMaskGeometry();
        return;
    }

    const QRectF img_scene = imageSceneRect();
    if (img_scene.isEmpty())
    {
        m_crop_rect_item->setVisible(false);
        updateCropMaskGeometry();
        return;
    }

    QRectF r_scene;
    r_scene.setX(img_scene.x() + r_norm.x() * img_scene.width());
    r_scene.setY(img_scene.y() + r_norm.y() * img_scene.height());
    r_scene.setWidth(r_norm.width() * img_scene.width());
    r_scene.setHeight(r_norm.height() * img_scene.height());
    r_scene = clampRectToImage(r_scene);

    m_crop_rect_item->setRect(r_scene);
    m_crop_rect_item->setVisible(true);

    updateCropMaskGeometry();
}

void
ScanPreviewWidget::updateCropMaskGeometry()
{
    if (!m_crop_mask_item)
        return;

    if (!m_crop_mask_pattern_item)
        return;

    if (!m_crop_overlay_enabled || !m_image_item || !m_crop_rect_item || !m_crop_rect_item->isVisible())
    {
        m_crop_mask_item->setVisible(false);
        m_crop_mask_pattern_item->setVisible(false);
        return;
    }

    const QRectF img_scene = imageSceneRect();
    const QRectF crop_scene = m_crop_rect_item->rect().normalized();
    if (img_scene.isEmpty() || !crop_scene.isValid() || crop_scene.isNull())
    {
        m_crop_mask_item->setVisible(false);
        m_crop_mask_pattern_item->setVisible(false);
        return;
    }

    const qreal eps = 0.5;
    const bool is_full = (qAbs(crop_scene.left() - img_scene.left()) <= eps)
                      && (qAbs(crop_scene.top() - img_scene.top()) <= eps)
                      && (qAbs(crop_scene.right() - img_scene.right()) <= eps)
                      && (qAbs(crop_scene.bottom() - img_scene.bottom()) <= eps);
    if (is_full)
    {
        m_crop_mask_item->setVisible(false);
        m_crop_mask_pattern_item->setVisible(false);
        return;
    }

    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.addRect(img_scene);
    path.addRect(crop_scene);
    m_crop_mask_item->setPath(path);
    m_crop_mask_item->setVisible(true);

    m_crop_mask_pattern_item->setPath(path);
    m_crop_mask_pattern_item->setVisible(true);
}

void
ScanPreviewWidget::updateCropOverlayPen()
{
    if (!m_crop_rect_item)
        return;

    QColor color = m_crop_hovered ? Qt::blue : Qt::gray;
    color.setAlpha(m_crop_hovered ? 220 : 160);

    QPen pen(color, 2.0);
    pen.setStyle(m_crop_hovered ? Qt::SolidLine : Qt::DashLine);
    m_crop_rect_item->setPen(pen);
}

bool
ScanPreviewWidget::updateHoverState(const QPoint &view_pos)
{
    Qt::CursorShape desired_cursor = Qt::ArrowCursor;
    bool hovered = false;

    if (m_crop_overlay_enabled && m_image_item)
    {
        const QPointF scene_pos = mapToScene(view_pos);
        const QRectF img_scene = imageSceneRect();
        const QRectF unit(0.0, 0.0, 1.0, 1.0);
        const bool have_rect = m_crop_rect_item && m_crop_rect_item->isVisible() && (m_crop_rect_norm != unit);

        //Hit tolerance from view pixels -> scene units
        const QPointF s0 = mapToScene(QPoint(0, 0));
        const QPointF s1 = mapToScene(QPoint(10, 0));
        const qreal tol_scene = qMax<qreal>(3.0, QLineF(s0, s1).length());

        if (have_rect && m_crop_rect_item)
        {
            const DragMode mode = pickDragMode(scene_pos, tol_scene);
            if (mode != DragNone)
            {
                hovered = true;
                switch (mode)
                {
                    case DragMove:
                        desired_cursor = Qt::SizeAllCursor;
                        break;

                    case DragResizeL:
                    case DragResizeR:
                        desired_cursor = Qt::SizeHorCursor;
                        break;
                    case DragResizeT:
                    case DragResizeB:
                        desired_cursor = Qt::SizeVerCursor;
                        break;

                    case DragResizeTL:
                    case DragResizeBR:
                        desired_cursor = Qt::SizeFDiagCursor;
                        break;
                    case DragResizeTR:
                    case DragResizeBL:
                        desired_cursor = Qt::SizeBDiagCursor;
                        break;

                    case DragCreate:
                    case DragNone:
                        desired_cursor = Qt::ArrowCursor;
                        break;
                }
            }
        }
        else
        {
            if (!img_scene.isEmpty() && img_scene.contains(scene_pos))
                desired_cursor = Qt::CrossCursor;
        }
    }

    if (hovered != m_crop_hovered)
    {
        m_crop_hovered = hovered;
        updateCropOverlayPen();
        viewport()->update();
    }

    if (viewport())
    {
        if (desired_cursor == Qt::ArrowCursor)
            viewport()->unsetCursor();
        else
            viewport()->setCursor(desired_cursor);
    }

    return hovered;
}

ScanPreviewWidget::DragMode
ScanPreviewWidget::pickDragMode(const QPointF &scene_pos, qreal tol_scene) const
{
    if (!m_crop_rect_item)
        return DragNone;

    const QRectF r = m_crop_rect_item->rect();
    if (!r.isValid() || r.isNull())
        return DragNone;

    const QPointF tl = r.topLeft();
    const QPointF tr = r.topRight();
    const QPointF bl = r.bottomLeft();
    const QPointF br = r.bottomRight();

    auto near = [&](const QPointF &a, const QPointF &b) -> bool
    {
        return QLineF(a, b).length() <= tol_scene;
    };

    if (near(scene_pos, tl)) return DragResizeTL;
    if (near(scene_pos, tr)) return DragResizeTR;
    if (near(scene_pos, bl)) return DragResizeBL;
    if (near(scene_pos, br)) return DragResizeBR;

    if (qAbs(scene_pos.x() - r.left()) <= tol_scene) return DragResizeL;
    if (qAbs(scene_pos.x() - r.right()) <= tol_scene) return DragResizeR;
    if (qAbs(scene_pos.y() - r.top()) <= tol_scene) return DragResizeT;
    if (qAbs(scene_pos.y() - r.bottom()) <= tol_scene) return DragResizeB;

    if (r.contains(scene_pos))
        return DragMove;

    return DragNone;
}

QRectF
ScanPreviewWidget::clampRectToImage(const QRectF &scene_rect) const
{
    const QRectF img = imageSceneRect();
    if (img.isEmpty())
        return scene_rect;

    QRectF r = scene_rect;
    if (r.left() < img.left()) r.moveLeft(img.left());
    if (r.top() < img.top()) r.moveTop(img.top());
    if (r.right() > img.right()) r.moveRight(img.right());
    if (r.bottom() > img.bottom()) r.moveBottom(img.bottom());

    //Still ensure it's inside even after potential size changes
    r = r.intersected(img);
    return r;
}

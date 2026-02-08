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

#include "gui/scanpreviewwidget.hpp"
#include <QResizeEvent>
#include <QPen>
#include <QBrush>

ScanPreviewWidget::ScanPreviewWidget(QWidget *parent)
                 : QGraphicsView(parent),
                   m_scene(0),
                   m_image_item(0),
                   m_placeholder_rect(0),
                   m_zoom_mode(FIT_TO_WINDOW),
                   m_zoom_factor(1.0)
{
    // Create scene
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    
    // Configure view
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    
    // Show placeholder initially
    showPlaceholder();
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
        m_image_item = m_scene->addPixmap(QPixmap::fromImage(image));
        applyZoom();
    }
}

void
ScanPreviewWidget::clear()
{
    m_scene->clear();
    m_image_item = 0;
    m_placeholder_rect = 0;
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
            // No scaling
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
    // A4 aspect ratio: 210mm x 297mm = 1:1.414
    qreal width = 400;
    qreal height = width * 1.414;
    
    // Create a light gray rectangle with dashed border
    QPen pen(Qt::gray, 2, Qt::DashLine);
    QBrush brush(QColor(240, 240, 240));
    
    m_placeholder_rect = m_scene->addRect(0, 0, width, height, pen, brush);
    
    // Add text
    QGraphicsTextItem *text = m_scene->addText("No scan yet\n\nClick Scan to begin");
    text->setDefaultTextColor(Qt::gray);
    QFont font = text->font();
    font.setPointSize(14);
    text->setFont(font);
    
    // Center text in rectangle
    QRectF text_rect = text->boundingRect();
    text->setPos((width - text_rect.width()) / 2,
                 (height - text_rect.height()) / 2);
    
    // Set scene rect
    m_scene->setSceneRect(0, 0, width, height);
}

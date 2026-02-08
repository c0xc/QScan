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

#include "document/scannedpage.hpp"
#include <QTransform>

ScannedPage::ScannedPage()
           : m_rotation(0),
             m_crop_rect(0.0, 0.0, 1.0, 1.0)
{
    m_scan_time = QDateTime::currentDateTime();
}

ScannedPage::ScannedPage(const QImage &img)
           : m_image(img),
             m_rotation(0),
             m_crop_rect(0.0, 0.0, 1.0, 1.0)
{
    m_scan_time = QDateTime::currentDateTime();
}

void
ScannedPage::setImage(const QImage &img)
{
    m_image = img;
}

QImage
ScannedPage::rawImage() const
{
    return m_image;
}

QImage
ScannedPage::processedImage() const
{
    QImage result = m_image;
    
    // Apply rotation
    if (m_rotation != 0)
    {
        QTransform transform;
        transform.rotate(m_rotation);
        result = result.transformed(transform);
    }
    
    // Apply crop
    if (m_crop_rect != QRectF(0.0, 0.0, 1.0, 1.0))
    {
        int x = m_crop_rect.x() * result.width();
        int y = m_crop_rect.y() * result.height();
        int w = m_crop_rect.width() * result.width();
        int h = m_crop_rect.height() * result.height();
        result = result.copy(x, y, w, h);
    }
    
    return result;
}

void
ScannedPage::setRotation(int degrees)
{
    m_rotation = degrees;
}

int
ScannedPage::rotation() const
{
    return m_rotation;
}

void
ScannedPage::setCropRect(const QRectF &rect)
{
    m_crop_rect = rect;
}

QRectF
ScannedPage::cropRect() const
{
    return m_crop_rect;
}

QPixmap
ScannedPage::thumbnail(const QSize &size) const
{
    QImage img = processedImage();
    return QPixmap::fromImage(img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void
ScannedPage::setScanTime(const QDateTime &time)
{
    m_scan_time = time;
}

QDateTime
ScannedPage::scanTime() const
{
    return m_scan_time;
}

void
ScannedPage::setSourceName(const QString &name)
{
    m_source_name = name;
}

QString
ScannedPage::sourceName() const
{
    return m_source_name;
}

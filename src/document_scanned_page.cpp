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

#include <QColorSpace>
#include <QMetaEnum>
#include <QTransform>

#include "document/scannedpage.hpp"

ScannedPage::ScannedPage()
           : m_rotation(0),
                         m_crop_rect(0.0, 0.0, 1.0, 1.0),
                         m_acquisition_kind(AcquisitionKind::Scan),
                         m_auto_crop_applied(false),
                         m_scan_resolution_dpi(0),
                         m_scan_area_mm(),
                         m_auto_page_size(false),
                         m_used_adf(false),
                         m_used_duplex(false),
                         m_reported_document_size_valid(false),
                         m_reported_document_size_mm(),
                         m_reported_paper_name(),
                         m_backend_kind(),
                         m_backend_details(),
                         m_effective_resolution_valid(false),
                         m_effective_resolution_dpi(0),
                         m_effective_color_mode_valid(false),
                         m_effective_color_mode()
{
    m_scan_time = QDateTime::currentDateTime();
}

ScannedPage::ScannedPage(const QImage &img)
           : m_image(img),
             m_rotation(0),
                         m_crop_rect(0.0, 0.0, 1.0, 1.0),
             m_acquisition_kind(AcquisitionKind::Scan),
             m_auto_crop_applied(false),
                         m_scan_resolution_dpi(0),
                         m_scan_area_mm(),
                         m_auto_page_size(false),
                         m_used_adf(false),
             m_used_duplex(false),
             m_reported_document_size_valid(false),
             m_reported_document_size_mm(),
                         m_reported_paper_name(),
                         m_backend_kind(),
                         m_backend_details(),
                         m_effective_resolution_valid(false),
                         m_effective_resolution_dpi(0),
                         m_effective_color_mode_valid(false),
                         m_effective_color_mode()
{
    m_scan_time = QDateTime::currentDateTime();
}

void
ScannedPage::setAcquisitionKind(AcquisitionKind kind)
{
    m_acquisition_kind = kind;
}

ScannedPage::AcquisitionKind
ScannedPage::acquisitionKind() const
{
    return m_acquisition_kind;
}

void
ScannedPage::setAutoCropApplied(bool applied)
{
    m_auto_crop_applied = applied;
}

bool
ScannedPage::autoCropApplied() const
{
    return m_auto_crop_applied;
}

void
ScannedPage::setImage(const QImage &img)
{
    m_image = img;
}

void
ScannedPage::setOriginalImage(const QImage &img)
{
    m_original_image = img;
}

bool
ScannedPage::hasOriginalImage() const
{
    return !m_original_image.isNull();
}

QImage
ScannedPage::originalImage() const
{
    return m_original_image;
}

void
ScannedPage::setWarpedImage(const QImage &img)
{
    m_warped_image = img;
}

bool
ScannedPage::hasWarpedImage() const
{
    return !m_warped_image.isNull();
}

QImage
ScannedPage::warpedImage() const
{
    return m_warped_image;
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
    
    //Apply rotation
    if (m_rotation != 0)
    {
        QTransform transform;
        transform.rotate(m_rotation);
        result = result.transformed(transform);
    }
    
    //Apply crop
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

QImage
ScannedPage::rotatedImage() const
{
    return currentImage();
}

QImage
ScannedPage::currentImage() const
{
    QImage result = m_image;

    if (m_rotation != 0)
    {
        QTransform transform;
        transform.rotate(m_rotation);
        result = result.transformed(transform);
    }

    return result;
}

QSize
ScannedPage::pixelSize() const
{
    return m_image.size();
}

int
ScannedPage::imageDepthBits() const
{
    return m_image.depth();
}

int
ScannedPage::bytesPerLine() const
{
    return m_image.bytesPerLine();
}

bool
ScannedPage::hasAlphaChannel() const
{
    return m_image.hasAlphaChannel();
}

bool
ScannedPage::isGrayscale() const
{
    return m_image.isGrayscale();
}

int
ScannedPage::imageFormat() const
{
    return (int)m_image.format();
}

QString
ScannedPage::imageFormatName() const
{
    switch (m_image.format())
    {
        case QImage::Format_Invalid: return QStringLiteral("Invalid");
        case QImage::Format_Mono: return QStringLiteral("Mono");
        case QImage::Format_MonoLSB: return QStringLiteral("MonoLSB");
        case QImage::Format_Indexed8: return QStringLiteral("Indexed8");
        case QImage::Format_RGB32: return QStringLiteral("RGB32");
        case QImage::Format_ARGB32: return QStringLiteral("ARGB32");
        case QImage::Format_ARGB32_Premultiplied: return QStringLiteral("ARGB32_Premultiplied");
        case QImage::Format_RGB16: return QStringLiteral("RGB16");
        case QImage::Format_RGB888: return QStringLiteral("RGB888");
        case QImage::Format_RGBX8888: return QStringLiteral("RGBX8888");
        case QImage::Format_RGBA8888: return QStringLiteral("RGBA8888");
        case QImage::Format_RGBA8888_Premultiplied: return QStringLiteral("RGBA8888_Premultiplied");
        case QImage::Format_Grayscale8: return QStringLiteral("Grayscale8");
        case QImage::Format_Grayscale16: return QStringLiteral("Grayscale16");
        case QImage::Format_BGR888: return QStringLiteral("BGR888");
        default: break;
    }

    return QStringLiteral("Format(%1)").arg((int)m_image.format());
}

QString
ScannedPage::imageColorSummary() const
{
    QString cs;
    if (m_image.colorSpace().isValid())
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        cs = m_image.colorSpace().description();
#else
        const QColorSpace space = m_image.colorSpace();

        const QMetaEnum prim_enum = QMetaEnum::fromType<QColorSpace::Primaries>();
        const char *prim_key = prim_enum.valueToKey((int)space.primaries());

        const QMetaEnum tf_enum = QMetaEnum::fromType<QColorSpace::TransferFunction>();
        const char *tf_key = tf_enum.valueToKey((int)space.transferFunction());

        const QString prim = prim_key ? QString::fromLatin1(prim_key) : QStringLiteral("Primaries(?)");
        const QString tf = tf_key ? QString::fromLatin1(tf_key) : QStringLiteral("TransferFunction(?)");

        cs = QStringLiteral("%1/%2").arg(prim, tf);
#endif
    }
    if (cs.isEmpty())
        cs = QStringLiteral("N/A");

    return QStringLiteral("%1×%2 px · %3 bpp · %4 · alpha:%5 · gray:%6 · cs:%7")
        .arg(m_image.width())
        .arg(m_image.height())
        .arg(m_image.depth())
        .arg(imageFormatName())
        .arg(m_image.hasAlphaChannel() ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(m_image.isGrayscale() ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(cs);
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

void
ScannedPage::setSourceDescription(const QString &description)
{
    m_source_description = description;
}

QString
ScannedPage::sourceDescription() const
{
    return m_source_description;
}

void
ScannedPage::setScanResolutionDpi(int dpi)
{
    m_scan_resolution_dpi = dpi;
}

int
ScannedPage::scanResolutionDpi() const
{
    return m_scan_resolution_dpi;
}

void
ScannedPage::setScanColorMode(const QString &mode)
{
    m_scan_color_mode = mode;
}

QString
ScannedPage::scanColorMode() const
{
    return m_scan_color_mode;
}

void
ScannedPage::setScanAreaMm(const QSizeF &mm_size)
{
    m_scan_area_mm = mm_size;
}

QSizeF
ScannedPage::scanAreaMm() const
{
    return m_scan_area_mm;
}

void
ScannedPage::setAutoPageSize(bool enabled)
{
    m_auto_page_size = enabled;
}

bool
ScannedPage::autoPageSize() const
{
    return m_auto_page_size;
}

void
ScannedPage::setUsedAdf(bool enabled)
{
    m_used_adf = enabled;
}

bool
ScannedPage::usedAdf() const
{
    return m_used_adf;
}

void
ScannedPage::setUsedDuplex(bool enabled)
{
    m_used_duplex = enabled;
}

bool
ScannedPage::usedDuplex() const
{
    return m_used_duplex;
}

void
ScannedPage::setReportedDocumentSizeMm(const QSizeF &mm_size, const QString &paper_name)
{
    m_reported_document_size_valid = !mm_size.isEmpty();
    m_reported_document_size_mm = mm_size;
    m_reported_paper_name = paper_name;
}

bool
ScannedPage::hasReportedDocumentSize() const
{
    return m_reported_document_size_valid;
}

QSizeF
ScannedPage::reportedDocumentSizeMm() const
{
    return m_reported_document_size_mm;
}

QString
ScannedPage::reportedPaperName() const
{
    return m_reported_paper_name;
}

void
ScannedPage::setBackendKind(const QString &kind)
{
    m_backend_kind = kind;
}

QString
ScannedPage::backendKind() const
{
    return m_backend_kind;
}

void
ScannedPage::setBackendDetails(const QVariantMap &details)
{
    m_backend_details = details;
}

QVariantMap
ScannedPage::backendDetails() const
{
    return m_backend_details;
}

void
ScannedPage::setEffectiveResolutionDpi(int dpi)
{
    m_effective_resolution_valid = (dpi > 0);
    m_effective_resolution_dpi = dpi;
}

bool
ScannedPage::hasEffectiveResolutionDpi() const
{
    return m_effective_resolution_valid;
}

int
ScannedPage::effectiveResolutionDpi() const
{
    return m_effective_resolution_dpi;
}

void
ScannedPage::setEffectiveColorMode(const QString &mode)
{
    m_effective_color_mode_valid = !mode.isEmpty();
    m_effective_color_mode = mode;
}

bool
ScannedPage::hasEffectiveColorMode() const
{
    return m_effective_color_mode_valid;
}

QString
ScannedPage::effectiveColorMode() const
{
    return m_effective_color_mode;
}

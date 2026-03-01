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

#include "document/documentexporter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QPdfWriter>
#include <QSet>
#include <QStringList>
#include <QTemporaryFile>

#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
#include <QPageLayout>
#include <QPageSize>
#endif

#ifdef HAVE_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <exception>
#include <string>
#endif

namespace
{

static QString
lowerSuffix(const QString &path)
{
    const QString s = QFileInfo(path).suffix().trimmed().toLower();
    return s;
}

static QString
formatNameForExport(DocumentExporter::ExportFormat format)
{
    switch (format)
    {
        case DocumentExporter::JPG: return QStringLiteral("jpg");
        case DocumentExporter::PNG: return QStringLiteral("png");
        default: break;
    }
    return QString();
}

//Unused but useful+tested functions are not a bad thing (c0xc)
static QString
makePagedFileName(const QString &base_path, int page_index_0, int page_count, const QString &suffix)
{
    QFileInfo info(base_path);

    const QString dir = info.absolutePath();
    QString base = info.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("scan");

    const int digits = page_count >= 1000 ? 4 : 3;
    const QString page_text = QStringLiteral("%1").arg(page_index_0 + 1, digits, 10, QLatin1Char('0'));
    const QString file_name = QStringLiteral("%1_p%2.%3").arg(base, page_text, suffix);

    return QDir(dir).filePath(file_name);
}

static bool
resolvePageSizeMm(const ScannedPage &page, const QImage &image, QSizeF &mm_out, int &dpi_out)
{
    dpi_out = 0;
    mm_out = QSizeF();

    if (page.hasEffectiveResolutionDpi())
        dpi_out = page.effectiveResolutionDpi();
    else if (page.scanResolutionDpi() > 0)
        dpi_out = page.scanResolutionDpi();

    if (page.hasReportedDocumentSize())
    {
        mm_out = page.reportedDocumentSizeMm();
        if (!mm_out.isEmpty())
            return true;
    }

    if (dpi_out <= 0)
        return false;

    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return false;

    const qreal w_mm = (qreal)image.width() * 25.4 / (qreal)dpi_out;
    const qreal h_mm = (qreal)image.height() * 25.4 / (qreal)dpi_out;
    mm_out = QSizeF(w_mm, h_mm);
    return !mm_out.isEmpty();
}

static QString
formatPdfDateUtc(const QDateTime &dt)
{
    if (!dt.isValid())
        return QString();

    const QDateTime utc = dt.toUTC();
    return QStringLiteral("D:%1Z").arg(utc.toString(QStringLiteral("yyyyMMddHHmmss")));
}

#ifdef HAVE_QPDF
static bool
injectPdfMetadataWithQpdf(const Document &doc,
                          const QString &in_pdf,
                          const QString &out_pdf,
                          const QString &title,
                          QString &error_out)
{
    error_out.clear();

    try
    {
        QPDF pdf;
        pdf.processFile(in_pdf.toStdString().c_str());

        QPDFObjectHandle trailer = pdf.getTrailer();
        QPDFObjectHandle info = trailer.getKey("/Info");
        if (!info.isDictionary())
        {
            info = QPDFObjectHandle::newDictionary();
            trailer.replaceKey("/Info", info);
        }

        const QString creator = QStringLiteral("%1 (%2)").arg(doc.creatorProgram(), doc.creatorAuthor());
        const QString author = doc.creatorAuthor();
        const QString creation_date = formatPdfDateUtc(doc.createdTime());

        if (!title.isEmpty())
            info.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title.toUtf8().toStdString()));
        if (!creator.isEmpty())
            info.replaceKey("/Creator", QPDFObjectHandle::newUnicodeString(creator.toUtf8().toStdString()));
        if (!author.isEmpty())
            info.replaceKey("/Author", QPDFObjectHandle::newUnicodeString(author.toUtf8().toStdString()));
        if (!creation_date.isEmpty())
        {
            info.replaceKey("/CreationDate", QPDFObjectHandle::newString(creation_date.toStdString()));
            info.replaceKey("/ModDate", QPDFObjectHandle::newString(creation_date.toStdString()));
        }

        QSet<QString> scanner_names;
        QSet<QString> scanner_descriptions;
        QSet<QString> backend_kinds;
        for (int i = 0; i < doc.pageCount(); i++)
        {
            const ScannedPage &p = doc.page(i);
            if (!p.sourceName().trimmed().isEmpty())
                scanner_names.insert(p.sourceName().trimmed());
            if (!p.sourceDescription().trimmed().isEmpty())
                scanner_descriptions.insert(p.sourceDescription().trimmed());
            if (!p.backendKind().trimmed().isEmpty())
                backend_kinds.insert(p.backendKind().trimmed());
        }

        QStringList scanner_names_list = scanner_names.values();
        scanner_names_list.sort();
        QStringList backend_kinds_list = backend_kinds.values();
        backend_kinds_list.sort();

        const QString scanner_names_text = scanner_names_list.join(QStringLiteral("; "));
        const QString backend_kinds_text = backend_kinds_list.join(QStringLiteral("; "));

        //Standard-ish place: keywords (best effort; limited structure)
        QStringList keywords;
        keywords << QStringLiteral("QScan");
        if (!scanner_names_list.isEmpty())
            keywords << QStringLiteral("scanner=%1").arg(scanner_names_text);
        if (!backend_kinds_list.isEmpty())
            keywords << QStringLiteral("backend=%1").arg(backend_kinds_text);

        info.replaceKey("/Keywords", QPDFObjectHandle::newUnicodeString(keywords.join(QStringLiteral("; ")).toStdString()));

        //Custom fields (exact values; may be ignored by some consumers)
        info.replaceKey("/QScanPageCount", QPDFObjectHandle::newUnicodeString(QString::number(doc.pageCount()).toStdString()));
        if (!scanner_names_text.isEmpty())
            info.replaceKey("/QScanScannerNames", QPDFObjectHandle::newUnicodeString(scanner_names_text.toUtf8().toStdString()));

        if (!scanner_descriptions.isEmpty())
        {
            QStringList desc_list = scanner_descriptions.values();
            desc_list.sort();
            info.replaceKey("/QScanScannerDescriptions", QPDFObjectHandle::newUnicodeString(desc_list.join(QStringLiteral("; ")).toUtf8().toStdString()));
        }

        if (!backend_kinds_text.isEmpty())
            info.replaceKey("/QScanBackendKinds", QPDFObjectHandle::newUnicodeString(backend_kinds_text.toUtf8().toStdString()));

        if (!doc.creatorProgram().trimmed().isEmpty())
            info.replaceKey("/QScanCreatorApp", QPDFObjectHandle::newUnicodeString(doc.creatorProgram().trimmed().toUtf8().toStdString()));
        if (!doc.creatorAuthor().trimmed().isEmpty())
            info.replaceKey("/QScanCreatorAuthor", QPDFObjectHandle::newUnicodeString(doc.creatorAuthor().trimmed().toUtf8().toStdString()));

        QPDFWriter writer(pdf, out_pdf.toStdString().c_str());
        writer.setStaticID(true);
        writer.write();
        return true;
    }
    catch (const QPDFExc &e)
    {
        error_out = QString::fromStdString(e.what());
        return false;
    }
    catch (const std::exception &e)
    {
        error_out = QString::fromStdString(e.what());
        return false;
    }
    catch (...)
    {
        error_out = QStringLiteral("Unknown QPDF error");
        return false;
    }
}
#endif

}

DocumentExporter::DocumentExporter()
{
}

bool
DocumentExporter::exportDocument(const Document &doc,
                                const QString &path,
                                ExportFormat format)
{
    m_last_error.clear();

    if (doc.pageCount() <= 0)
    {
        m_last_error = QStringLiteral("No pages to export");
        return false;
    }

    if (path.trimmed().isEmpty())
    {
        m_last_error = QStringLiteral("Empty output path");
        return false;
    }

    if (format == PDF)
        return exportToPDF(doc, path);

    const QString fmt = formatNameForExport(format);
    if (fmt.isEmpty())
    {
        m_last_error = QStringLiteral("Unsupported export format");
        return false;
    }

    //Image formats are single-page only; caller must decide which page to export
    if (doc.pageCount() != 1)
    {
        m_last_error = QStringLiteral("This format supports only a single page");
        return false;
    }

    return exportSingleImage(doc.page(0), path, format);
}

bool
DocumentExporter::exportSingleImage(const ScannedPage &page,
                                   const QString &path,
                                   ExportFormat format)
{
    m_last_error.clear();

    if (path.trimmed().isEmpty())
    {
        m_last_error = QStringLiteral("Empty output path");
        return false;
    }

    const QImage image = page.processedImage();
    if (image.isNull())
    {
        m_last_error = QStringLiteral("No image data");
        return false;
    }

    QString fmt = formatNameForExport(format);
    if (fmt.isEmpty())
    {
        m_last_error = QStringLiteral("Unsupported export format");
        return false;
    }

    const QString suffix = lowerSuffix(path);
    if (!suffix.isEmpty())
        fmt = suffix;

    return exportImage(image, path, fmt, page.sourceName(), page.scanTime());
}

QString
DocumentExporter::lastError() const
{
    return m_last_error;
}

bool
DocumentExporter::exportToPDF(const Document &doc, const QString &path)
{
    m_last_error.clear();

    if (doc.pageCount() <= 0)
    {
        m_last_error = QStringLiteral("No pages to export");
        return false;
    }

    QFileInfo out_info(path);
    const QDir out_dir(out_info.absolutePath());
    if (!out_dir.exists())
    {
        m_last_error = QStringLiteral("Output directory does not exist");
        return false;
    }

    QTemporaryFile tmp(out_dir.filePath(QStringLiteral("qscan_export_XXXXXX.pdf")));
    tmp.setAutoRemove(false);
    if (!tmp.open())
    {
        m_last_error = QStringLiteral("Failed to create temporary PDF");
        return false;
    }
    const QString tmp_path = tmp.fileName();
    tmp.close();

    QPdfWriter writer(tmp_path);
    const QString creator = QStringLiteral("%1 (%2)").arg(doc.creatorProgram(), doc.creatorAuthor());
    writer.setCreator(creator);
    writer.setTitle(out_info.completeBaseName());

    QPainter painter;
    if (!painter.begin(&writer))
    {
        m_last_error = QStringLiteral("Failed to create PDF");
        return false;
    }

    bool first = true;
    for (int i = 0; i < doc.pageCount(); i++)
    {
        const ScannedPage &page = doc.page(i);
        const QImage image = page.processedImage();
        if (image.isNull())
        {
            m_last_error = QStringLiteral("Page %1 has no image data").arg(i + 1);
            painter.end();
            return false;
        }

        QSizeF page_mm;
        int dpi = 0;
        const bool have_physical_size = resolvePageSizeMm(page, image, page_mm, dpi);

    #if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
        if (have_physical_size)
        {
            const QPageSize ps(page_mm, QPageSize::Millimeter);
            const QPageLayout layout(ps, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
            writer.setPageLayout(layout);
        }
    #endif

        if (!first)
        {
            if (!writer.newPage())
            {
                m_last_error = QStringLiteral("Failed to create new PDF page");
                painter.end();
                return false;
            }
        }
        first = false;

        const QRect page_rect(0, 0, writer.width(), writer.height());
        const QSize img_size = image.size();
        const QSize target_size = img_size.scaled(page_rect.size(), Qt::KeepAspectRatio);
        const int x = (page_rect.width() - target_size.width()) / 2;
        const int y = (page_rect.height() - target_size.height()) / 2;
        const QRect centered(page_rect.x() + x, page_rect.y() + y, target_size.width(), target_size.height());

        painter.fillRect(page_rect, Qt::white);
        painter.drawImage(centered, image);
    }

    painter.end();

#ifdef HAVE_QPDF
    {
        QString qpdf_err;
        const bool ok = injectPdfMetadataWithQpdf(doc, tmp_path, path, out_info.completeBaseName(), qpdf_err);
        if (ok)
        {
            QFile::remove(tmp_path);
            return true;
        }
        //Fallback: keep the Qt-generated PDF if QPDF is unavailable/broken
        if (!QFile::remove(path))
        {
            //Ignore; rename below will fail if overwriting isn't possible
        }
        if (!QFile::rename(tmp_path, path))
        {
            m_last_error = qpdf_err.isEmpty()
                ? QStringLiteral("Failed to write output PDF")
                : QStringLiteral("QPDF metadata injection failed: %1").arg(qpdf_err);
            QFile::remove(tmp_path);
            return false;
        }
        return true;
    }
#else
    if (!QFile::remove(path))
    {
        //Ignore; rename below will fail if overwriting isn't possible
    }
    if (!QFile::rename(tmp_path, path))
    {
        m_last_error = QStringLiteral("Failed to write output PDF");
        QFile::remove(tmp_path);
        return false;
    }
    return true;
#endif
}

bool
DocumentExporter::exportImage(const QImage &image,
                             const QString &path,
                             const QString &format,
                             const QString &scanner_name,
                             const QDateTime &scan_time)
{
    m_last_error.clear();

    if (path.trimmed().isEmpty())
    {
        m_last_error = QStringLiteral("Empty output path");
        return false;
    }

    if (image.isNull())
    {
        m_last_error = QStringLiteral("No image data");
        return false;
    }

    const QByteArray fmt = format.trimmed().toLower().toLatin1();
    if (fmt.isEmpty())
    {
        m_last_error = QStringLiteral("Empty image format");
        return false;
    }

    QImageWriter writer(path, fmt);
    writer.setText(QStringLiteral("Software"), QStringLiteral("QScan"));
    if (!scanner_name.isEmpty())
        writer.setText(QStringLiteral("Scanner"), scanner_name);
    if (scan_time.isValid())
        writer.setText(QStringLiteral("ScanTime"), scan_time.toString(Qt::ISODate));

    if (!writer.write(image))
    {
        m_last_error = writer.errorString();
        return false;
    }

    return true;
}

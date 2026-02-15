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

#include "scan/scanner_source.hpp"
#include "scan/scanner_backend.hpp"
#include "core/classlogger.hpp"

#include <memory>
#include <QMetaObject>

extern QList<ScanDeviceInfo>
enumerateDevices_SANE();

extern std::unique_ptr<ScannerBackend>
createScannerBackend_SANE();

ScannerSource::ScannerSource(const QString &device_name,
                             const QString &device_desc,
                             QObject *parent)
             : ScanSource(parent),
               m_device_name(device_name),
               m_device_desc(device_desc),
               m_is_scanning(false),
               m_is_initialized(false),
               m_last_auto_page_size(false),
               m_preview_active(false),
               m_preview_thread(0)
{
    m_backend = createScannerBackend_SANE();
}

ScannerSource::~ScannerSource()
{
    stopPreview();
    //Backend destructor handles cleanup
    m_backend.reset();
}

QList<ScanDeviceInfo>
ScannerSource::enumerateDevices()
{
    return enumerateDevices_SANE();
}

ScanCapabilities
ScannerSource::capabilities() const
{
    if (!m_backend)
        return ScanCapabilities();

    return m_backend->capabilities();
}

QString
ScannerSource::deviceName() const
{
    return m_device_name;
}

QString
ScannerSource::deviceDescription() const
{
    return m_device_desc;
}

bool
ScannerSource::initialize()
{
    if (m_is_initialized)
        return true;

    if (!m_backend)
        return false;

    if (!m_backend->initialize(m_device_name))
        return false;

    m_is_initialized = true;
    return true;
}

bool
ScannerSource::startScan(const ScanParameters &params)
{
    if (!m_is_initialized || !m_backend)
        return false;

    if (m_preview_active)
    {
        Debug(QS("startScan: preview active, stopping preview"));
        stopPreview();
    }

    m_last_auto_page_size = params.auto_page_size;

    m_is_scanning = true;
    emit scanStarted();

    QString error;
    if (!m_backend->scan(params, [this](const QImage &image, int page_number)
    {
        emit pageScanned(image, page_number);
        return true;
    }, error))
    {
        if (error.isEmpty())
            error = tr("Scan failed");
        emit scanError(error);
        m_is_scanning = false;
        return false;
    }

    emit scanComplete();
    m_is_scanning = false;
    return true;
}

void
ScannerSource::cancelScan()
{
    if (m_backend)
        m_backend->cancelScan();
    m_is_scanning = false;
    m_preview_active = false;
}

bool
ScannerSource::isScanning() const
{
    return m_is_scanning;
}

bool
ScannerSource::isOpen() const
{
    return m_backend && m_backend->isOpen();
}

bool
ScannerSource::startPreview()
{
    if (!m_is_initialized || !m_backend)
        return false;

    if (m_is_scanning)
        return false;

    if (m_preview_active)
        return true;

    ScanParameters preview_params;
    preview_params.resolution = 75;
    preview_params.color_mode = "Gray";
    preview_params.auto_page_size = true;

    m_last_auto_page_size = true;

    m_preview_active = true;

    //Run preview scan on worker thread to avoid UI blocking
    m_preview_thread = QThread::create([this, preview_params]()
    {
        QString error;
        bool ok = false;

        QImage first_page;

        if (m_backend)
            ok = m_backend->scan(preview_params, [&first_page](const QImage &image, int)
            {
                first_page = image;
                return false;
            }, error);

        QMetaObject::invokeMethod(this, [this, ok, first_page, error]()
        {
            if (!m_preview_active)
                return;

            if (!ok || first_page.isNull())
            {
                QString msg = error;
                if (msg.isEmpty())
                    msg = tr("Preview scan failed");
                emit scanError(msg);
            }
            else
            {
                emit previewFrameReady(first_page);
            }

            m_preview_active = false;
        }, Qt::QueuedConnection);
    });

    connect(m_preview_thread, &QThread::finished, this, [this]()
    {
        if (m_preview_thread)
        {
            m_preview_thread->deleteLater();
            m_preview_thread = nullptr;
        }
    });

    m_preview_thread->start();
    return true;
}

void
ScannerSource::stopPreview()
{
    if (!m_preview_active)
        return;

    m_preview_active = false;
    if (m_backend)
        m_backend->cancelScan();

    if (m_preview_thread)
    {
        m_preview_thread->quit();
        m_preview_thread->wait(2000);
        m_preview_thread->deleteLater();
        m_preview_thread = nullptr;
    }
}

bool
ScannerSource::isPreviewActive() const
{
    return m_preview_active;
}

QSizeF
ScannerSource::currentDocumentSize() const
{
    if (!m_backend)
        return QSizeF();
    return m_backend->currentDocumentSize();
}

bool
ScannerSource::documentSizeIsReported() const
{
    return !currentDocumentSize().isEmpty();
}

bool
ScannerSource::documentSizeWasAutoDetected() const
{
    return m_last_auto_page_size && !currentDocumentSize().isEmpty();
}

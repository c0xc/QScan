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

#include <memory>

#include <QMetaObject>

#include "scan/scanner_source.hpp"
#include "scan/scanner_backend.hpp"
#include "scan/document_size.hpp"
#include "core/classlogger.hpp"

extern QList<ScanDeviceInfo>
enumerateDevices_SANE();

extern std::unique_ptr<ScannerBackend>
createScannerBackend_SANE();

extern std::unique_ptr<ScannerBackend>
createScannerBackend_ESCL();

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
               m_preview_thread(nullptr),
               m_preview_cancel_requested(nullptr),
               m_scan_cancel_requested(nullptr),
               m_scan_thread(nullptr)
{
    if (device_name.startsWith("escl:"))
        m_backend = std::shared_ptr<ScannerBackend>(createScannerBackend_ESCL().release());
    else
        m_backend = std::shared_ptr<ScannerBackend>(createScannerBackend_SANE().release());
}

ScannerSource::~ScannerSource()
{
    stopPreview();

    cancelScan();
    if (m_scan_thread)
        m_scan_thread->wait(4000); //4s should be just right for any backend worker shutdown

    //Allow worker thread to finish with its captured backend reference
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

    if (m_is_scanning)
        return false;

    if (m_scan_thread)
        return false;

    if (m_preview_active)
    {
        Debug(QS("startScan: preview active, stopping preview"));
        stopPreview();
    }

    m_last_auto_page_size = params.auto_page_size;

    m_is_scanning = true;
    const auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    m_scan_cancel_requested = cancel_flag;
    emit scanStarted();

    //Run scan on worker thread to avoid UI blocking
    const auto backend = m_backend;
    QPointer<ScannerSource> self(this);

    m_scan_thread = QThread::create([backend, cancel_flag, self, params]()
    {
        QString error;
        bool ok = false;

        if (backend)
        {
            ok = backend->scan(params, [cancel_flag, self](const QImage &image, int page_number, const qscan::ScanPageInfo &page_info)
            {
                if (cancel_flag->load())
                    return false;

                if (self)
                {
                    QImage copy = image;
                    const qscan::ScanPageInfo info_copy = page_info;
                    QMetaObject::invokeMethod(self.data(), [self, copy, page_number, info_copy]()
                    {
                        if (!self)
                            return;
                        Q_EMIT self->pageScanned(copy, page_number, info_copy);
                    }, Qt::QueuedConnection);
                }

                return !cancel_flag->load();
            }, error);
        }

        const bool canceled = cancel_flag->load();

        if (self)
        {
            QMetaObject::invokeMethod(self.data(), [self, ok, canceled, error]()
            {
                if (!self)
                    return;

                if (canceled)
                {
                    Q_EMIT self->scanCanceled();
                    self->m_is_scanning = false;
                    return;
                }

                if (!ok)
                {
                    QString msg = error;
                    if (msg.isEmpty())
                        msg = self->tr("Scan failed");
                    Q_EMIT self->scanError(msg);
                    self->m_is_scanning = false;
                    return;
                }

                Q_EMIT self->scanComplete();
                self->m_is_scanning = false;
            }, Qt::QueuedConnection);
        }
    });

    connect(m_scan_thread, &QThread::finished, m_scan_thread, &QObject::deleteLater);
    connect(m_scan_thread, &QThread::finished, this, [this, cancel_flag]()
    {
        if (m_scan_cancel_requested == cancel_flag)
            m_scan_cancel_requested.reset();
        m_scan_thread = nullptr;
        m_is_scanning = false;
    });

    m_scan_thread->start();
    return true;
}

void
ScannerSource::cancelScan()
{
    if (!m_is_scanning)
        return;

    if (m_scan_cancel_requested)
        m_scan_cancel_requested->store(true);

    emit scanCancelRequested();
    emit scanStatusMessage(tr("Canceling..."));

    if (m_backend)
        m_backend->cancelScan();
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

    Debug(QS("startPreview: requesting preview for device <%s>", CSTR(m_device_name)));

    ScanParameters preview_params;
    //Preview should be fast; prefer 75 DPI gray, but some devices expose a restricted set
    //Pick the closest-safe single-pass values from the capability list (no second scan)
    preview_params.resolution = 75;
    preview_params.color_mode = "Gray";
    preview_params.auto_page_size = true;

    {
        const ScanCapabilities caps = capabilities();

        if (!caps.supported_resolutions.isEmpty())
        {
            int chosen_res = preview_params.resolution;
            if (!caps.supported_resolutions.contains(chosen_res))
            {
                //Prefer the smallest supported resolution for speed
                chosen_res = caps.supported_resolutions.first();
                for (int r : caps.supported_resolutions)
                    chosen_res = (r < chosen_res ? r : chosen_res);
                Debug(QS("Preview resolution %d not supported; falling back to %d", preview_params.resolution, chosen_res));
            }
            preview_params.resolution = chosen_res;
        }

        if (!caps.supported_color_modes.isEmpty() && !caps.supported_color_modes.contains(preview_params.color_mode))
        {
            //Prefer Gray if present, otherwise take first advertised mode
            QString chosen_mode = caps.supported_color_modes.first();
            if (caps.supported_color_modes.contains("Gray"))
                chosen_mode = "Gray";
            Debug(QS("Preview color mode '%s' not supported; falling back to '%s'",
                     CSTR(preview_params.color_mode), CSTR(chosen_mode)));
            preview_params.color_mode = chosen_mode;
        }
    }

    m_last_auto_page_size = true;

    m_preview_active = true;

    const auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    m_preview_cancel_requested = cancel_flag;
    const auto backend = m_backend;
    const QString dev_name = m_device_name;
    QPointer<ScannerSource> self(this);

    //Run preview scan on worker thread to avoid UI blocking
    m_preview_thread = QThread::create([backend, cancel_flag, self, preview_params, dev_name]()
    {
        QString error;
        bool ok = false;

        QImage first_page;

        if (backend)
        {
            Debug(QS("startPreview(worker): entering backend->scan() for <%s>", CSTR(dev_name)));
            ok = backend->scan(preview_params, [&first_page, cancel_flag](const QImage &image, int, const qscan::ScanPageInfo &)
            {
                if (cancel_flag->load())
                    return false;
                first_page = image;
                return false;
            }, error);
            Debug(QS("startPreview(worker): backend->scan() returned ok=%d image_null=%d error='%s' for <%s>",
                     (int)ok, (int)first_page.isNull(), CSTR(error), CSTR(dev_name)));
        }

        if (self)
        {
            QMetaObject::invokeMethod(self.data(), [self, ok, first_page, error]()
            {
                if (!self)
                    return;

                if (!self->m_preview_active)
                    return;

                if (!ok || first_page.isNull())
                {
                    if (!error.isEmpty())
                        Debug(QS("Preview scan failed (internal): %s", CSTR(error)));
                    else
                        Debug(QS("Preview scan failed (internal): <no details>"));

                    const QString msg = error.isEmpty()
                        ? self->tr("Preview scan failed")
                        : self->tr("Preview scan failed: %1").arg(error);
                    self->m_preview_active = false;
                    Q_EMIT self->scanError(msg);
                    return;
                }

                self->m_preview_active = false;
                Q_EMIT self->previewFrameReady(first_page);
            }, Qt::QueuedConnection);
        }
    });

    connect(m_preview_thread, &QThread::finished, m_preview_thread, &QObject::deleteLater);
    connect(m_preview_thread, &QThread::finished, this, [this, cancel_flag]()
    {
        if (m_preview_cancel_requested == cancel_flag)
            m_preview_cancel_requested.reset();
        m_preview_thread = nullptr;
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

    if (m_preview_cancel_requested)
        m_preview_cancel_requested->store(true);

    if (m_backend)
        m_backend->cancelScan();

    if (m_preview_thread)
    {
        m_preview_thread->wait(2000); //wait up to 2s for preview worker shutdown
        //Thread deletes itself on finish (deleteLater connected in startPreview)
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

ScanSource::ReportedDocumentSize
ScannerSource::reportedDocumentSize() const
{
    ReportedDocumentSize out;
    if (!m_backend)
        return out;

    if (!m_backend->documentSizeIsReported())
        return out;

    const QSizeF mm = m_backend->currentDocumentSize();
    if (mm.isEmpty())
        return out;

    out.valid = true;
    out.mm_size = mm;
    out.paper_name = documentSizePaperNameForMm(mm);
    return out;
}

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

#include "scan/mobile_source.hpp"
#include "core/classlogger.hpp"

MobileSource::MobileSource(const QString &device_identifier,
                          const QString &device_description,
                          QObject *parent)
            : ScanSource(parent),
              m_device_identifier(device_identifier),
              m_device_description(device_description),
              m_is_scanning(false),
              m_is_initialized(false),
                            m_live_preview_active(false),
                            m_preview_timer(nullptr),
                            m_backend_url("http://127.0.0.1:8765"),
                            m_session_id()
{
    //Configure capabilities for mobile video input
    m_capabilities.preview_mode = PreviewMode::LiveStream;
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;
    m_capabilities.supports_scan_settings = false;
    m_capabilities.supported_resolutions << 720 << 1080 << 4000;
    m_capabilities.supported_color_modes << "Color";
}

MobileSource::~MobileSource()
{
    if (m_live_preview_active)
    {
        stopPreview();
    }
}

QList<ScanDeviceInfo>
MobileSource::enumerateDevices()
{
    QList<ScanDeviceInfo> devices;
    // For now, add a dummy mobile device as a stub
    // In a real implementation, this would enumerate actual mobile devices
    ScanDeviceInfo info("mobile:default", "Mobile Camera (Stub)", ScanDeviceType::MOBILE);
    devices.append(info);
    return devices;
}

ScanCapabilities
MobileSource::capabilities() const
{
    return m_capabilities;
}

QString
MobileSource::deviceName() const
{
    return m_device_identifier;
}

QString
MobileSource::deviceDescription() const
{
    return m_device_description;
}

bool
MobileSource::initialize()
{
    if (m_is_initialized)
    {
        Debug(QS("<%s> already initialized", CSTR(m_device_identifier)));
        return true;
    }

    Debug(QS("Initializing mobile device <%s>...", CSTR(m_device_identifier)));
    
    // In a real implementation, this would initialize the mobile camera
    m_is_initialized = true;
    return true;
}

bool
MobileSource::startScan(const ScanParameters &params)
{
    if (!m_is_initialized)
        return false;

    m_is_scanning = true;
    emit scanStarted();

    //Capture single frame as scan (high-res photo in real implementation)
    QImage frame(640, 480, QImage::Format_RGB888);
    frame.fill(Qt::lightGray);

    if (!frame.isNull())
    {
        emit pageScanned(frame, 0);
        emit scanComplete();
    }
    else
    {
        emit scanError("Failed to capture image from mobile device");
    }

    m_is_scanning = false;
    return !frame.isNull();
}

void
MobileSource::cancelScan()
{
    m_is_scanning = false;
}

bool
MobileSource::isScanning() const
{
    return m_is_scanning;
}

bool
MobileSource::isOpen() const
{
    return m_is_initialized;
}

bool
MobileSource::startPreview()
{
    if (!m_is_initialized)
    {
        Debug(QS("startPreview: mobile device not initialized"));
        return false;
    }

    if (m_live_preview_active)
        return true;

    Debug(QS("Starting mobile preview stream"));
    
    //Start mobile preview (stub emits frames using timer)
    m_live_preview_active = true;

    if (!m_preview_timer)
    {
        m_preview_timer = new QTimer(this);
        connect(m_preview_timer, SIGNAL(timeout()), this, SLOT(onPreviewTimer()));
    }
    m_preview_timer->start(100);
    
    return true;
}

void
MobileSource::stopPreview()
{
    if (!m_live_preview_active)
        return;

    Debug(QS("Stopping mobile preview stream"));
    if (m_preview_timer)
        m_preview_timer->stop();
    m_live_preview_active = false;
}

bool
MobileSource::isPreviewActive() const
{
    return m_live_preview_active;
}

void
MobileSource::onPreviewTimer()
{
    if (!m_live_preview_active)
        return;

    //Stub frame that will later be replaced by backend stream
    QImage frame(640, 480, QImage::Format_RGB888);
    frame.fill(Qt::darkGray);
    emit previewFrameReady(frame);
}

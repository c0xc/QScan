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
              m_preview_timer(0),
              m_backend_url(),
              m_session_id()
{
    //Mobile/phone input is not implemented yet
    //Keep capabilities conservative so UI/logic doesn't assume support
    m_capabilities.preview_mode = PreviewMode::None;
    m_capabilities.supports_multi_page = false;
    m_capabilities.supports_auto_feed = false;
    m_capabilities.supports_scan_settings = false;
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
    //Not implemented yet
    //Keep enumeration empty so nothing user-visible is exposed prematurely
    return QList<ScanDeviceInfo>();
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
    
    //In a real implementation, this would initialize the mobile camera
    m_is_initialized = true;
    return true;
}

bool
MobileSource::startScan(const ScanParameters &params)
{
    Q_UNUSED(params)

    //Mobile/phone scanning is not available until the backend/API is implemented
    emit scanError(tr("Phone input is not available."));
    return false;
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
    emit scanError(tr("Phone preview is not available."));
    return false;
}

void
MobileSource::stopPreview()
{
    //No-op while unimplemented
}

bool
MobileSource::isPreviewActive() const
{
    return false;
}

void
MobileSource::onPreviewTimer()
{
    //Unused while unimplemented
}

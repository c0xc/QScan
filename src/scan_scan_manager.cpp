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

#include "scan/scan_manager.hpp"
#include "scan/scanner_source.hpp"
#include "scan/webcam_source.hpp"
#include "scan/mobile_source.hpp"
#include "scan/escl_probe.hpp"
#include "scan/escl_compatibility_detector.hpp"
#include "scan/escl_url.hpp"
#include "core/classlogger.hpp"
#include "core/profilesettings.hpp"

#include <QFileInfo>
#include <QSet>
#include <QDateTime>
#include <QXmlStreamReader>

QString
ScanManager::esclSuggestedLabelFromScannerCapabilitiesXml(const QByteArray &caps)
{
    if (caps.isEmpty())
        return QString();

    QXmlStreamReader xml(caps);
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const QString name = xml.name().toString();
        if (name.endsWith(QStringLiteral("MakeAndModel"), Qt::CaseInsensitive))
        {
            const QString text = xml.readElementText().trimmed();
            if (!text.isEmpty())
                return text;
        }
    }

    return QString();
}

ScanManager::ScanManager(QObject *parent)
           : QObject(parent),
             m_device_cache_valid(false),
             m_devices_cached_at_ms(0)
{
}

ScanManager::~ScanManager()
{
}

bool
ScanManager::initialize()
{
    //Reuse recent enumeration cache
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (m_device_cache_valid && m_devices_cached_at_ms > 0)
    {
        const qint64 age_ms = now_ms - m_devices_cached_at_ms;
        if (age_ms >= 0 && age_ms < kDeviceCacheTtlMs)
        {
            Debug(QS("Initialization using cached device list (age_ms=%lld count=%lld)",
                     static_cast<long long>(age_ms),
                     static_cast<long long>(m_devices.count())));
            return true;
        }

        Debug(QS("Device cache expired (age_ms=%lld ttl_ms=%lld), refreshing",
                 static_cast<long long>(age_ms),
                 static_cast<long long>(kDeviceCacheTtlMs)));
    }
    m_device_cache_valid = false;

    //Reset device list
    Debug(QS("Initializing, clearing device list"));
    m_devices.clear();

    //Enumerate scanners
    Debug(QS("Enumerating scanner devices..."));
    enumerateScanners();

    //Enumerate cameras
    Debug(QS("Enumerating camera devices..."));
    enumerateCameras();

    //Update enumeration cache timestamp
    m_device_cache_valid = true;
    m_devices_cached_at_ms = QDateTime::currentMSecsSinceEpoch();

    Debug(QS("Initialization complete, total %lld device(s) found", static_cast<long long>(m_devices.count())));
    return true; //always succeed - even if no devices found
}

QList<ScanDeviceInfo>
ScanManager::availableDevices() const
{
    return m_devices;
}

ScanSource*
ScanManager::createScanSource(const QString &device_name, QObject *parent)
{
    Debug(QS("Creating scan source for device <%s>", CSTR(device_name)));

    //Find device info
    ScanDeviceInfo info;
    bool found = false;
    foreach (const ScanDeviceInfo &dev, m_devices)
    {
        if (dev.name == device_name)
        {
            info = dev;
            found = true;
            break;
        }
    }

    if (!found)
    {
        Debug(QS("Device <%s> NOT FOUND in enumerated devices list", CSTR(device_name)));
        return 0;
    }

    Debug(QS("Found device <%s>, type=<%s>, description=<%s>",
             CSTR(info.name), CSTR(info.typeString()), CSTR(info.description)));

    //Create appropriate source type
    switch (info.type)
    {
        case ScanDeviceType::SCANNER:
            Debug(QS("Creating scanner device for <%s>", CSTR(info.name)));
            return new ScannerSource(info.name, info.description, parent);

        case ScanDeviceType::CAMERA:
            Debug(QS("Creating camera device for <%s>", CSTR(info.name)));
            return new WebcamSource(info.name, info.description, parent);

        case ScanDeviceType::MOBILE:
            Debug(QS("Creating mobile device for <%s>", CSTR(info.name)));
            return new MobileSource(info.name, info.description, parent);

        default:
            Debug(QS("Unknown device type %d for device <%s>", (int)info.type, CSTR(info.name)));
            return 0;
    }
}

bool
ScanManager::testEsclEndpoint(const QString &user_input,
                              QString &normalized_url_out,
                              QString &error_out,
                              QString &suggested_label_out)
{
    //Reset outputs
    normalized_url_out.clear();
    error_out.clear();
    suggested_label_out.clear();

    Debug(QS("eSCL test input=<%s>", CSTR(user_input)));

    //Normalize input to a base /eSCL URL
    const bool has_scheme = user_input.contains("://");
    QString err;

    const QString url_http = esclNormalizeBaseUrlForStorage(user_input, QStringLiteral("http"), &err);
    if (url_http.isEmpty())
    {
        Debug(QS("eSCL normalize failed: %s", CSTR(err)));
        error_out = err;
        return false;
    }

    QString url_https;
    if (!has_scheme)
        url_https = esclNormalizeBaseUrlForStorage(user_input, QStringLiteral("https"), nullptr);

    Debug(QS("eSCL normalized http=<%s>", CSTR(url_http)));
    if (!url_https.isEmpty())
        Debug(QS("eSCL normalized https=<%s>", CSTR(url_https)));

    //Detect SOAP-HT scanners that respond to eSCL info endpoints but cannot pull-scan via eSCL
    //NOTE This must not POST to /eSCL/ScanJobs
    {
        const QString host = QUrl(url_http).host();
        if (!host.isEmpty())
        {
            escl::DetectionResult detail;
            const escl::CompatibilityResult compat = escl::detectCompatibility(host, 2000, &detail);
            if (!escl::isCompatible(compat))
            {
                if (compat == escl::CompatibilityResult::SoapHt)
                {
                    //Reference: M476DW_ESCL_INCOMPATIBILITY_ANALYSIS.md
                    error_out = tr("This device looks like SOAP-HT (HorseThief) and likely cannot scan via AirScan (eSCL). ")
                              + tr("It may still answer eSCL info endpoints, but pull scanning fails (HP M476dw is a known example). ")
                              + tr("Install the official SANE drivers for this scanner.");
                }
                else if (compat == escl::CompatibilityResult::NotEscl)
                {
                    error_out = tr("ScannerCapabilities is not reachable on this host. This does not look like a working AirScan (eSCL) endpoint.");
                }
                else
                {
                    error_out = tr("Compatibility could not be determined.");
                }

                Debug(QS("eSCL compatibility reject host=<%s> result=%d port8289=%d Server=<%s>",
                         CSTR(host), (int)compat,
                         detail.port8289Open ? 1 : 0,
                         CSTR(detail.serverHeader)));
                return false;
            }
        }
    }

    QByteArray caps;
    QUrl effective_base;

    //Probe http endpoint
    Debug(QS("eSCL probe attempt (http)"));
    QString probe_err;
    bool ok = esclProbeScannerCapabilities(QUrl(url_http), 8000, &probe_err, &caps, &effective_base);
    QString chosen = effective_base.isValid() ? effective_base.toString(QUrl::NormalizePathSegments) : url_http;

    //Fallback probe via https when scheme not explicitly provided
    if (!ok && !has_scheme && !url_https.isEmpty())
    {
        Debug(QS("eSCL probe attempt (https)"));
        QString probe_err2;
        QByteArray caps2;
        QUrl effective_base2;
        const bool ok2 = esclProbeScannerCapabilities(QUrl(url_https), 8000, &probe_err2, &caps2, &effective_base2);
        if (ok2)
        {
            ok = true;
            chosen = effective_base2.isValid() ? effective_base2.toString(QUrl::NormalizePathSegments) : url_https;
            caps = caps2;
            probe_err.clear();
        }
        else if (!probe_err2.isEmpty())
        {
            probe_err = probe_err2;
        }
    }

    Debug(QS("eSCL probe result ok=%d url=<%s> err=<%s>", ok ? 1 : 0, CSTR(chosen), CSTR(probe_err)));

    if (ok)
    {
        normalized_url_out = chosen;
        suggested_label_out = esclSuggestedLabelFromScannerCapabilitiesXml(caps);
        return true;
    }

    error_out = probe_err;
    return false;
}

void
ScanManager::enumerateScanners()
{
    //Enumerate scanner backends
    QList<ScanDeviceInfo> sane_devices;
    const bool sane_ok = ScannerSource::enumerateDevices(sane_devices);
    if (!sane_ok)
        Debug(QS("Scanner enumeration failed - backend initialization or SANE query issue"));

    const long long raw_sane_count = static_cast<long long>(sane_devices.count());
    Debug(QS("SANE enumeration raw returned %lld device(s)", raw_sane_count));

    QList<ScanDeviceInfo> scanner_devices;
    QList<ScanDeviceInfo> camera_devices_from_sane;

    int v4l_seen = 0;
    int v4l_valid_node = 0;
    int v4l_filtered_out = 0;

    //Filter SANE v4l pseudo-devices
    foreach (const ScanDeviceInfo &dev, sane_devices)
    {
        //Some systems expose webcams via SANE v4l backends (e.g. v4l:/dev/video0)
        //These are not scanners and should not show up as such in the UI
        if (dev.name.startsWith("v4l:") || dev.name.startsWith("v4l2:"))
        {
            v4l_seen++;
            const int prefix_len = dev.name.startsWith("v4l2:") ? 5 : 4;
            const QString node = dev.name.mid(prefix_len);
            if (!node.isEmpty() && node.startsWith("/") && QFileInfo::exists(node))
            {
                v4l_valid_node++;
                v4l_filtered_out++;
                continue;
            }
        }
        scanner_devices.append(dev);
    }

    Debug(QS("SANE enumeration post-filter: scanners=%lld cameras_from_sane=%lld v4l_seen=%d v4l_valid=%d",
             static_cast<long long>(scanner_devices.count()),
             static_cast<long long>(camera_devices_from_sane.count()),
             v4l_seen,
             v4l_valid_node));

    Debug(QS("SANE v4l handling: filtered out %d v4l/v4l2 device(s) (webcam backend available)", v4l_filtered_out));

    if (raw_sane_count > 0 && scanner_devices.isEmpty() && v4l_seen == raw_sane_count)
        Debug(QS("SANE enumeration OK but all %lld device(s) were v4l/v4l2 and were filtered out -> 0 scanners", raw_sane_count));

    //Append scanner devices
    m_devices.append(scanner_devices);
    m_devices.append(camera_devices_from_sane);

    //Append configured eSCL endpoints
    QSet<QString> seen;
    foreach (const ScanDeviceInfo &dev, m_devices)
        seen.insert(dev.name);

    ProfileSettings *settings = ProfileSettings::useDefaultProfile();
    QVariantList list = settings->variant("network_scanners").toList();

    //Migrate legacy endpoint settings
    bool list_changed = false;
    for (int i = 0; i < list.size(); ++i)
    {
        QVariantMap m = list[i].toMap();
        const QString url = m.value("url").toString().trimmed();
        if (url.isEmpty())
            continue;

        if (!m.contains("ignore_cert_error"))
        {
            m["ignore_cert_error"] = true;
            list[i] = m;
            list_changed = true;
        }
    }
    if (list_changed)
    {
        settings->setVariant("network_scanners", list);
        settings->save();
    }

    //Append endpoints from profile settings
    int added = 0;
    foreach (const QVariant &v, list)
    {
        QVariantMap m = v.toMap();
        const QString url = m.value("url").toString().trimmed();
        const QString label = m.value("label").toString().trimmed();
        if (url.isEmpty())
            continue;

        const QString name = QStringLiteral("escl:%1").arg(url);
        if (seen.contains(name))
            continue;

        const QString desc = label.isEmpty() ? url : label;
        m_devices.append(ScanDeviceInfo(name, desc, ScanDeviceType::SCANNER));
        seen.insert(name);
        added++;
    }

    if (added > 0)
        Debug(QS("Added %d eSCL endpoint(s) from profile settings", added));
}

bool
ScanManager::enumerateCameras()
{
    //Run backend camera enumeration
    QList<ScanDeviceInfo> camera_devices;
    bool success = enumerateCameras(camera_devices);

    //Handle backend failure separately from empty result
    if (!success)
    {
        //Keep existing list unchanged on backend failure
        Debug(QS("Camera enumeration failed - likely backend initialization issue"));
        emit enumerationWarning(QStringLiteral("Camera"),
            tr("Camera backend failed to initialize."));
        return false;
    }

    Debug(QS("Camera enumeration returned %lld device(s)", static_cast<long long>(camera_devices.count())));

    //Append only new camera devices
    QSet<QString> seen;
    foreach (const ScanDeviceInfo &dev, m_devices)
        seen.insert(dev.name);

    foreach (const ScanDeviceInfo &dev, camera_devices)
    {
        if (seen.contains(dev.name))
            continue;
        m_devices.append(dev);
        seen.insert(dev.name);
    }

    return true;
}

bool
ScanManager::enumerateCameras(QList<ScanDeviceInfo> &devices)
{
    //Delegate to webcam backend selector
    return WebcamSource::enumerateDevices(devices);
}

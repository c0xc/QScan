/****************************************************************************
**
** Copyright (C) 2026 Philip Seeger (p@c0xc.net)
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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "core/runtime_selfcheck.hpp"
#include "core/classlogger.hpp"
#include "scan/scanner_source.hpp"
#include "scan/webcam_source.hpp"

/**
 * Runtime self-check implementation and CLI entry point
 * 
 * This is a tool to check if required runtime libraries
 * are unavailable or broken. For example, if Qt's multimedia module
 * is missing in the AppImage and the QtCamera backend is compiled,
 * camera enumeration will fail.
 * 
 * Example output:
 * No QtMultimedia backends found. Only QMediaDevices, QAudioDevice, QSoundEffect, QAudioSink, and QAudioSource are available.
 * qt.qpa.services: Failed to register with host portal QDBusError("org.freedesktop.portal.Error.Failed", "Could not register app ID: App info not found for 'qscan'")
 * QScan Self-Check
 * status=FAIL
 * scanner.enumeration_ok=1
 * scanner.device_count=3
 * camera.enumeration_ok=0
 * camera.device_count=0
 * notes=-
 * warnings=-
 * errors=Camera enumeration failed (QtCamera/GStreamer runtime issue likely)
 * 
 */

namespace qscan
{

class RuntimeSelfCheckInternal
{
public:

    static QString
    joinListForText(const QStringList &items)
    {
        if (items.isEmpty())
            return QStringLiteral("-");

        return items.join(QStringLiteral(" | "));
    }

    static void
    restoreEnvValue(const char *key, const QByteArray &saved, bool was_set)
    {
        if (was_set)
            qputenv(key, saved);
        else
            qunsetenv(key);
    }
};

RuntimeSelfCheckResult::RuntimeSelfCheckResult()
    : scanner_enumeration_ok(false),
      camera_enumeration_ok(false),
      scanner_device_count(0),
      camera_device_count(0)
{
}

bool
RuntimeSelfCheckResult::isFailure() const
{
    return !errors.isEmpty();
}

bool
RuntimeSelfCheckResult::isDegraded() const
{
    return !isFailure() && !warnings.isEmpty();
}

QString
RuntimeSelfCheckResult::statusLabel() const
{
    if (isFailure())
        return QStringLiteral("FAIL");
    if (isDegraded())
        return QStringLiteral("DEGRADED");
    return QStringLiteral("OK");
}

RuntimeSelfCheckResult
runRuntimeSelfCheck(bool full_probe)
{
    RuntimeSelfCheckResult result;

    Debug(QS("Self-check: begin (full_probe=%d)", full_probe ? 1 : 0));

    QByteArray saved_sane_dlopen;
    const bool sane_dlopen_was_set = qEnvironmentVariableIsSet("QSCAN_SANE_TEST_DLOPEN_BACKENDS");
    if (sane_dlopen_was_set)
        saved_sane_dlopen = qgetenv("QSCAN_SANE_TEST_DLOPEN_BACKENDS");

    if (full_probe && !sane_dlopen_was_set)
    {
        qputenv("QSCAN_SANE_TEST_DLOPEN_BACKENDS", QByteArray("hpaio,airscan"));
        result.notes << QStringLiteral("Enabled temporary SANE backend dlopen probe for hpaio,airscan");
    }

    QList<ScanDeviceInfo> scanner_devices;
    result.scanner_enumeration_ok = ScannerSource::enumerateDevices(scanner_devices);
    result.scanner_device_count = scanner_devices.size();

    if (!result.scanner_enumeration_ok)
        result.errors << QStringLiteral("Scanner enumeration failed (SANE runtime path/backend issue likely)");
    else if (result.scanner_device_count == 0)
        result.warnings << QStringLiteral("Scanner enumeration returned 0 devices");

    RuntimeSelfCheckInternal::restoreEnvValue("QSCAN_SANE_TEST_DLOPEN_BACKENDS", saved_sane_dlopen, sane_dlopen_was_set);

    QList<ScanDeviceInfo> camera_devices;
    result.camera_enumeration_ok = WebcamSource::enumerateDevices(camera_devices);
    result.camera_device_count = camera_devices.size();

    if (!result.camera_enumeration_ok)
        result.errors << QStringLiteral("Camera enumeration failed (QtCamera/GStreamer runtime issue likely)");
    else if (result.camera_device_count == 0)
        result.warnings << QStringLiteral("Camera enumeration returned 0 devices");

    Debug(QS("Self-check: done status=<%s> scanners_ok=%d scanners=%d cameras_ok=%d cameras=%d",
        CSTR(result.statusLabel()),
        result.scanner_enumeration_ok ? 1 : 0,
        result.scanner_device_count,
        result.camera_enumeration_ok ? 1 : 0,
        result.camera_device_count));

    return result;
}

QString
runtimeSelfCheckToText(const RuntimeSelfCheckResult &result)
{
    QString out;
    QTextStream stream(&out);

    stream << "QScan Self-Check\n";
    stream << "status=" << result.statusLabel() << "\n";
    stream << "scanner.enumeration_ok=" << (result.scanner_enumeration_ok ? "1" : "0") << "\n";
    stream << "scanner.device_count=" << result.scanner_device_count << "\n";
    stream << "camera.enumeration_ok=" << (result.camera_enumeration_ok ? "1" : "0") << "\n";
    stream << "camera.device_count=" << result.camera_device_count << "\n";
    stream << "notes=" << RuntimeSelfCheckInternal::joinListForText(result.notes) << "\n";
    stream << "warnings=" << RuntimeSelfCheckInternal::joinListForText(result.warnings) << "\n";
    stream << "errors=" << RuntimeSelfCheckInternal::joinListForText(result.errors);

    return out;
}

QByteArray
runtimeSelfCheckToJson(const RuntimeSelfCheckResult &result)
{
    QJsonObject root;
    root.insert(QStringLiteral("status"), result.statusLabel());
    root.insert(QStringLiteral("scannerEnumerationOk"), result.scanner_enumeration_ok);
    root.insert(QStringLiteral("scannerDeviceCount"), result.scanner_device_count);
    root.insert(QStringLiteral("cameraEnumerationOk"), result.camera_enumeration_ok);
    root.insert(QStringLiteral("cameraDeviceCount"), result.camera_device_count);

    QJsonArray notes_json;
    for (int i = 0; i < result.notes.size(); ++i)
        notes_json.append(result.notes.at(i));
    root.insert(QStringLiteral("notes"), notes_json);

    QJsonArray warnings_json;
    for (int i = 0; i < result.warnings.size(); ++i)
        warnings_json.append(result.warnings.at(i));
    root.insert(QStringLiteral("warnings"), warnings_json);

    QJsonArray errors_json;
    for (int i = 0; i < result.errors.size(); ++i)
        errors_json.append(result.errors.at(i));
    root.insert(QStringLiteral("errors"), errors_json);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

int
runtimeSelfCheckExitCode(const RuntimeSelfCheckResult &result)
{
    return result.isFailure() ? 1 : 0;
}

} //namespace qscan

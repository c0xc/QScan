//Small additions for ScannerSource convenience methods
#include "scan/scanner_source.hpp"
#include "scan/scanner_backend.hpp"

QStringList
ScannerSource::supportedInputSources() const
{
    if (!m_backend)
        return QStringList();
    return m_backend->capabilities().supported_input_sources;
}

bool
ScannerSource::supportsDuplex() const
{
    if (!m_backend)
        return false;
    return m_backend->capabilities().supports_duplex;
}

QStringList
ScannerSource::supportedScanSides() const
{
    if (!m_backend)
        return QStringList();
    return m_backend->capabilities().supported_scan_sides;
}

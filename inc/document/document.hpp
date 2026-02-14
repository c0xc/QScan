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

#ifndef DOCUMENT_DOCUMENT_HPP
#define DOCUMENT_DOCUMENT_HPP

#include <QObject>
#include <QList>

#include "document/scannedpage.hpp"

/**
 * Represents a multi-page scanned document.
 * Manages a collection of ScannedPage objects.
 * Note: ScanMode is deprecated - the UI now adapts dynamically based on page count.
 */
class Document : public QObject
{
    Q_OBJECT

public:

    /**
     * @deprecated Scan modes are no longer used. UI adapts dynamically based on page count.
     */
    enum ScanMode
    {
        IMAGE_MODE,    // Deprecated
        DOCUMENT_MODE  // Deprecated
    };
    Q_ENUM(ScanMode)

    explicit
    Document(QObject *parent = nullptr);

    /**
     * Add a page to the document.
     */
    void
    addPage(const ScannedPage &page);

    /**
     * Insert a page at specific position.
     */
    void
    insertPage(int index, const ScannedPage &page);

    /**
     * Remove a page by index.
     */
    void
    removePage(int index);

    /**
     * Move a page from one position to another.
     */
    void
    movePage(int from, int to);

    /**
     * Get page by index.
     */
    ScannedPage&
    page(int index);

    /**
     * Get page by index (const).
     */
    const ScannedPage&
    page(int index) const;

    /**
     * Get number of pages.
     */
    int
    pageCount() const;

    /**
     * Clear all pages.
     */
    void
    clear();

    /**
     * @deprecated Set scan mode. No longer used - kept for compatibility.
     */
    void
    setScanMode(ScanMode mode);

    /**
     * @deprecated Get scan mode. No longer used - kept for compatibility.
     */
    ScanMode
    scanMode() const;

signals:

    /**
     * Emitted when a page is added.
     */
    void
    pageAdded(int index);

    /**
     * Emitted when a page is removed.
     */
    void
    pageRemoved(int index);

    /**
     * Emitted when a page is modified.
     */
    void
    pageModified(int index);

    /**
     * Emitted when pages are reordered.
     */
    void
    pagesReordered();

private:

    QList<ScannedPage> m_pages;
    ScanMode m_mode;

};

#endif // DOCUMENT_DOCUMENT_HPP

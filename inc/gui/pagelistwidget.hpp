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

#ifndef GUI_PAGELISTWIDGET_HPP
#define GUI_PAGELISTWIDGET_HPP

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "document/document.hpp"

/**
 * Widget displaying thumbnails of scanned pages.
 * Allows reordering, deleting, and adding pages.
 * Only visible in DOCUMENT_MODE.
 */
class PageListWidget : public QWidget
{
    Q_OBJECT

public:

    explicit
    PageListWidget(Document *doc, QWidget *parent = 0);

    /**
     * Refresh the list from the document.
     */
    void
    refresh();

    /**
     * Get currently selected page index.
     * Returns -1 if no selection.
     */
    int
    selectedPageIndex() const;

signals:

    /**
     * Emitted when a page is selected.
     */
    void
    pageSelected(int index);

    /**
     * Emitted when user requests to add a new page.
     */
    void
    addPageRequested();

    /**
     * Emitted when user requests to delete a page.
     */
    void
    deletePageRequested(int index);

private slots:

    void
    onItemSelectionChanged();

    void
    onAddPageClicked();

    void
    onDeletePageClicked();

    void
    onDocumentPageAdded(int index);

    void
    onDocumentPageRemoved(int index);

private:

    Document *m_document;
    QListWidget *m_list;
    QPushButton *m_btn_add;
    QPushButton *m_btn_delete;

    void
    setupUi();

    void
    updateThumbnail(int index);

};

#endif // GUI_PAGELISTWIDGET_HPP

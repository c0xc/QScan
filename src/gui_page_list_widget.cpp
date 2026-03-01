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

#include <QAbstractItemModel>
#include <QAction>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalBlocker>
#include <QUrl>

#include "document/pdf_importer.hpp"
#include "gui/pagelistwidget.hpp"

PageListWidget::PageListWidget(Document *doc, QWidget *parent)
              : QWidget(parent),
                m_document(doc),
                m_ignore_document_reorder(false),
                m_ignore_list_rows_moved(false)
{
    setupUi();

    m_list->viewport()->installEventFilter(this);

    //Connect document signals
    if (m_document)
    {
        connect(m_document, SIGNAL(pageAdded(int)),
                this, SLOT(onDocumentPageAdded(int)));
        connect(m_document, SIGNAL(pageRemoved(int)),
                this, SLOT(onDocumentPageRemoved(int)));
        connect(m_document, SIGNAL(pagesReordered()),
                this, SLOT(onDocumentPagesReordered()));
    }
}

bool
PageListWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_list->viewport())
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
    {
        QDropEvent *drop = static_cast<QDropEvent*>(event);
        if (drop->mimeData() && drop->mimeData()->hasUrls())
        {
            drop->acceptProposedAction();
            return true;
        }

        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::Drop)
    {
        QDropEvent *drop = static_cast<QDropEvent*>(event);
        if (!drop->mimeData() || !drop->mimeData()->hasUrls())
            return QWidget::eventFilter(obj, event);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        const QPoint pos = drop->position().toPoint();
#else
        const QPoint pos = drop->pos();
#endif

        const bool handled = tryImportDroppedPdfs(drop->mimeData()->urls(), pos);
        if (handled)
            drop->acceptProposedAction();
        return handled;
    }

    return QWidget::eventFilter(obj, event);
}

bool
PageListWidget::tryImportDroppedPdfs(const QList<QUrl> &urls, const QPoint &pos)
{
    if (!m_document)
        return false;

    QStringList pdf_paths;
    for (const QUrl &url : urls)
    {
        if (!url.isLocalFile())
        {
            QMessageBox::critical(this, tr("PDF Import Failed"), tr("Only local PDF files can be imported."));
            return true;
        }

        const QString path = url.toLocalFile();
        const QFileInfo fi(path);
        if (fi.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
        {
            QMessageBox::critical(this, tr("PDF Import Failed"), tr("Only PDF files can be imported."));
            return true;
        }

        pdf_paths.push_back(path);
    }

    if (pdf_paths.isEmpty())
        return false;

    bool ok = false;
    const QString dpi_text = QInputDialog::getText(this,
        tr("Import PDF"),
        tr("Render DPI:"),
        QLineEdit::Normal,
        QString(),
        &ok);

    if (!ok)
        return true;

    bool dpi_ok = false;
    const int dpi = dpi_text.trimmed().toInt(&dpi_ok);
    if (!dpi_ok || dpi <= 0)
    {
        QMessageBox::critical(this, tr("PDF Import Failed"), tr("DPI must be a positive integer."));
        return true;
    }

    QVector<ScannedPage> imported;
    imported.reserve(16);

    for (const QString &path : pdf_paths)
    {
        QVector<ScannedPage> pages;
        QString error;
        if (!PdfImporter::importPdf(path, dpi, pages, error))
        {
            const QString msg = error.isEmpty() ? tr("Failed to import PDF.") : error;
            QMessageBox::critical(this, tr("PDF Import Failed"), msg);
            return true;
        }

        for (const ScannedPage &p : pages)
            imported.push_back(p);
    }

    if (imported.isEmpty())
    {
        QMessageBox::critical(this, tr("PDF Import Failed"), tr("No pages were imported."));
        return true;
    }

    //Determine insertion index from drop position
    int insert_at = m_document->pageCount();
    if (QListWidgetItem *item = m_list->itemAt(pos))
        insert_at = m_list->row(item);
    insert_at = qBound(0, insert_at, m_document->pageCount());

    //Insert pages
    m_list->setUpdatesEnabled(false);
    for (int i = 0; i < imported.size(); ++i)
        m_document->insertPage(insert_at + i, imported.at(i));
    m_list->setUpdatesEnabled(true);

    refresh();
    selectPageIndex(insert_at + imported.size() - 1);
    return true;
}

void
PageListWidget::refresh()
{
    const int previous = selectedPageIndex();
    m_list->clear();

    if (!m_document)
        return;

    for (int i = 0; i < m_document->pageCount(); ++i)
    {
        updateThumbnail(i);
    }

    if (m_document->pageCount() <= 0)
        return;

    if (previous >= 0 && previous < m_document->pageCount())
        m_list->setCurrentRow(previous);
    else
        m_list->setCurrentRow(m_document->pageCount() - 1);
}

int
PageListWidget::selectedPageIndex() const
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return -1;

    return m_list->row(item);
}

void
PageListWidget::selectPageIndex(int index)
{
    if (!m_document)
        return;

    if (index < 0 || index >= m_document->pageCount())
    {
        m_list->clearSelection();
        return;
    }

    m_list->setCurrentRow(index);
    QListWidgetItem *item = m_list->item(index);
    if (item)
        m_list->scrollToItem(item);
}

void
PageListWidget::onItemSelectionChanged()
{
    int index = selectedPageIndex();
    if (index >= 0)
    {
        m_btn_delete->setEnabled(true);
        emit pageSelected(index);
    }
    else
    {
        m_btn_delete->setEnabled(false);
    }
}

void
PageListWidget::onDeletePageClicked()
{
    int index = selectedPageIndex();
    if (index >= 0)
    {
        emit deletePageRequested(index);
    }
}

void
PageListWidget::onDocumentPageAdded(int index)
{
    if (!m_document)
        return;

    for (int i = index; i < m_document->pageCount(); ++i)
    {
        updateThumbnail(i);
    }
}

void
PageListWidget::onDocumentPageRemoved(int index)
{
    QListWidgetItem *item = m_list->item(index);
    if (item)
    {
        delete item;
    }

    if (!m_document)
        return;

    for (int i = index; i < m_document->pageCount(); ++i)
    {
        updateThumbnail(i);
    }
}

void
PageListWidget::onDocumentPagesReordered()
{
    if (m_ignore_document_reorder)
        return;

    refresh();
}

void
PageListWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    //List widget
    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(120, 170));
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setMovement(QListWidget::Snap);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list);

    connect(m_list, SIGNAL(itemSelectionChanged()),
            this, SLOT(onItemSelectionChanged()));

    connect(m_list->viewport(), SIGNAL(customContextMenuRequested(const QPoint&)),
            this, SLOT(onListContextMenuRequested(const QPoint&)));

    connect(m_list->model(), SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)),
            this, SLOT(onListRowsMoved(QModelIndex,int,int,QModelIndex,int)));

    //Delete button
    m_btn_delete = new QPushButton(tr("Delete Page"));
    m_btn_delete->setEnabled(false); //Disabled until selection
    m_btn_delete->setVisible(false);
    connect(m_btn_delete, SIGNAL(clicked()), this, SLOT(onDeletePageClicked()));
    layout->addWidget(m_btn_delete);
}

void
PageListWidget::onListContextMenuRequested(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        item = m_list->currentItem();
    if (!item)
        return;

    const int index = m_list->row(item);
    if (!m_document || index < 0 || index >= m_document->pageCount())
        return;

    m_list->setCurrentRow(index);

    QMenu menu(this);

    QAction *act_move_up = menu.addAction(tr("Move Up"));
    QAction *act_move_down = menu.addAction(tr("Move Down"));
    menu.addSeparator();
    QAction *act_delete = menu.addAction(tr("Remove Page"));

    act_move_up->setEnabled(index > 0);
    act_move_down->setEnabled(index + 1 < m_document->pageCount());

    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == act_delete)
    {
        emit deletePageRequested(index);
        return;
    }

    int from = index;
    int to = index;
    if (chosen == act_move_up)
        to = index - 1;
    else if (chosen == act_move_down)
        to = index + 1;

    if (to == from)
        return;

    QListWidgetItem *taken = m_list->takeItem(from);
    if (!taken)
        return;

    m_ignore_list_rows_moved = true;
    m_list->insertItem(to, taken);
    m_list->setCurrentRow(to);
    m_ignore_list_rows_moved = false;

    m_ignore_document_reorder = true;
    m_document->movePage(from, to);
    m_ignore_document_reorder = false;

    renumberListItems();
    emit pageSelected(to);
}

void
PageListWidget::onListRowsMoved(const QModelIndex &sourceParent, int sourceStart, int sourceEnd,
const QModelIndex &destinationParent, int destinationRow)
{
    Q_UNUSED(sourceParent);
    Q_UNUSED(destinationParent);

    if (m_ignore_list_rows_moved)
        return;

    if (!m_document)
        return;

    if (sourceStart != sourceEnd)
        return;

    int from = sourceStart;
    int to = destinationRow;
    if (destinationRow > sourceStart)
        to -= 1;

    if (from == to)
        return;

    m_ignore_document_reorder = true;
    m_document->movePage(from, to);
    m_ignore_document_reorder = false;

    renumberListItems();

    const int selected = selectedPageIndex();
    if (selected >= 0)
        emit pageSelected(selected);
}

void
PageListWidget::updateThumbnail(int index)
{
    if (!m_document || index < 0 || index >= m_document->pageCount())
        return;

    const ScannedPage &page = m_document->page(index);
    QPixmap thumbnail = page.thumbnail(QSize(120, 170));

    QListWidgetItem *item = m_list->item(index);
    if (!item)
    {
        item = new QListWidgetItem;
        m_list->insertItem(index, item);
    }

    item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);

    item->setIcon(QIcon(thumbnail));
    item->setText(tr("Page %1").arg(index + 1));
}

void
PageListWidget::renumberListItems()
{
    for (int i = 0; i < m_list->count(); ++i)
    {
        QListWidgetItem *item = m_list->item(i);
        if (item)
            item->setText(tr("Page %1").arg(i + 1));
    }
}

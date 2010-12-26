/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "autoexpandingtreeview.h"

#include <QMouseEvent>
#include <QtDebug>

const int AutoExpandingTreeView::kRowsToShow = 50;

AutoExpandingTreeView::AutoExpandingTreeView(QWidget *parent)
  : QTreeView(parent),
    auto_open_(true),
    expand_on_reset_(true),
    ignore_next_click_(false)
{
  setExpandsOnDoubleClick(false);

  connect(this, SIGNAL(expanded(QModelIndex)), SLOT(ItemExpanded(QModelIndex)));
  connect(this, SIGNAL(clicked(QModelIndex)), SLOT(ItemClicked(QModelIndex)));
  connect(this, SIGNAL(doubleClicked(QModelIndex)), SLOT(ItemDoubleClicked(QModelIndex)));
}

void AutoExpandingTreeView::reset() {
  QTreeView::reset();

  // Expand nodes in the tree until we have about 50 rows visible in the view
  if (auto_open_ && expand_on_reset_) {
    RecursivelyExpand(rootIndex());
  }
}

void AutoExpandingTreeView::RecursivelyExpand(const QModelIndex &index) {
  int rows = model()->rowCount(index);
  RecursivelyExpand(index, &rows);
}

bool AutoExpandingTreeView::RecursivelyExpand(const QModelIndex& index, int* count) {
  if (!CanRecursivelyExpand(index))
    return true;

  if (model()->canFetchMore(index))
    model()->fetchMore(index);

  int children = model()->rowCount(index);
  if (*count + children > kRowsToShow)
    return false;

  expand(index);
  *count += children;

  for (int i=0 ; i<children ; ++i) {
    if (!RecursivelyExpand(model()->index(i, 0, index), count))
      return false;
  }

  return true;
}

void AutoExpandingTreeView::ItemExpanded(const QModelIndex& index) {
  if (model()->rowCount(index) == 1 && auto_open_)
    expand(model()->index(0, 0, index));
}

void AutoExpandingTreeView::ItemClicked(const QModelIndex& index) {
  if (ignore_next_click_) {
    ignore_next_click_ = false;
    return;
  }

  setExpanded(index, !isExpanded(index));
}

void AutoExpandingTreeView::ItemDoubleClicked(const QModelIndex& index) {
  ignore_next_click_ = true;
}

void AutoExpandingTreeView::mousePressEvent(QMouseEvent* event) {
  if (event->modifiers() != Qt::NoModifier) {
    ignore_next_click_ = true;
  }

  QTreeView::mousePressEvent(event);
}

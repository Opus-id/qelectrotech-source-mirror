/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PIDDUPLICATETAGS_H
#define PIDDUPLICATETAGS_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

class DynamicElementTextItem;
class Element;
class QETProject;
class QGraphicsItem;
class QPainter;
class QUndoCommand;

/**
	Tags of P&ID symbols held twice in a project (step 9h): reported, never
	changed unasked. A P&ID tag names one device in the whole project,
	whatever the plant and location around it (decision of the user of
	27/11/2026), so the symbols whose family has a model of tag are compared
	here rather than by the place of DuplicateLabels.

	Two tags made by the model are the same when unit, identifier, train and
	order are (PidTag::sameTagKey()): PT 105 and PIC 105 of one loop are
	not, two PT 105 are; a symbol without an order never is. A label typed
	by hand on such a symbol is the same as any other label of these
	symbols it equals. The folio reports and the symbols excluded from the
	parts list (a device drawn twice on purpose) are left out.

	The whole project is read, the folios not built from the project
	database: none is built (#1766). On the screen, the text of the tag of
	such a symbol has an orange veil behind it, its tool tip says where the
	others are, and the "Correct" submenu gives it the next order.
*/
namespace PidDuplicateTags {

		/// Another symbol which holds the tag of a symbol
	struct Other {
		QUuid element;
		QUuid folio;
		QString label;
	};

		/// The symbols of @p project whose tag another holds too, by uuid,
		/// with those others; read each time, nothing kept
	QHash<QUuid, QList<Other>> find(QETProject *project,
									Qt::CaseSensitivity sensitivity = Qt::CaseSensitive);

		/// find() as groups: the uuids of the symbols which hold one tag,
		/// a symbol and those it clashes with, two at least
	QList<QList<QUuid>> groups(QETProject *project,
							   Qt::CaseSensitivity sensitivity = Qt::CaseSensitive);

		/// @p element is compared here, not by place (DuplicateLabels): its
		/// family has a model of tag in its project
	bool comparedHere(const Element *element);

		/// The others which hold the tag of @p element, as find() gives
		/// them, kept until a tag of its project may have changed
		/// (changed())
	QList<Other> othersOf(const Element *element);

		/// What the tool tip of @p element (or of the text of its tag) says
		/// of its tag held elsewhere: nothing when it is not
	QStringList warnings(const Element *element);

		/// The orange veil behind @p text when it shows the tag of a symbol
		/// which another holds; on the screen only, as the other orange
		/// marks. A symbol which shows no text of its tag has the veil
		/// over its drawing instead (drawOnSymbol()).
	void drawBehind(const DynamicElementTextItem *text, QPainter *painter);
	void drawOnSymbol(const Element *element, QPainter *painter);

		/// A tag may have changed: what is kept is read again, and the
		/// symbols whose veil comes or goes are drawn again, once the
		/// events in progress are over
	void changed();
		/// What changed() does at once (tests)
	void refresh();

		/// "Give the next number" of the "Correct" submenu
	struct Proposal {
		QPointer<Element> element;
		QString order;
		QString text;   ///< what the submenu shows, the name of the undo step
	};
		/// The proposal for @p item, a symbol or the text of its tag, whose
		/// tag another holds and is made by the model; none for anything
		/// else
	std::optional<Proposal> proposal(QGraphicsItem *item);
		/// The undo step of @p proposal: the symbol alone takes the order;
		/// null when it is gone or has it already
	QUndoCommand *apply(const Proposal &proposal);

} // namespace PidDuplicateTags

#endif // PIDDUPLICATETAGS_H

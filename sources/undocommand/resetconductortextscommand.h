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
#ifndef RESETCONDUCTORTEXTSCOMMAND_H
#define RESETCONDUCTORTEXTSCOMMAND_H

#include <QList>
#include <QPointF>
#include <QPointer>
#include <QUndoCommand>

class Conductor;
class QGraphicsItem;

/**
	@brief The ResetConductorTextsCommand class
	Put the text of conductors (a wire number, the designation of a pipe)
	moved or turned by hand back in the place and angle the conductor gives
	it, without touching their path ("Reset conductors" does both, and only
	for a path changed by hand). Undone, each text goes back where it was.
*/
class ResetConductorTextsCommand : public QUndoCommand
{
	public:
		explicit ResetConductorTextsCommand(const QList<Conductor *> &conductors,
											QUndoCommand *parent = nullptr);

		void undo() override;
		void redo() override;
			/// Some text of the conductors given was moved or turned by hand
		bool isValid() const {return !m_texts.isEmpty();}
			/// Some text of @p conductors was moved or turned by hand
		static bool canReset(const QList<Conductor *> &conductors);
			/// The conductors of @p items (a selection): those selected and
			/// those whose text is, once each; a right click on a text
			/// selects the text alone
		static QList<Conductor *> conductorsOf(const QList<QGraphicsItem *> &items);

	private:
		struct Text
		{
			QPointer<Conductor> conductor;
			QPointF pos;
			qreal rotation = 0;
			bool moved = false;
			bool rotated = false;
		};
		QList<Text> m_texts;
};

#endif // RESETCONDUCTORTEXTSCOMMAND_H

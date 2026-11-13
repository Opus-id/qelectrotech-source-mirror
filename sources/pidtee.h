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
#ifndef PIDTEE_H
#define PIDTEE_H

#include "ElementsCollection/elementslocation.h"
#include "qet.h"

#include <QCoreApplication>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QString>

class Conductor;
class Diagram;
class Element;
class QUndoCommand;
class Terminal;

/**
	@brief The PidTee class
	A tee placed by a click on a pipe (P&ID, 5l-1q): with the tool "Draw a
	pipe", or the release on a pipe of a drag from a free nozzle. The tee is
	the symbol of the project (PidCodification::tool_symbols, role "tee"):
	two nozzles facing each other along the pipe and a third aside. The pipe
	is cut through it, both pieces keeping its data; the new pipe leaves by
	the way left free. Pipes only: a wire is never cut here, nor joined.
*/
class PidTee
{
	Q_DECLARE_TR_FUNCTIONS(PidTee)

	public:
			/// Where a tee goes on a pipe for a click
		struct Spot
		{
				/// The pipe cut
			QPointer<Conductor> pipe;
				/// Where the ways of the tee meet, on the folio
			QPointF point;
				/// How far along the pipe, from its terminal1
			qreal along = 0;
				/// Which ways the pipe leaves @p point: toward its terminal1
				/// and toward its terminal2
			Qet::Orientation back = Qet::West, forward = Qet::East;
				/// True at a bend: the tee takes its place
			bool corner = false;
				/// Why no tee may go there, empty when it may
			QString refusal;
			bool isValid() const { return pipe && refusal.isEmpty(); }
		};

			/// Least room between an arm of a tee and a nozzle at the end
			/// of the straight part it lies on, and between two tees: one
			/// step of the grid (2.5 mm), room for the connections drawn
		static constexpr qreal nozzle_room = 10.0;

			/// The symbol of tee of the project of @p diagram, invalid when
			/// not set or not found
		static ElementsLocation symbol(const Diagram *diagram);
			/// True when the project of @p diagram has a symbol of tee set
		static bool isSet(const Diagram *diagram);
			/// The conductor under @p scene_pos (its line, its width and a
			/// little more), nullptr for none
		static Conductor *conductorAt(const Diagram *diagram, const QPointF &scene_pos);
			/// Where a tee of arms @p arm long goes on @p pipe for a click at
			/// @p scene_pos: the point of the line nearest to it, set on the
			/// grid along the line; at a bend when the straight part is too
			/// short for it there
		static Spot spotOn(Conductor *pipe, const QPointF &scene_pos, qreal arm);
			/// spotOn() with the arms of the tee of the project, and the
			/// refusals of a conductor which is not a pipe or no tee
		static Spot spotAt(const Diagram *diagram, const QPointF &scene_pos);
			/// Which way the new pipe leaves a tee at @p spot to reach
			/// @p toward: aside of a straight part (below or to the right
			/// when @p toward is in its axis); at a bend, on along the leg
			/// which goes most toward @p toward
		static Qet::Orientation branchFacing(const Spot &spot, const QPointF &toward);
			/// Why two tees may not go at @p a and @p b of one pipe, empty
			/// when they may
		static QString clash(const Spot &a, const Spot &b, qreal arm);
			/// The length of the arms of the tee of the project, 0 when
			/// it is not a tee
		static qreal armOf(const Diagram *diagram);

			/// A tee of the project, not on the folio yet; nullptr when
			/// there is none or it is not a tee (@p error says why).
			/// Taken into the project unless @p import is false
		static Element *build(Diagram *diagram, QString *error = nullptr, bool import = true);
			/// Turn and move @p tee onto @p spot, a way facing @p out;
			/// @return the nozzle facing @p out
		static Terminal *fit(Element *tee, const Spot &spot, Qet::Orientation out);
			/// Put @p tees (fitted on spots of @p pipe, in the order of
			/// @p spots) on the folio and cut @p pipe through them, as
			/// children of @p parent: the pieces keep its data and its way
		static void cut(Diagram *diagram, Conductor *pipe, const QList<Spot> &spots,
						const QList<Element *> &tees, QUndoCommand *parent);

			/// The release of a drag from @p nozzle at @p scene_pos: on a
			/// pipe, a tee there and a pipe to it, in one undo step.
			/// @return false when there is no pipe there or no tee in the
			/// project (nothing done, nothing said); true otherwise, with
			/// @p refusal set when nothing was done
		static bool branchFrom(Terminal *nozzle, const QPointF &scene_pos, QString *refusal);

			/// What is said when the symbol of tee of the project is not a tee
		static QString notATee();
			/// The three nozzles of a tee: the two facing each other, then
			/// the one aside; false when @p tee is not a tee
		static bool waysOf(const Element *tee, Terminal **run1, Terminal **run2,
						   Terminal **branch);
			/// Where the ways of @p tee meet, on the folio: between its two
			/// facing nozzles (on the axis of the pipe it cuts); a null point
			/// when @p tee is not a tee
		static QPointF middleOf(const Element *tee);
};

#endif // PIDTEE_H

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
#ifndef SIGNAL_MARK_H
#define SIGNAL_MARK_H

#include "signalproperties.h"

#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QString>
#include <QTransform>

/**
	@brief The SignalMark class
	How a signal line of a P&ID says its type of signal: a thin line with a
	mark repeated along it (bars, a cross, a letter the line is the bar of, a
	wave, a circle), or a dashed line, or a plain one.
	The catalogue of the marks is fixed. Which type of signal takes which
	mark is a convention: defaultMark() gives those of ISO 14617 where it has
	one, else those of ANSI/ISA-5.1-2009, and a project may say otherwise
	(PidCodification::signal_marks).

	Nothing here knows a conductor: places() and shapes() work on points, so
	that the rule which places the marks is tested alone.
*/
class SignalMark
{
	public:
		enum Mark {
			None,		///< a plain line
			Dashes,		///< a dashed line, without any mark
			Bar1,		///< one oblique bar
			Bar2,		///< two oblique bars
			Bar3,		///< three oblique bars
			AMark,		///< a chevron, the line being the bar of an "A"
			EMark,		///< a leaning square "C", the line being the bar of an "E"
			LMark,		///< a leaning "L"
			Cross,		///< a cross
			Wave,		///< a wave over the line
			Circle		///< an open circle, the line stopping at it
		};

			/// All of them, in the order the lists offer them
		static QList<Mark> all();
			/// The word the files use for @p mark
		static QString toString(Mark mark);
			/// The mark @p string names; anything else is None. @p ok, if
			/// given, tells whether @p string was a word of the catalogue.
		static Mark fromString(const QString &string, bool *ok = nullptr);
			/// The name of @p mark for the user, translated
		static QString name(Mark mark);
			/// The mark of a signal line of type @p type when nothing else
			/// says: ISO 14617 (7.1.2) for the undefined, pneumatic,
			/// electrical, hydraulic and capillary signals, ANSI/ISA-5.1-2009
			/// (table 5.3.2) for the guided electromagnetic signal and the
			/// data link, which ISO 14617 has not
		static Mark defaultMark(SignalProperties::SignalType type);

			/// The sizes, in drawing units, four to a millimetre. No standard
			/// gives any: they are those of the drawings of ISA-5.1-2009.
			/// The width of a signal line is not here: see
			/// ConductorProperties::lineWidth().
			/// From a mark to the next one along a straight run
		static constexpr qreal pitch = 48;
			/// The height of a bar, a letter or a cross, across the line
		static constexpr qreal height = 10;
			/// From a bar to the next one of the same mark
		static constexpr qreal barGap = 4;
			/// The length of a wave, and how far it goes either side
		static constexpr qreal waveLength = 16;
		static constexpr qreal waveHeight = 4;
		static constexpr qreal circleDiameter = 6;
			/// The dashes of a dashed line and the room between two
		static constexpr qreal dashLength = 6;
		static constexpr qreal dashGap = 4;

			/// @p mark is drawn again and again along the line: all but a
			/// plain and a dashed line
		static bool isRepeated(Mark mark) { return mark != None && mark != Dashes; }
			/// The room @p mark takes along the line; 0 when not repeated
		static qreal length(Mark mark);
			/// The length of line not drawn where @p mark is: an open circle
			/// stands in the line, any other mark over it
		static qreal lineGap(Mark mark) { return mark == Circle ? circleDiameter : 0; }
			/// Make @p pen draw the dashes of a dashed signal line: in drawing
			/// units, whatever its width, with flat ends
		static void applyDashes(QPen &pen);

			/// The strokes of @p mark, in a frame whose origin is on the line,
			/// whose x goes along the line the way it is read and whose y goes
			/// across, downwards for who reads it; empty when not repeated
		static QPainterPath shapes(Mark mark);

			/// Where a mark stands
		struct Place
		{
				/// On the line
			QPointF at;
				/// The way the mark is read, a vector of length one: to the
				/// right on a run drawn either way, upwards on a vertical one
				/// (read from the right of the sheet, as ISO 81714-1 asks),
				/// to the right on an oblique one
			QPointF along;
				/// Which run of straightRuns() it is on, the first being 0
			int run = 0;
		};
			/// @p points without those which change nothing: one of two on
			/// the same place, and one between two others on a straight line
		static QList<QPointF> straightRuns(const QList<QPointF> &points);
			/// Where marks @p mark_length long stand along the line @p points,
			/// @p mark_pitch from one to the next: on each straight run, as
			/// many as fit a mark away at least from its ends, evenly, about
			/// its middle; one in the middle of a run too short for that but
			/// two marks long; none on a shorter one. A line none of whose runs
			/// is long enough has one in the middle of its longest run, so that
			/// its type is always said.
		static QList<Place> places(const QList<QPointF> &points, qreal mark_length,
								   qreal mark_pitch = pitch);
			/// What takes the frame of shapes() to @p place
		static QTransform frame(const Place &place);
			/// The marks @p mark of the line @p points, placed
		static QPainterPath marksPath(Mark mark, const QList<QPointF> &points);
			/// The line @p points without what @p mark hides of it
			/// (lineGap()) at each place it stands
		static QPainterPath brokenLine(Mark mark, const QList<QPointF> &points);
};

#endif

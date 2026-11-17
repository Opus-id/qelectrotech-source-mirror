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
#include "signalmark.h"

#include "conductordashpattern.h"

#include <QLineF>
#include <QtMath>

namespace {
		//Two points nearer than this are one
	constexpr qreal same_place = 0.01;

		//One oblique bar of a mark, at 45 degrees, its middle at @p x
	void addBar(QPainterPath &path, qreal x)
	{
		const qreal h = SignalMark::height / 2.0;
		path.moveTo(x - h, h);
		path.lineTo(x + h, -h);
	}

		//A point of a letter @p x from its axis and @p y from the line,
		//leaning to the right as an italic one
	QPointF leaning(qreal x, qreal y)
	{
		return QPointF(x - 0.3 * y, y);
	}
}

/**
	@brief SignalMark::all
	See the declaration
*/
QList<SignalMark::Mark> SignalMark::all()
{
	return {None, Dashes, Bar1, Bar2, Bar3, AMark, EMark, LMark, Cross, Wave, Circle};
}

/**
	@brief SignalMark::toString
	@param mark
	@return the word the files use for @p mark: part of the file format, it
	never changes
*/
QString SignalMark::toString(Mark mark)
{
	switch (mark) {
		case None:   return QStringLiteral("none");
		case Dashes: return QStringLiteral("dashes");
		case Bar1:   return QStringLiteral("bar1");
		case Bar2:   return QStringLiteral("bar2");
		case Bar3:   return QStringLiteral("bar3");
		case AMark:  return QStringLiteral("a_mark");
		case EMark:  return QStringLiteral("e_mark");
		case LMark:  return QStringLiteral("l_mark");
		case Cross:  return QStringLiteral("cross");
		case Wave:   return QStringLiteral("wave");
		case Circle: return QStringLiteral("circle");
	}
	return QStringLiteral("none");
}

/**
	@brief SignalMark::fromString
	See the declaration
*/
SignalMark::Mark SignalMark::fromString(const QString &string, bool *ok)
{
	for (Mark mark : all()) {
		if (string == toString(mark)) {
			if (ok) *ok = true;
			return mark;
		}
	}
	if (ok) *ok = false;
	return None;
}

/**
	@brief SignalMark::defaultMark
	See the declaration
*/
SignalMark::Mark SignalMark::defaultMark(SignalProperties::SignalType type)
{
	switch (type) {
		case SignalProperties::Undefined:             return Bar1;
		case SignalProperties::Pneumatic:             return AMark;
		case SignalProperties::Electrical:            return EMark;
		case SignalProperties::Hydraulic:             return LMark;
		case SignalProperties::Capillary:             return Cross;
		case SignalProperties::GuidedElectromagnetic: return Wave;
		case SignalProperties::DataLink:              return Circle;
	}
	return Bar1;
}

/**
	@brief SignalMark::length
	See the declaration
*/
qreal SignalMark::length(Mark mark)
{
	return isRepeated(mark) ? shapes(mark).boundingRect().width() : 0;
}

/**
	@brief SignalMark::applyDashes
	See the declaration. Drawn as a wire with a pattern of its own is
	(ConductorDashPattern): the lengths are in drawing units, not in widths
	of the pen as those of Qt::DashLine are.
*/
void SignalMark::applyDashes(QPen &pen)
{
	ConductorDashPattern dashes;
	dashes.enabled = true;
	dashes.dash = dashLength;
	dashes.gap = dashGap;
	dashes.apply(pen, Qt::DashLine);
}

/**
	@brief SignalMark::shapes
	See the declaration
*/
QPainterPath SignalMark::shapes(Mark mark)
{
	QPainterPath path;
	const qreal h = height / 2.0;
	switch (mark) {
		case None:
		case Dashes:
			break;
		case Bar1:
			addBar(path, 0);
			break;
		case Bar2:
			addBar(path, -barGap / 2.0);
			addBar(path, barGap / 2.0);
			break;
		case Bar3:
			addBar(path, -barGap);
			addBar(path, 0);
			addBar(path, barGap);
			break;
		case AMark:
				//Its top above the line, which is its bar
			path.moveTo(-4, h - 1);
			path.lineTo(0, -h - 1);
			path.lineTo(4, h - 1);
			break;
		case EMark:
				//The top, the back and the foot of an "E"; the line is its
				//middle bar
			path.moveTo(leaning(3, -h));
			path.lineTo(leaning(-3, -h));
			path.lineTo(leaning(-3, h));
			path.lineTo(leaning(3, h));
			break;
		case LMark:
			path.moveTo(leaning(-1.5, -h));
			path.lineTo(leaning(-1.5, h));
			path.lineTo(leaning(4.5, h));
			break;
		case Cross:
			path.moveTo(-h, -h);
			path.lineTo(h, h);
			path.moveTo(-h, h);
			path.lineTo(h, -h);
			break;
		case Wave:
				//One period: a control point twice as far as the wave goes
			path.moveTo(-waveLength / 2.0, 0);
			path.quadTo(-waveLength / 4.0, -2 * waveHeight, 0, 0);
			path.quadTo(waveLength / 4.0, 2 * waveHeight, waveLength / 2.0, 0);
			break;
		case Circle:
			path.addEllipse(QPointF(0, 0), circleDiameter / 2.0, circleDiameter / 2.0);
			break;
	}
	return path;
}

/**
	@brief SignalMark::straightRuns
	See the declaration
*/
QList<QPointF> SignalMark::straightRuns(const QList<QPointF> &points)
{
	QList<QPointF> runs;
	for (const QPointF &point : points)
	{
		if (!runs.isEmpty() && QLineF(runs.constLast(), point).length() < same_place) {
			continue;
		}
		if (runs.size() >= 2)
		{
				//Going on the same way: the point before is no corner
			const QPointF before = runs.constLast() - runs.at(runs.size() - 2);
			const QPointF after = point - runs.constLast();
			const qreal cross = before.x() * after.y() - before.y() * after.x();
			const qreal dot = before.x() * after.x() + before.y() * after.y();
			if (qAbs(cross) < same_place * (QLineF(QPointF(), before).length()
											 + QLineF(QPointF(), after).length())
				&& dot > 0) {
				runs.removeLast();
			}
		}
		runs.append(point);
	}
	return runs;
}

/**
	@brief SignalMark::places
	See the declaration
*/
QList<SignalMark::Place> SignalMark::places(const QList<QPointF> &points, qreal mark_length,
											 qreal mark_pitch)
{
	QList<Place> found;
	const QList<QPointF> runs = straightRuns(points);
	if (runs.size() < 2 || mark_length <= 0 || mark_pitch <= 0) {
		return found;
	}
		//Where a mark is read from, on the run from @p from to @p to
	auto reading = [](const QPointF &from, const QPointF &to) {
		const QLineF run(from, to);
		QPointF along((to.x() - from.x()) / run.length(), (to.y() - from.y()) / run.length());
		const bool vertical = qAbs(along.x()) < same_place;
		if (vertical ? along.y() > 0 : along.x() < 0) {
			along = -along;
		}
		return along;
	};

	int longest = 0;
	qreal longest_length = 0;
	for (int i = 0 ; i + 1 < runs.size() ; ++i)
	{
		const QPointF from = runs.at(i);
		const QPointF to = runs.at(i + 1);
		const qreal length = QLineF(from, to).length();
		if (length > longest_length) {
			longest = i;
			longest_length = length;
		}
		if (length < 2 * mark_length) {
			continue;
		}
			//A mark away from each end: the middle of the first one is a
			//mark and a half from it
		const int count = qMax(1, qFloor((length - 3 * mark_length) / mark_pitch) + 1);
		const QPointF step = (to - from) / length;
		for (int n = 0 ; n < count ; ++n) {
			const qreal at = length / 2.0 + (n - (count - 1) / 2.0) * mark_pitch;
			found.append({from + at * step, reading(from, to), i});
		}
	}
	if (found.isEmpty()) {
		const QPointF from = runs.at(longest);
		const QPointF to = runs.at(longest + 1);
		found.append({(from + to) / 2.0, reading(from, to), longest});
	}
	return found;
}

/**
	@brief SignalMark::frame
	See the declaration
*/
QTransform SignalMark::frame(const Place &place)
{
	const QPointF along = place.along;
		//Across: downwards for who reads the mark
	return QTransform(along.x(), along.y(), -along.y(), along.x(), place.at.x(), place.at.y());
}

/**
	@brief SignalMark::marksPath
	See the declaration
*/
QPainterPath SignalMark::marksPath(Mark mark, const QList<QPointF> &points)
{
	QPainterPath marks;
	if (!isRepeated(mark)) {
		return marks;
	}
	const QPainterPath one = shapes(mark);
	for (const Place &place : places(points, length(mark))) {
		marks.addPath(frame(place).map(one));
	}
	return marks;
}

/**
	@brief SignalMark::brokenLine
	See the declaration
*/
QPainterPath SignalMark::brokenLine(Mark mark, const QList<QPointF> &points)
{
	QPainterPath line;
	const QList<QPointF> runs = straightRuns(points);
	if (runs.size() < 2) {
		return line;
	}
	const qreal half_gap = lineGap(mark) / 2.0;
	const QList<Place> marks = half_gap > 0 ? places(points, length(mark)) : QList<Place>();
	line.moveTo(runs.constFirst());
	for (int i = 0 ; i + 1 < runs.size() ; ++i)
	{
		const QPointF from = runs.at(i);
		const QPointF to = runs.at(i + 1);
		const QPointF step = (to - from) / QLineF(from, to).length();
			//The places of a run are in order, from its start
		for (const Place &place : marks) {
			if (place.run != i) {
				continue;
			}
			line.lineTo(place.at - half_gap * step);
			line.moveTo(place.at + half_gap * step);
		}
		line.lineTo(to);
	}
	return line;
}

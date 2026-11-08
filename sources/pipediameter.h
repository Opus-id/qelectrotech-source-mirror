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
#ifndef PIPE_DIAMETER_H
#define PIPE_DIAMETER_H

#include <QChar>
#include <QDomElement>
#include <QLocale>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QString>

/**
	@brief The PipeDiameter class
	The size a pipe or a nozzle is named by: a nominal diameter (DN 100), a
	whole number without unit, as for a metal pipe or a flange; or an outside
	diameter in millimetres, to the tenth (Ø 110, Ø 88,9), as for a plastic
	pipe or a socket; or a pipe thread in inches (G 1"1/2), for a threaded
	nozzle only. The same number may be either: the kind is always kept
	with it, and a DN is never read as an outside diameter, nor the reverse.
	None when not set.

	It is kept in tenths of a millimetre, so that two sizes compare exactly;
	a thread in eighths of an inch. A file always writes it with a point
	(88.9), a thread as "1 1/2"; the screen follows the setting of the
	project (PidCodification::locale(), 88,9 with a comma); what is typed
	may have either.

	A thread is joined to a DN by a fixed table (threadTable()), the usual
	one of EN 10255: 1/2" is DN 15, 1"1/2 DN 40.
*/
class PipeDiameter
{
	public:
		enum Kind { None, Nominal, Outside, Thread };

		PipeDiameter() = default;
			/// A DN of @p value; none when @p value is not positive
		static PipeDiameter nominal(int value) { return make(Nominal, value * 10); }
			/// An outside diameter of @p millimetres, to the tenth; none when
			/// it is not positive
		static PipeDiameter outside(double millimetres) {
			return make(Outside, qRound(millimetres * 10));
		}
			/// A pipe thread of @p eighths of an inch (12 for 1"1/2); none
			/// when it is not positive
		static PipeDiameter thread(int eighths) { return make(Thread, eighths); }
			/// A diameter of the kind @p kind of @p tenths of a millimetre (of
			/// eighths of an inch for a thread); a DN must be whole
		static PipeDiameter fromTenths(Kind kind, int tenths) {
			return (kind == Nominal && tenths % 10) ? PipeDiameter() : make(kind, tenths);
		}

		Kind kind() const { return m_kind; }
			/// In millimetres, or the DN; in inches for a thread
		double value() const { return m_kind == Thread ? m_tenths / 8.0 : m_tenths / 10.0; }
			/// In tenths of a millimetre; in eighths of an inch for a thread
		int tenths() const { return m_tenths; }
		bool isSet() const { return m_kind != None; }
		bool isNominal() const { return m_kind == Nominal; }
		bool isOutside() const { return m_kind == Outside; }
		bool isThread() const { return m_kind == Thread; }

			/// The sign of an outside diameter
		static QChar outsideSign() { return QChar(0x00D8); }
			/// The sign of a pipe thread (ISO 228)
		static QChar threadSign() { return QLatin1Char('G'); }

			/// The pipe threads and their DN, in eighths of an inch: the
			/// usual table of EN 10255, fixed, from 1/8" to 6"
		static const QList<QPair<int, int>> &threadTable()
		{
			static const QList<QPair<int, int>> table = {
				{1, 6}, {2, 8}, {3, 10}, {4, 15}, {6, 20}, {8, 25}, {10, 32},
				{12, 40}, {16, 50}, {20, 65}, {24, 80}, {32, 100}, {40, 125}, {48, 150}};
			return table;
		}
			/// The pipe threads of threadTable(), from the smallest
		static QList<PipeDiameter> threadSeries()
		{
			QList<PipeDiameter> series;
			for (const auto &row : threadTable()) {
				series << thread(row.first);
			}
			return series;
		}
			/// The DN the table joins to this thread; none for another kind,
			/// or a thread the table has not
		PipeDiameter threadNominal() const
		{
			if (m_kind == Thread) {
				for (const auto &row : threadTable()) {
					if (row.first == m_tenths) return nominal(row.second);
				}
			}
			return PipeDiameter();
		}
			/// The thread the table joins to the DN @p dn; none for another
			/// kind, or a DN the table has not
		static PipeDiameter threadOfNominal(const PipeDiameter &dn)
		{
			if (dn.isNominal()) {
				for (const auto &row : threadTable()) {
					if (row.second * 10 == dn.tenths()) return thread(row.first);
				}
			}
			return PipeDiameter();
		}

			/// @p eighths of an inch as the screen writes them: 1/2", 1",
			/// 1"1/2
		static QString inches(int eighths)
		{
			const QString whole = eighths >= 8 ? QString::number(eighths / 8) + QLatin1Char('"')
											   : QString();
			const QString fraction = eighthsFraction(eighths % 8);
			return whole.isEmpty() ? fraction + QLatin1Char('"') : whole + fraction;
		}
			/// @p eighths of an inch as written in a file: 1/2, 1, 1 1/2
		static QString fileInches(int eighths)
		{
			const QString whole = eighths >= 8 ? QString::number(eighths / 8) : QString();
			const QString fraction = eighthsFraction(eighths % 8);
			if (whole.isEmpty()) return fraction;
			return fraction.isEmpty() ? whole : whole + QLatin1Char(' ') + fraction;
		}
			/// The inches written in @p text, in eighths: 2, 3/4, 1 1/2, as
			/// a file writes them, or 1"1/2, 3/4", 1-1/2, as they are typed;
			/// a fraction of 2, 4 or 8 only; else 0
		static int eighthsOf(const QString &text)
		{
			QString written = text;
			written.replace(QLatin1Char('"'), QLatin1Char(' '));
			written.replace(QLatin1Char('-'), QLatin1Char(' '));
			static const QRegularExpression inches_re(QStringLiteral(
				"^\\s*(?:([1-9][0-9]?)(?:\\s+([1-7])/([248]))?|([1-7])/([248]))\\s*$"));
			const QRegularExpressionMatch match = inches_re.match(written);
			if (!match.hasMatch()) {
				return 0;
			}
			const bool alone = !match.captured(4).isEmpty();
			const int whole = alone ? 0 : match.captured(1).toInt();
			const QString numerator = alone ? match.captured(4) : match.captured(2);
			const QString denominator = alone ? match.captured(5) : match.captured(3);
			int eighths = whole * 8;
			if (!numerator.isEmpty()) {
				const int n = numerator.toInt();
				const int d = denominator.toInt();
				if (n >= d) {
					return 0;
				}
				eighths += n * (8 / d);
			}
			return eighths;
		}

			/// @p tenths as a number of @p locale: 110, 88,9 (88.9 in
			/// English); no tenth written when there is none
		static QString number(int tenths, const QLocale &locale = QLocale())
		{
				//1100, not 1 100
			QLocale plain = locale;
			plain.setNumberOptions(QLocale::OmitGroupSeparator);
			if (tenths % 10 == 0) {
				return plain.toString(tenths / 10);
			}
			return plain.toString(tenths / 10.0, 'f', 1);
		}
			/// @p tenths as written in a file: 110, 88.9
		static QString fileNumber(int tenths)
		{
			return number(tenths, QLocale::c());
		}

			/// "DN 100", "Ø 88,9", "G 1"1/2", or nothing: how the lists offer
			/// it, and how a nozzle shows it under its name
		QString text(const QLocale &locale = QLocale()) const
		{
			switch (m_kind) {
				case Nominal: return QStringLiteral("DN ") + number(m_tenths, locale);
				case Outside: return QString(outsideSign()) + QLatin1Char(' ') + number(m_tenths, locale);
				case Thread:  return QString(threadSign()) + QLatin1Char(' ') + inches(m_tenths);
				case None:    break;
			}
			return QString();
		}

			/**
				The diameter written in @p text: "DN 100" or "DN100" is a DN,
				"Ø 110", "ø88,9" or "D110" an outside diameter, "G 1"1/2",
				"G3/4" or a fraction of an inch (1/2") a pipe thread, and a
				number alone is of the kind @p bare. A tenth may follow a comma
				or a point, for an outside diameter only. Empty text is none,
				and so is anything else, @p ok being then set to false.
			*/
		static PipeDiameter fromText(const QString &text, Kind bare = Nominal, bool *ok = nullptr)
		{
			QString rest = text.trimmed();
			if (ok) *ok = true;
			if (rest.isEmpty()) {
				return PipeDiameter();
			}
			Kind kind = bare == None ? Nominal : bare;
			if (rest.startsWith(QLatin1String("DN"), Qt::CaseInsensitive)) {
				kind = Nominal;
				rest = rest.mid(2);
			} else if (rest.at(0) == outsideSign() || rest.at(0) == QChar(0x00F8)
					   || rest.at(0) == QLatin1Char('D') || rest.at(0) == QLatin1Char('d')) {
				kind = Outside;
				rest = rest.mid(1);
			} else if (rest.at(0) == threadSign() || rest.at(0) == QLatin1Char('g')) {
				kind = Thread;
				rest = rest.mid(1);
			} else if (rest.contains(QLatin1Char('/')) || rest.contains(QLatin1Char('"'))) {
				kind = Thread;
			}
			const PipeDiameter diameter = kind == Thread ? thread(eighthsOf(rest))
														 : fromTenths(kind, tenthsOf(rest));
			if (!diameter.isSet() && ok) {
				*ok = false;
			}
			return diameter;
		}

			/// The diameter of the attributes @p nominal_name (a DN),
			/// @p outside_name and @p thread_name (if given) of @p e: the DN
			/// when several are there, as an older version only knew the DN;
			/// none when none can be read
		static PipeDiameter fromXml(const QDomElement &e, const QString &nominal_name,
									const QString &outside_name,
									const QString &thread_name = QString())
		{
			const PipeDiameter dn = fromTenths(Nominal, tenthsOf(e.attribute(nominal_name)));
			if (dn.isSet()) {
				return dn;
			}
			const PipeDiameter od = fromTenths(Outside, tenthsOf(e.attribute(outside_name)));
			if (od.isSet() || thread_name.isEmpty()) {
				return od;
			}
			return thread(eighthsOf(e.attribute(thread_name)));
		}
			/// The name of the attribute the diameter is written as: one of
			/// those given, none when it is not set, or a thread without
			/// @p thread_name
		QString attributeName(const QString &nominal_name, const QString &outside_name,
							  const QString &thread_name = QString()) const
		{
			switch (m_kind) {
				case Nominal: return nominal_name;
				case Outside: return outside_name;
				case Thread:  return thread_name;
				case None:    break;
			}
			return QString();
		}
			/// The value of the attribute the diameter is written as: 110,
			/// 88.9, "1 1/2"
		QString fileText() const
		{
			return m_kind == Thread ? fileInches(m_tenths) : fileNumber(m_tenths);
		}
			/// Writes the diameter as the attribute @p nominal_name,
			/// @p outside_name or @p thread_name of @p e; nothing when it is
			/// not set, nor for a thread without @p thread_name
		void toXml(QDomElement &e, const QString &nominal_name, const QString &outside_name,
				   const QString &thread_name = QString()) const
		{
			const QString name = attributeName(nominal_name, outside_name, thread_name);
			if (isSet() && !name.isEmpty()) {
				e.setAttribute(name, fileText());
			}
		}

			/// The positive number to the tenth written in @p text, with a
			/// comma or a point, in tenths of a millimetre; else 0
		static int tenthsOf(const QString &text)
		{
			QString written = text.trimmed();
			written.replace(QLatin1Char(','), QLatin1Char('.'));
			if (written.isEmpty() || written.startsWith(QLatin1Char('.'))
				|| written.startsWith(QLatin1Char('+')) || written.startsWith(QLatin1Char('-'))) {
				return 0;
			}
			const int point = written.indexOf(QLatin1Char('.'));
			if (point >= 0 && written.size() - point > 2) {
				return 0;
			}
			bool ok = false;
			const double value = written.toDouble(&ok);
			const int tenths = ok ? qRound(value * 10) : 0;
			return (tenths > 0 && tenths <= 999990) ? tenths : 0;
		}

		bool operator==(const PipeDiameter &other) const {
			return m_kind == other.m_kind && m_tenths == other.m_tenths;
		}
		bool operator!=(const PipeDiameter &other) const { return !(*this == other); }

	private:
			/// @p eighths (less than 8) of an inch as a fraction: 1/8, 1/4,
			/// 3/8, 1/2; nothing for 0
		static QString eighthsFraction(int eighths)
		{
			if (eighths <= 0) {
				return QString();
			}
			int numerator = eighths;
			int denominator = 8;
			while (numerator % 2 == 0) {
				numerator /= 2;
				denominator /= 2;
			}
			return QString::number(numerator) + QLatin1Char('/') + QString::number(denominator);
		}

		static PipeDiameter make(Kind kind, int tenths)
		{
			PipeDiameter diameter;
			if (kind != None && tenths > 0 && tenths <= 999990) {
				diameter.m_kind = kind;
				diameter.m_tenths = tenths;
			}
			return diameter;
		}

		Kind m_kind = None;
		int m_tenths = 0;
};

	/// The diameters a placed element gives its nozzles, by name of nozzle
using NozzleDiameters = QMap<QString, PipeDiameter>;

#endif

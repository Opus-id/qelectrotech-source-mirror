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
#include <QMap>
#include <QString>

/**
	@brief The PipeDiameter class
	The size a pipe or a nozzle is named by: a nominal diameter (DN 100), a
	whole number without unit, as for a metal pipe or a flange; or an outside
	diameter in millimetres, to the tenth (Ø 110, Ø 88,9), as for a plastic
	pipe or a socket. The same number may be either: the kind is always kept
	with it, and a DN is never read as an outside diameter, nor the reverse.
	None when not set.

	It is kept in tenths of a millimetre, so that two sizes compare exactly.
	A file always writes it with a point (88.9); the screen follows the
	setting of the project (PidCodification::locale(), 88,9 with a comma);
	what is typed may have either.
*/
class PipeDiameter
{
	public:
		enum Kind { None, Nominal, Outside };

		PipeDiameter() = default;
			/// A DN of @p value; none when @p value is not positive
		static PipeDiameter nominal(int value) { return make(Nominal, value * 10); }
			/// An outside diameter of @p millimetres, to the tenth; none when
			/// it is not positive
		static PipeDiameter outside(double millimetres) {
			return make(Outside, qRound(millimetres * 10));
		}
			/// A diameter of the kind @p kind of @p tenths of a millimetre; a
			/// DN must be whole
		static PipeDiameter fromTenths(Kind kind, int tenths) {
			return (kind == Nominal && tenths % 10) ? PipeDiameter() : make(kind, tenths);
		}

		Kind kind() const { return m_kind; }
			/// In millimetres, or the DN
		double value() const { return m_tenths / 10.0; }
		int tenths() const { return m_tenths; }
		bool isSet() const { return m_kind != None; }
		bool isNominal() const { return m_kind == Nominal; }
		bool isOutside() const { return m_kind == Outside; }

			/// The sign of an outside diameter
		static QChar outsideSign() { return QChar(0x00D8); }

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

			/// "DN 100", "Ø 88,9", or nothing: how the lists offer it
		QString text(const QLocale &locale = QLocale()) const
		{
			switch (m_kind) {
				case Nominal: return QStringLiteral("DN ") + number(m_tenths, locale);
				case Outside: return QString(outsideSign()) + QLatin1Char(' ') + number(m_tenths, locale);
				case None:    break;
			}
			return QString();
		}
			/// "100", "Ø88,9", or nothing: how a nozzle shows it under its
			/// name, a DN as before
		QString label(const QLocale &locale = QLocale()) const
		{
			switch (m_kind) {
				case Nominal: return number(m_tenths, locale);
				case Outside: return QString(outsideSign()) + number(m_tenths, locale);
				case None:    break;
			}
			return QString();
		}

			/**
				The diameter written in @p text: "DN 100" or "DN100" is a DN,
				"Ø 110", "ø88,9" or "D110" an outside diameter, and a number
				alone is of the kind @p bare. A tenth may follow a comma or a
				point, for an outside diameter only. Empty text is none, and so
				is anything else, @p ok being then set to false.
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
			}
			const PipeDiameter diameter = fromTenths(kind, tenthsOf(rest));
			if (!diameter.isSet() && ok) {
				*ok = false;
			}
			return diameter;
		}

			/// The diameter of the attributes @p nominal_name (a DN) and
			/// @p outside_name of @p e: the DN when both are there, as an older
			/// version only knew the DN; none when neither can be read
		static PipeDiameter fromXml(const QDomElement &e, const QString &nominal_name,
									const QString &outside_name)
		{
			const PipeDiameter dn = fromTenths(Nominal, tenthsOf(e.attribute(nominal_name)));
			return dn.isSet() ? dn : fromTenths(Outside, tenthsOf(e.attribute(outside_name)));
		}
			/// The name of the attribute the diameter is written as: one of
			/// those given, none when it is not set
		QString attributeName(const QString &nominal_name, const QString &outside_name) const
		{
			return m_kind == Nominal ? nominal_name
								  : m_kind == Outside ? outside_name : QString();
		}
			/// Writes the diameter as the attribute @p nominal_name or
			/// @p outside_name of @p e; nothing when it is not set
		void toXml(QDomElement &e, const QString &nominal_name, const QString &outside_name) const
		{
			if (isSet()) {
				e.setAttribute(attributeName(nominal_name, outside_name), fileNumber(m_tenths));
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

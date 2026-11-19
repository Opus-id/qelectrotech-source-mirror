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
#ifndef TERMINALDATA_H
#define TERMINALDATA_H

#include <qet.h>
#include "propertiesinterface.h"
#include "../pipeconnection.h"
#include "../pipediameter.h"

#include <QColor>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QStringList>
#include <QUuid>

class QGraphicsObject;

/**
	@brief The TerminalData class
	Data of the terminal.
	Stored in extra class so it can be used by PartTerminal
	and Terminal without defining everything again.
	@note tis class needs to be work on
*/
class TerminalData : public PropertiesInterface
{
	Q_GADGET

	public:
		enum Type {
			Generic,
			Inner,
			Outer,
			No,     ///< Normally Open terminal (for SW contacts)
			Nc,     ///< Normally Closed terminal (for SW contacts)
			Common  ///< Common terminal (for SW contacts)
		};
		Q_ENUM(Type)

			/// The medium a terminal connects: what flows through what is
			/// connected to it. Electrical, the only medium QET knew, is the
			/// default and is not written: a terminal that says nothing keeps
			/// behaving as it always did. The others belong to process
			/// symbols: a nozzle connects a pipe, an instrument a signal.
			/// (No medium for a drive shaft: a motor is drawn in its symbol,
			/// and a mechanical link would be a type of signal line.)
		enum Medium {
			Electrical,
			Pipe,
			Signal
		};
		Q_ENUM(Medium)

		TerminalData();
		TerminalData(QGraphicsObject* parent);
		~TerminalData() override;

		void init();

		void setParent(QGraphicsObject* parent);
		void toSettings(QSettings &settings,
				const QString prefix = QString()) const override;
		void fromSettings(const QSettings &settings,
				  const QString prefix = QString()) override;
		QDomElement toXml(QDomDocument &xml_element) const override;
		bool fromXml(const QDomElement &xml_element) override;

		static QString typeToString(TerminalData::Type type);
		static TerminalData::Type typeFromString(const QString &string);
			/// The word the files use for @p medium, for a terminal and for a
			/// conductor alike. Defined here, so that the conductor properties
			/// can use it without the rest of the terminal code.
		static QString mediumToString(TerminalData::Medium medium)
		{
			switch (medium) {
				case Electrical: return QStringLiteral("electrical");
				case Pipe:       return QStringLiteral("pipe");
				case Signal:     return QStringLiteral("signal");
			}
			return QStringLiteral("electrical");
		}
			/// The medium @p string names. Anything else, the absence of the
			/// attribute included, is electrical: what QET always made of a
			/// terminal or a wire, and what it keeps making of a word written
			/// by a later version that it does not know. Such a word, which
			/// would otherwise go unnoticed, is said in the log.
		static TerminalData::Medium mediumFromString(const QString &string)
		{
			if (string == QLatin1String("pipe"))   return Pipe;
			if (string == QLatin1String("signal")) return Signal;
			if (!string.isEmpty() && string != QLatin1String("electrical")) {
				qWarning().noquote() << QStringLiteral("Unknown medium \"%1\", read as electrical")
										.arg(string);
			}
			return Electrical;
		}
			/// The diameter of the <nozzle> @a e of the <pid_nozzles> block of
			/// a placed element: its attribute "dn", else "od" (an outside
			/// diameter), else "thread" (inches, "1 1/2"); none when none is there
		static PipeDiameter diameterFromXml(const QDomElement &e)
		{
			return PipeDiameter::fromXml(e, QStringLiteral("dn"), QStringLiteral("od"),
										 QStringLiteral("thread"));
		}
			/// The diameter of the nozzle of a symbol written as the <terminal>
			/// @a e: the same attributes, prefixed "pid_" as every P&ID
			/// attribute outside a P&ID block
		static PipeDiameter symbolDiameterFromXml(const QDomElement &e)
		{
			return PipeDiameter::fromXml(e, QStringLiteral("pid_dn"), QStringLiteral("pid_od"),
										 QStringLiteral("pid_thread"));
		}
			/// The connection of the nozzle of a symbol written as the
			/// <terminal> @a e (attribute "pid_connection"), a flange when it
			/// says nothing
		static PipeConnection::Type connectionFromXml(const QDomElement &e)
		{
			return PipeConnection::fromString(e.attribute(QStringLiteral("pid_connection")));
		}
			/// The text shown at a terminal: its @a name, and under it the
			/// diameter of a nozzle, if it has one ("DN 100", "Ø 110")
		static QString labelText(const QString &name, const PipeDiameter &diameter,
								 const QLocale &locale = QLocale::c())
		{
			if (!diameter.isSet()) return name;
			const QString shown = diameter.text(locale);
			return name.isEmpty() ? shown : name + QLatin1Char('\n') + shown;
		}
			/// The size of a labelText() in the font of @a metrics
		static QSizeF labelSize(const QFontMetrics &metrics, const QString &text)
		{
			return metrics.size(text.contains(QLatin1Char('\n')) ? 0 : int(Qt::TextSingleLine), text);
		}

			/// Where the name and the size of a nozzle are written (step
			/// 5l-1o): beside its pipe, wherever the symbol says, so that
			/// they read the same way on any symbol, turned or mirrored.
			/// The rects are in a frame whose origin is the end of the
			/// nozzle and whose axes are those of the sheet, turned by
			/// rotation degrees.
		struct NozzleLabels {
			qreal rotation = 0;
			QRectF name;
			QRectF size;
		};
			/// How far the letters of the texts of a nozzle are from the axis
			/// of its pipe (the foot of the letters of the name, the top of
			/// the capitals of the size): beyond the largest mark of a
			/// connection, drawn or not, so that they keep their place
			/// whatever the project draws
		static constexpr qreal nozzleLabelGap = PipeConnection::markHalfHeight + 2;
			/// The room the box of a text keeps under the foot of its
			/// letters in the font of @p metrics: that of the descenders,
			/// which the name of a nozzle ("N5") seldom has
		static qreal roomUnderLetters(const QFontMetrics &metrics)
		{
			return metrics.descent();
		}
			/// The room the box of a text keeps above its capitals and its
			/// figures in the font of @p metrics: that of the accents
		static qreal roomAboveCapitals(const QFontMetrics &metrics)
		{
			return qMax(0, metrics.ascent() - metrics.capHeight());
		}
			/// How far the texts of a nozzle begin from its end, along its
			/// pipe: 1 mm, so that they touch nothing of the symbol drawn at
			/// the end of the nozzle (the cover of a manhole)
		static constexpr qreal nozzleLabelAway = 4;
			/// The NozzleLabels of a nozzle whose pipe leaves towards
			/// @p leaving on the sheet, of texts of the sizes @p name and
			/// @p size (empty for a text not written). Along a horizontal
			/// pipe, nozzleLabelAway from the end of the nozzle: the name
			/// above, the size under it. Along a vertical one, the same
			/// turned to read from the right of the sheet (the name on the
			/// left); or, when
			/// @p turn_vertical is false (a project keeping the texts of its
			/// symbols horizontal), on the right of the pipe, the name above
			/// the size, beside its start.
			/// Above and under the pipe the letters, not the boxes, are
			/// nozzleLabelGap from its axis: the box of the name comes
			/// nearer by @p name_room (roomUnderLetters()), that of the
			/// size by @p size_room (roomAboveCapitals()).
		static NozzleLabels nozzleLabels(Qet::Orientation leaving, bool turn_vertical,
										 const QSizeF &name, const QSizeF &size,
										 qreal name_room = 0, qreal size_room = 0)
		{
			const qreal gap = nozzleLabelGap;
			const qreal away = nozzleLabelAway;
			NozzleLabels labels;
			const bool vertical = leaving == Qet::North || leaving == Qet::South;
			if (vertical && !turn_vertical) {
				const qreal top = leaving == Qet::North ? -away - (name.height() + size.height())
														: away;
				labels.name = QRectF(QPointF(gap, top), name);
				labels.size = QRectF(QPointF(gap, top + name.height()), size);
				return labels;
			}
				//Turned a quarter anticlockwise, the text runs up the sheet:
				//a pipe going north runs along it as one going east
			labels.rotation = vertical ? -90 : 0;
			const bool forward = leaving == Qet::East || leaving == Qet::North;
			labels.name = QRectF(QPointF(forward ? away : -away - name.width(),
										 -gap - name.height() + name_room), name);
			labels.size = QRectF(QPointF(forward ? away : -away - size.width(), gap - size_room), size);
			return labels;
		}
		static QString namePrefix(TerminalData::Medium medium);
		static QString suggestedName(TerminalData::Medium medium,
					     const QString &current,
					     const QStringList &taken);

	// must be public, because this class is a private member
	// of PartTerminal/Terminal and they must access this data
	public:
		/**
			@brief m_orientation
			Orientation of the terminal
		*/
		Qet::Orientation m_orientation;
		/**
			@brief second_point
			Position of the second point of the terminal
			in scene coordinates
		*/
		QPointF m_second_point;
		/**
			@brief m_uuid
			Uuid of the terminal.

			In elementscene.cpp an element gets a new uuid when
			saving the element. In the current state
			each connection is made by using the local position
			of the terminal and a dynamic id. In the new
			case, each terminal should have it's own uuid to
			identify it uniquely. When changing each time this
			uuid, the conductor after updating the part is anymore
			valid. So if in the loaded document a uuid exists,
			use this one and don't create a new one.
		*/
		QUuid m_uuid;
		/**
			@brief m_name
			Name of the element.
			It can be used to create wiring harness tables
		*/
		QString m_name;

		/**
			@brief m_potential
			An optional, symbol-author-chosen grouping identifier shared by
			terminals that belong to the same physical terminal within
			a multi-terminal block. When the function separation checkbox
			(element information potential_isolating) is enabled
			on the parent element, terminals sharing a non-empty,
			matching m_potential stay linked to each other; terminals
			with different (or empty) values become separated from each
			other. Empty by default.
		*/
		QString m_potential;

		/**
			@brief m_pos
			Position of the terminal. The second point is calculated
			from this position and the orientation
			@note
			Important: this variable is only updated during read
			from xml and not during mouse move!
			It is used to store the initial position so that
			PartTerminal and Terminal have access to it.
		*/
		QPointF m_pos;

		TerminalData::Type m_type = TerminalData::Generic;

			/// The medium the terminal connects, see Medium. Kept apart from
			/// m_type, which tells terminal blocks and contacts apart.
		TerminalData::Medium m_medium = TerminalData::Electrical;
			/// The diameter of a nozzle (a terminal of the Pipe medium): a DN
			/// (a flange) or an outside diameter (a socket); none by default
		PipeDiameter m_diameter;
			/// How a pipe is joined to a nozzle; a flange by default
		PipeConnection::Type m_connection = PipeConnection::Flange;
			/// The condition on the options of the symbol under which the
			/// terminal is shown, as the show_if of a part of the drawing
			/// (ElementOptions::isShown()); empty: always shown
		QString m_show_if;

		/// Whether to display the terminal name as a text label
		bool m_show_name = false;
		/// Position of the text label relative to the terminal
		QPointF m_label_pos{0.0, 0.0};
		/// Font used for the text label
		QFont m_label_font;
		/// Rotation of the text label in degrees
		qreal m_label_rotation = 0.0;
		/// Horizontal alignment of the text label
		Qt::Alignment m_label_halignment = Qt::AlignHCenter;
		/// Vertical alignment of the text label
		Qt::Alignment m_label_valignment = Qt::AlignVCenter;
		/// Whether to draw a frame around the text label
		bool m_label_frame = false;
		/// Color of the text label
		QColor m_label_color = Qt::black;

		/// Whether this terminal uses a label from the master's contact group
		bool m_use_master_label = false;
		/// Index into the master's contact group labels (T1=0, T2=1, ..., T20=19)
		int m_master_label_index = 0;

	private:
		QGraphicsObject* q{nullptr};
};

#endif // TERMINALDATA_H

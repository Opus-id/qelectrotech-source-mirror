// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "conductorsegment.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "properties/potentialtypes.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "signalmark.h"
#include "signalproperties.h"

#include <QDomDocument>
#include <QGraphicsSceneHoverEvent>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

// How a signal line is drawn on a folio: a thin line, whatever width, style
// or pattern of dashes it holds as a wire; in the colour the project gives
// the signal lines; with the mark of its type of signal repeated along it,
// the default one unless the project says another. The potential types of the project never reach it. A wire and a pipe are
// drawn as before.
class tst_signaldrawing : public QObject
{
	Q_OBJECT

		// A symbol with two terminals N1 (west) and N2 (east), of the medium
		// @p medium ("" for none)
	static QDomElement definition(QDomDocument &doc, const QString &uuid, const QString &medium)
	{
		const QString attr = medium.isEmpty()
				? QString()
				: QStringLiteral(" medium=\"%1\"").arg(medium);
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\">"
			"<uuid uuid=\"{%1}\"/><names><name lang=\"en\">S</name></names>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-style:normal;line-weight:normal;"
			"filling:none;color:black\" antialias=\"false\"/>"
			"<terminal name=\"N1\" x=\"-10\" y=\"0\" orientation=\"w\" type=\"Generic\"%2/>"
			"<terminal name=\"N2\" x=\"10\" y=\"0\" orientation=\"e\" type=\"Generic\"%2/>"
			"</description></definition>").arg(uuid, attr));
		return doc.documentElement();
	}

	QETProject m_project;
	Diagram *m_folio = nullptr;
	ElementsLocation m_electrical, m_nozzles, m_signals;
	qreal m_next_y = 0;

	ElementsLocation add(const QString &file, const QDomElement &definition)
	{
		m_project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/sdraw"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/sdraw/") + file, &m_project);
	}

	Element *place(const ElementsLocation &location, const QPointF &pos)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		m_folio->addItem(element);
		element->setPos(pos);
		return element;
	}

		// A straight conductor between two symbols of a row of their own
	Conductor *draw(const ElementsLocation &location)
	{
		m_next_y += 100;
		Element *a = place(location, QPointF(0, m_next_y));
		Element *b = place(location, QPointF(240, m_next_y));
		if (!a || !b) return nullptr;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

	static void setType(Conductor *line, SignalProperties::SignalType type)
	{
		ConductorProperties p = line->properties();
		p.m_signal.signal_type = type;
		line->setProperties(p);
	}

		// The points of @p conductor, on the folio
	static QList<QPointF> points(const Conductor *conductor)
	{
		QList<QPointF> found;
		for (ConductorSegment *segment : conductor->segmentsList()) {
			if (found.isEmpty()) {
				found << conductor->mapToScene(segment->firstPoint());
			}
			found << conductor->mapToScene(segment->secondPoint());
		}
		return found;
	}

		// What is drawn of a conductor, as on paper: an image, and where the
		// folio is in it
	struct Picture
	{
		QImage image;
		QRectF area;
		qreal scale = 1;

		bool inked(int x, int y) const
		{
			return image.valid(x, y) && (image.pixel(x, y) & 0xffffff) != 0xffffff;
		}
			// Ink in the square of @p reach pixels about the point @p at of
			// the folio
		bool inkedNear(const QPointF &at, int reach = 1) const
		{
			const int x = qFloor((at.x() - area.left()) * scale);
			const int y = qFloor((at.y() - area.top()) * scale);
			for (int dy = -reach ; dy <= reach ; ++dy) {
				for (int dx = -reach ; dx <= reach ; ++dx) {
					if (inked(x + dx, y + dy)) return true;
				}
			}
			return false;
		}
		int inkedPixels() const
		{
			int count = 0;
			for (int y = 0 ; y < image.height() ; ++y) {
				for (int x = 0 ; x < image.width() ; ++x) {
					if (inked(x, y)) ++count;
				}
			}
			return count;
		}
			// Ink @p away pixels at least above or under the row of @p line_y
		int inkedOffTheLine(qreal line_y, int away = 3) const
		{
			const int row = qFloor((line_y - area.top()) * scale);
			int count = 0;
			for (int y = 0 ; y < image.height() ; ++y) {
				if (qAbs(y - row) < away) continue;
				for (int x = 0 ; x < image.width() ; ++x) {
					if (inked(x, y)) ++count;
				}
			}
			return count;
		}
			// How many pixels of the column of @p folio_x are inked
		int inkedInColumn(qreal folio_x) const
		{
			const int x = qFloor((folio_x - area.left()) * scale);
			int count = 0;
			for (int y = 0 ; y < image.height() ; ++y) {
				if (inked(x, y)) ++count;
			}
			return count;
		}
		int pixelsOf(const QColor &color) const
		{
			int count = 0;
			for (int y = 0 ; y < image.height() ; ++y) {
				for (int x = 0 ; x < image.width() ; ++x) {
					if (QColor(image.pixel(x, y)) == color) ++count;
				}
			}
			return count;
		}
	};

	static Picture picture(Conductor *conductor, qreal scale = 1.0)
	{
		Picture p;
		p.scale = scale;
		p.area = conductor->sceneBoundingRect().adjusted(-4, -4, 4, 4);
		p.image = QImage((p.area.size() * scale).toSize(), QImage::Format_RGB32);
		p.image.fill(Qt::white);
		QPainter painter(&p.image);
		painter.scale(scale, scale);
		painter.translate(-p.area.topLeft() + conductor->scenePos());
		QStyleOptionGraphicsItem option;
		conductor->paint(&painter, &option, nullptr);
		painter.end();
		return p;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("sdraw"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
							   definition(a, "a1111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "a2222222-2222-2222-2222-222222222222", "pipe"));
			m_signals = add("signals.elmt",
							definition(c, "a3333333-3333-3333-3333-333333333333", "signal"));
			QVERIFY(m_electrical.exist() && m_nozzles.exist() && m_signals.exist());
			m_folio = m_project.addNewDiagram();
		}

			// ---- the line ----
			// A wire: its own width, style and pattern, as always. A signal
			// line: a thin solid line; what it holds as a wire is kept, and
			// written, but not applied
		void aSignalLineIsThinWhateverItHoldsAsAWire()
		{
			ConductorProperties wire;
			wire.cond_size = 3;
			wire.style = Qt::DashDotLine;
			wire.m_dash_pattern.enabled = true;
			QCOMPARE(wire.lineWidth(), 3.0);
			QCOMPARE(wire.lineStyle(), Qt::DashDotLine);
			QVERIFY(wire.usesDashPattern());

			ConductorProperties line = wire;
			line.m_medium = TerminalData::Signal;
			QCOMPARE(line.lineWidth(), 1.0);
			QCOMPARE(line.lineStyle(), Qt::SolidLine);
			QVERIFY(!line.usesDashPattern());

				// opening and saving a file changes nothing of it
			QDomDocument doc;
			QDomElement saved = doc.createElement(QStringLiteral("conductor"));
			line.toXml(saved);
			ConductorProperties back;
			back.fromXml(saved);
			QCOMPARE(back.cond_size, 3.0);
			QCOMPARE(back.style, Qt::DashDotLine);
			QVERIFY(back.m_dash_pattern.enabled);
		}

		void aSignalLineIsDrawnOneUnitWide()
		{
			Conductor *line = draw(m_signals);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && wire);
			QCOMPARE(line->medium(), TerminalData::Signal);
			for (Conductor *conductor : {line, wire}) {
				ConductorProperties p = conductor->properties();
				p.cond_size = 6;
				conductor->setProperties(p);
			}
				// away from its marks, which stand a mark from its ends at least
			const QList<QPointF> ends = points(line);
			QVERIFY(ends.size() >= 2);
			QCOMPARE(picture(line).inkedInColumn(ends.first().x() + 4), 1);
			QCOMPARE(picture(wire).inkedInColumn(points(wire).first().x() + 4), 6);
				// and its click area is that of a thin line
			QGraphicsSceneHoverEvent leave(QEvent::GraphicsSceneHoverLeave);
			m_folio->sendEvent(line, &leave);
			QCOMPARE(line->shape().boundingRect().height(), Conductor::min_click_width);
		}

			// ---- its colour ----
		void aSignalLineTakesTheColourOfTheSignalLinesOfTheProject()
		{
			Conductor *line = draw(m_signals);
			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && pipe && wire);
			const QColor line_before = line->drawnColor();
			const QColor pipe_before = pipe->drawnColor();
			const QColor wire_before = wire->drawnColor();
			QVERIFY(picture(line).pixelsOf(Qt::black) > 0);

			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;signal;#0000ff\n")));
			QCOMPARE(line->drawnColor(), QColor(0, 0, 255));
			QCOMPARE(pipe->drawnColor(), pipe_before);
			QCOMPARE(wire->drawnColor(), wire_before);
				// its line and its marks
			const Picture blue = picture(line);
			QVERIFY(blue.pixelsOf(QColor(0, 0, 255)) > 0);
			QCOMPARE(blue.pixelsOf(Qt::black), 0);
			QVERIFY(blue.inkedOffTheLine(points(line).first().y()) > 0);

				// the colour of the pipes is not that of the signal lines
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;pipe;#808000\n")));
			QCOMPARE(line->drawnColor(), line_before);
			QCOMPARE(pipe->drawnColor(), QColor(0x80, 0x80, 0));

			m_project.setPidCodification(PidCodification());
			QCOMPARE(line->drawnColor(), line_before);
		}

			// ---- its marks ----
		void eachTypeOfSignalHasItsMarksAlongTheLine()
		{
			Conductor *line = draw(m_signals);
			Conductor *wire = draw(m_electrical);
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(line && wire && pipe);
			const qreal y = points(line).first().y();

			QList<QImage> pictures;
			for (SignalProperties::SignalType type : SignalProperties::types()) {
				setType(line, type);
				QCOMPARE(int(line->signalMark()), int(SignalMark::defaultMark(type)));
				const Picture p = picture(line);
				if (SignalMark::isRepeated(SignalMark::defaultMark(type))) {
					QVERIFY2(p.inkedOffTheLine(y) > 0,
							 qPrintable(SignalProperties::typeToString(type)));
				} else {
						// the connection to the process: a plain line
					QCOMPARE(int(type), int(SignalProperties::ProcessConnection));
					QCOMPARE(p.inkedOffTheLine(y), 0);
				}
				for (const QImage &other : std::as_const(pictures)) {
					QVERIFY2(p.image != other, qPrintable(SignalProperties::typeToString(type)));
				}
				pictures << p.image;
			}
				// a wire and a pipe have none
			QCOMPARE(int(wire->signalMark()), int(SignalMark::None));
			QCOMPARE(int(pipe->signalMark()), int(SignalMark::None));
			QCOMPARE(picture(wire).inkedOffTheLine(points(wire).first().y()), 0);
			QCOMPARE(picture(pipe).inkedOffTheLine(points(pipe).first().y()), 0);
		}

			// Where SignalMark::places() says, and nowhere else
		void theMarksStandWhereTheRuleSays()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			setType(line, SignalProperties::Pneumatic);
			const QList<QPointF> ends = points(line);
			const QList<SignalMark::Place> places =
					SignalMark::places(ends, SignalMark::length(SignalMark::AMark));
			QVERIFY(places.size() >= 2);
			const Picture p = picture(line);
			for (const SignalMark::Place &place : places) {
					// the top of the "A", above the line
				QVERIFY(p.inkedNear(place.at + QPointF(0, -SignalMark::height / 2), 1));
			}
				// between two marks, the line alone
			const QPointF between = (places.at(0).at + places.at(1).at) / 2;
			QCOMPARE(p.inkedInColumn(between.x()), 1);
				// and none near its ends
			QCOMPARE(p.inkedInColumn(ends.first().x() + 4), 1);
			QCOMPARE(p.inkedInColumn(ends.last().x() - 4), 1);
		}

			// An open circle stands in the line; any other mark over it
		void theLineStopsAtTheCirclesOfADataLink()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			setType(line, SignalProperties::DataLink);
			const QList<SignalMark::Place> places =
					SignalMark::places(points(line), SignalMark::length(SignalMark::Circle));
			QVERIFY(!places.isEmpty());
			const Picture circles = picture(line);
			for (const SignalMark::Place &place : places) {
					// the four pixels about its middle: no line there
				for (const QPointF &d : {QPointF(0, 0), QPointF(-1, 0), QPointF(0, -1), QPointF(-1, -1)}) {
					QVERIFY(!circles.inkedNear(place.at + d, 0));
				}
				QVERIFY(circles.inkedNear(place.at + QPointF(SignalMark::circleDiameter / 2, 0), 1));
				QVERIFY(circles.inkedNear(place.at + QPointF(0, -SignalMark::circleDiameter / 2), 1));
			}

			setType(line, SignalProperties::Undefined);
			const QList<SignalMark::Place> bars =
					SignalMark::places(points(line), SignalMark::length(SignalMark::Bar1));
			QVERIFY(!bars.isEmpty());
			QVERIFY(picture(line).inkedNear(bars.first().at, 1));
		}

			// A smaller picture, as a folio printed small: the marks are
			// still there
		void theMarksFollowTheZoom()
		{
			Conductor *line = draw(m_signals);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && wire);
			setType(line, SignalProperties::Capillary);
			for (qreal scale : {2.0, 0.4}) {
				const Picture p = picture(line, scale);
				QVERIFY2(p.inkedOffTheLine(points(line).first().y(), 2) > 0,
						 qPrintable(QString::number(scale)));
				QVERIFY(p.inkedPixels() > picture(wire, scale).inkedPixels());
			}
		}

			// ---- the marks the project says ----
			// A project says which mark a type of signal takes
			// (PidCodification::signal_marks): its lines are drawn with it
			// at once, and with the default one again when it says no more
		void theProjectSaysTheMarkOfATypeOfSignal()
		{
			Conductor *line = draw(m_signals);
			Conductor *other = draw(m_signals);
			QVERIFY(line && other);
			setType(line, SignalProperties::Pneumatic);
			setType(other, SignalProperties::Capillary);
			QCOMPARE(int(line->signalMark()), int(SignalMark::AMark));
			const QImage before = picture(line).image;
			const QImage other_before = picture(other).image;

			PidCodification c;
			c.setSignalMark(SignalProperties::Pneumatic, SignalMark::Bar2);
			m_project.setPidCodification(c);
			QCOMPARE(int(line->signalMark()), int(SignalMark::Bar2));
			QVERIFY(picture(line).image != before);
			QVERIFY(picture(line).inkedOffTheLine(points(line).first().y()) > 0);
				// a type it does not name keeps its mark
			QCOMPARE(int(other->signalMark()), int(SignalMark::Cross));
			QCOMPARE(picture(other).image, other_before);

				// a mark of a later version: the default one meanwhile
			c.signal_marks.insert(QStringLiteral("pneumatic"), QStringLiteral("diamond"));
			m_project.setPidCodification(c);
			QCOMPARE(int(line->signalMark()), int(SignalMark::AMark));
			QCOMPARE(picture(line).image, before);

			m_project.setPidCodification(PidCodification());
			QCOMPARE(int(line->signalMark()), int(SignalMark::AMark));
			QCOMPARE(picture(line).image, before);
		}

			// Two marks no type takes by default: a plain line, nothing
			// along it; a dashed line, cut and nothing off it
		void aTypeMayBeDrawnPlainOrDashed()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			setType(line, SignalProperties::Electrical);
			const qreal y = points(line).first().y();
			QVERIFY(picture(line).inkedOffTheLine(y) > 0);

			PidCodification c;
			c.setSignalMark(SignalProperties::Electrical, SignalMark::None);
			m_project.setPidCodification(c);
			const Picture plain = picture(line);
			QCOMPARE(plain.inkedOffTheLine(y), 0);
			const int whole = plain.inkedPixels();
			QVERIFY(whole > 0);

			c.setSignalMark(SignalProperties::Electrical, SignalMark::Dashes);
			m_project.setPidCodification(c);
			const Picture dashed = picture(line);
			QCOMPARE(dashed.inkedOffTheLine(y), 0);
				// dashes of 6 units, 4 between two: six tenths of the line
			const QByteArray counts = QByteArray::number(dashed.inkedPixels()) + " of "
									  + QByteArray::number(whole);
			QVERIFY2(dashed.inkedPixels() > whole * 0.45, counts.constData());
			QVERIFY2(dashed.inkedPixels() < whole * 0.75, counts.constData());
				// a dashed line stays a thin one
			QCOMPARE(dashed.inkedInColumn(points(line).first().x() + 2), 1);

			m_project.setPidCodification(PidCodification());
		}

			// The marks of the project are those of its signal lines only
		void theMarksOfTheProjectNeverReachAWireOrAPipe()
		{
			Conductor *wire = draw(m_electrical);
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(wire && pipe);
			const QImage wire_before = picture(wire).image;
			const QImage pipe_before = picture(pipe).image;

			PidCodification c;
			for (SignalProperties::SignalType type : SignalProperties::types()) {
				c.setSignalMark(type, SignalMark::Bar3);
			}
			m_project.setPidCodification(c);
			QCOMPARE(int(wire->signalMark()), int(SignalMark::None));
			QCOMPARE(int(pipe->signalMark()), int(SignalMark::None));
			QCOMPARE(picture(wire).image, wire_before);
			QCOMPARE(picture(pipe).image, pipe_before);

			m_project.setPidCodification(PidCodification());
		}

			// ---- the potential types of the wires ----
			// A project which draws its wires from potential types: a signal
			// line has no potential, and keeps its look
			// Step 7c-bis: a line drawn by hand in a U, whose symbols come
			// level, would go back over itself. A signal line routes itself
			// again; an electrical wire keeps its path, as before
		void aSignalLineGoingBackOverItselfRoutesItselfAgain()
		{
			for (const bool signal : {true, false}) {
				m_next_y += 200;
				const qreal y = m_next_y;
				const ElementsLocation &location = signal ? m_signals : m_electrical;
					// N2 of a at (10, y), east; N1 of b at (200, y + 150), west;
					// a U on the way down
				Element *a = place(location, QPointF(0, y));
				Element *b = place(location, QPointF(210, y + 150));
				QVERIFY(a && b);
				auto *line = new Conductor(a->terminals().at(1), b->terminals().at(0));
				m_folio->addItem(line);
				QCOMPARE(line->medium(), signal ? TerminalData::Signal : TerminalData::Electrical);
				QVERIFY(line->setPathPoints({QPointF(10, y), QPointF(100, y), QPointF(100, y + 100),
											 QPointF(60, y + 100), QPointF(60, y + 150),
											 QPointF(200, y + 150)}));
					// b comes level with a: the U is crushed, its bottom
					// goes back over its top
				b->setPos(210, y);
				QList<QPointF> path;
				for (const QPointF &point : points(line)) {
					path << line->mapFromScene(point);
				}
				const QList<QPointF> folded{QPointF(10, y), QPointF(100, y), QPointF(100, y),
											QPointF(60, y), QPointF(60, y), QPointF(200, y)};
				QList<QPointF> folded_here;
				for (const QPointF &point : folded) {
					folded_here << line->mapFromScene(point);
				}
				if (signal) {
					QVERIFY(line->foldsBack(folded_here));
					QString seen;
					for (const QPointF &point : points(line)) {
						seen += QStringLiteral(" (%1, %2)").arg(point.x()).arg(point.y() - y);
					}
					QVERIFY2(!line->foldsBack(path), qPrintable(seen));
					QVERIFY(points(line) != folded);
				} else {
					QVERIFY(!line->foldsBack(folded_here));
					QCOMPARE(points(line), folded);
				}
			}
		}

			// Step 7c-bis: a detour drawn by hand on a signal line stays on its
			// side when its ends come level
		void aDetourOfASignalLineKeepsItsSide()
		{
			m_next_y += 400;
			const qreal y = m_next_y;
				// N2 of a at (10, y + 100), east; N1 of b at (200, y), west
			Element *a = place(m_signals, QPointF(0, y + 100));
			Element *b = place(m_signals, QPointF(210, y));
			QVERIFY(a && b);
			auto *line = new Conductor(a->terminals().at(1), b->terminals().at(0));
			m_folio->addItem(line);
			QCOMPARE(line->medium(), TerminalData::Signal);
			QVERIFY(line->setPathPoints({QPointF(10, y + 100), QPointF(40, y + 100), QPointF(40, y + 200),
										 QPointF(110, y + 200), QPointF(110, y), QPointF(200, y)}));
			a->setPos(0, y);
			const QList<QPointF> kept{QPointF(10, y), QPointF(40, y), QPointF(40, y + 150),
									  QPointF(110, y + 150), QPointF(110, y), QPointF(200, y)};
			QCOMPARE(points(line), kept);
		}

		void thePotentialTypesNeverReachASignalLine()
		{
			Conductor *line = draw(m_signals);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && wire);
			PotentialTypes types;
			types.setEnabled(true);
			types.setTypes({{QStringLiteral("N"), QColor(0, 0, 255), Qt::DashLine, 3}});
			types.setMode(PotentialTypes::Mode::AtSource);
			m_project.setPotentialTypes(types);
			for (Conductor *conductor : {line, wire}) {
				ConductorProperties p = conductor->properties();
				p.m_potential_type = QStringLiteral("N");
				conductor->setProperties(p);
			}

			PotentialType type;
			QVERIFY(wire->potentialTypeFormat(&type));
			QVERIFY(picture(wire).pixelsOf(QColor(0, 0, 255)) > 0);

			QVERIFY(!line->potentialTypeFormat(&type));
			QVERIFY(line->drawnColor() != QColor(0, 0, 255));
			const Picture p = picture(line);
			QCOMPARE(p.pixelsOf(QColor(0, 0, 255)), 0);
			QCOMPARE(p.inkedInColumn(points(line).first().x() + 4), 1);

			m_project.setPotentialTypes(PotentialTypes());
		}
};

QET_APP_TEST_MAIN(tst_signaldrawing)
#include "tst_signaldrawing.moc"

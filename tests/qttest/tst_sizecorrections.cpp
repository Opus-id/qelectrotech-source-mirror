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
#include "qetapptest.h"

#include "diagram.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidsizecorrections.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QUndoCommand>
#include <QUndoStack>

/**
	"Correct" on a size reported (step 8r-3 a): a diamond of two sizes at a
	nozzle, a band of a size out of its series. Proposals only, one undo
	step each, each named as in the submenu.
*/
class tst_sizecorrections : public QObject
{
	Q_OBJECT

		// N1 has the DN @p n1_dn (none for 0), N2 none
	static ElementsLocation addSymbol(QETProject &project, const QString &file,
									  const QString &uuid, int n1_dn,
									  const QString &extra = QString())
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("sizes"), NamesList());
		QDomDocument doc;
		doc.setContent(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" %3"
			"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
			"<uuid uuid=\"%1\"/>"
			"<names><name lang=\"en\">Tank</name></names>"
			"<description>"
			"<line x1=\"-20\" y1=\"0\" x2=\"20\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" antialias=\"false\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
			"<terminal uuid=\"{7c000000-0000-0000-0000-000000000001}\" name=\"N1\" x=\"-20\" y=\"0\" "
			"orientation=\"w\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" pid_dn=\"%2\" "
			"show_name=\"true\"/>"
			"<terminal uuid=\"{7c000000-0000-0000-0000-000000000002}\" name=\"N2\" x=\"20\" y=\"0\" "
			"orientation=\"e\" type=\"Generic\" medium=\"pipe\" pid_connection=\"plain_end\" show_name=\"true\"/>"
			"</description></definition>").arg(uuid).arg(n1_dn).arg(extra));
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/sizes"), file, doc.documentElement());
		return ElementsLocation(QStringLiteral("embed://import/sizes/") + file, &project);
	}

	static ElementsLocation addTank(QETProject &project)
	{
		return addSymbol(project, QStringLiteral("tank.elmt"),
						 QStringLiteral("{7c111111-1111-1111-1111-111111111111}"), 80);
	}

	static ElementsLocation addValve(QETProject &project)
	{
		return addSymbol(project, QStringLiteral("valve.elmt"),
						 QStringLiteral("{7c333333-3333-3333-3333-333333333333}"), 0,
						 QStringLiteral("pid_inline=\"true\" "));
	}

	static Element *place(Diagram *folio, const ElementsLocation &location, qreal x)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (!element || state) return nullptr;
		folio->addItem(element);
		element->setPos(x, 200);
		return element;
	}

	static Terminal *terminal(const Element *element, const QString &name)
	{
		for (Terminal *t : element->terminals()) {
			if (t->baseName() == name) return t;
		}
		return nullptr;
	}

	static Conductor *connect(Diagram *folio, const Element *a, const QString &from,
							  const Element *b, const QString &to)
	{
		auto *pipe = new Conductor(terminal(a, from), terminal(b, to));
		folio->addItem(pipe);
		return pipe;
	}

	static void setSize(Conductor *pipe, int dn, const QString &material = QString())
	{
		ConductorProperties properties = pipe->properties();
		properties.m_pipe.nominal = dn ? PipeDiameter::nominal(dn) : PipeDiameter();
		properties.m_pipe.outside = PipeDiameter();
		if (!material.isEmpty()) {
			properties.m_pipe.material = material;
		}
		pipe->setProperties(properties);
	}

	static int dn(const Conductor *pipe)
	{
		return qRound(pipe->properties().m_pipe.nominal.value());
	}

	static QStringList texts(const QList<PidSizeCorrections::Proposal> &found)
	{
		QStringList list;
		for (const PidSizeCorrections::Proposal &proposal : found) {
			list << proposal.text;
		}
		return list;
	}

		// The proposal of text @p text applied as the submenu does, in
		// one step pushed on the folio; false if none or nothing done
	static bool apply(Diagram *folio, const QList<PidSizeCorrections::Proposal> &found,
					  const QString &text, QString *said = nullptr)
	{
		for (const PidSizeCorrections::Proposal &proposal : found) {
			if (proposal.text != text) continue;
			QUndoCommand *undo = PidSizeCorrections::apply(
						proposal, nullptr, said, [](const QString &, const QStringList &) { return 0; });
			if (!undo) return false;
			if (undo->text() != text) {
				delete undo;
				return false;
			}
			folio->undoStack().push(undo);
			return true;
		}
		return false;
	}

	private slots:
			// A nozzle of DN 80 on a pipe of DN 100: its size to the line, or
			// the size of the line to it; from the pipe or from the symbol
		void aNozzleGapOffersBothWays()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation tank = addTank(project);
			Element *a = place(folio, tank, 200);
			Element *b = place(folio, tank, 400);
			QVERIFY(a && b);
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			setSize(pipe, 100);
			Terminal *n1 = terminal(b, QStringLiteral("N1"));
			QVERIFY(n1->hasDiameterGap());
			QCOMPARE(n1->diameterGap().kind, Terminal::DiameterGap::Nozzle);

			const QStringList expected{QStringLiteral("Give DN 80 to the line"),
									   QStringLiteral("Give DN 100 to the nozzle N1")};
				// on the pipe, wherever the click: the only end marked
			QCOMPARE(texts(PidSizeCorrections::proposals(pipe, terminal(a, QStringLiteral("N2"))->dockConductor())),
					 expected);
				// on the symbol
			const QList<PidSizeCorrections::Proposal> found =
					PidSizeCorrections::proposals(b, n1->dockConductor());
			QCOMPARE(texts(found), expected);

				// its size to the line: one step, undone
			const int steps = folio->undoStack().count();
			QVERIFY(apply(folio, found, expected.first()));
			QCOMPARE(dn(pipe), 80);
			QVERIFY(!n1->hasDiameterGap());
			QCOMPARE(folio->undoStack().count(), steps + 1);
			QVERIFY(PidSizeCorrections::proposals(b, n1->dockConductor()).isEmpty());
			folio->undoStack().undo();
			QCOMPARE(dn(pipe), 100);

				// the size of the line to it: its own, one step, undone
			QVERIFY(apply(folio, found, expected.last()));
			QCOMPARE(b->nozzleDiameters().value(QStringLiteral("N1")), PipeDiameter::nominal(100));
			QCOMPARE(dn(pipe), 100);
			QVERIFY(!n1->hasDiameterGap());
			folio->undoStack().undo();
			QVERIFY(b->nozzleDiameters().isEmpty());
			QVERIFY(n1->hasDiameterGap());
		}

			// The size goes as far as the panel takes it: through a valve, up to
			// the equipment
		void theLineTakesItThroughAValve()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation tank = addTank(project);
			const ElementsLocation plain = addSymbol(project, QStringLiteral("plain.elmt"),
													 QStringLiteral("{7c444444-4444-4444-4444-444444444444}"), 0);
			Element *start = place(folio, plain, 0);
			Element *a = place(folio, plain, 200);
			Element *valve = place(folio, addValve(project), 400);
			Element *b = place(folio, tank, 600);
			QVERIFY(start && a && valve && b);
			Conductor *before = connect(folio, start, QStringLiteral("N2"), a, QStringLiteral("N1"));
			Conductor *first = connect(folio, a, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *second = connect(folio, valve, QStringLiteral("N2"), b, QStringLiteral("N1"));
			for (Conductor *pipe : {before, first, second}) {
				setSize(pipe, 100);
			}
			Terminal *n1 = terminal(b, QStringLiteral("N1"));
			QVERIFY(apply(folio, PidSizeCorrections::proposals(second, n1->dockConductor()),
						  QStringLiteral("Give DN 80 to the line")));
			QCOMPARE(dn(first), 80);
			QCOMPARE(dn(second), 80);
				// the equipment a stops it
			QCOMPARE(dn(before), 100);
			QVERIFY(!n1->hasDiameterGap());
		}

			// A size which is not one of the material of the line is not
			// offered to it (choice of the user of 25/11)
		void aSizeOutOfTheSeriesOfTheLineIsNotOffered()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			codification.materials = {steel};
			project.setPidCodification(codification);
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation tank = addTank(project);
			Element *a = place(folio, tank, 200);
			Element *b = place(folio, tank, 400);
			QVERIFY(a && b);
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			setSize(pipe, 80, QStringLiteral("M02"));
			b->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::nominal(150)}});
			Terminal *n1 = terminal(b, QStringLiteral("N1"));
			QVERIFY(n1->hasDiameterGap());
			QCOMPARE(texts(PidSizeCorrections::proposals(b, n1->dockConductor())),
					 QStringList{QStringLiteral("Give DN 80 to the nozzle N1")});
				// one of its sizes is
			b->setNozzleDiameters({{QStringLiteral("N1"), PipeDiameter::nominal(100)}});
			QCOMPARE(texts(PidSizeCorrections::proposals(b, n1->dockConductor())),
					 QStringList({QStringLiteral("Give DN 100 to the line"),
								  QStringLiteral("Give DN 80 to the nozzle N1")}));
		}

			// An inline item between two sizes: the whole line takes one or
			// the other, through it
		void anInlineItemBetweenTwoSizesOffersBoth()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation plain = addSymbol(project, QStringLiteral("plain.elmt"),
													 QStringLiteral("{7c444444-4444-4444-4444-444444444444}"), 0);
			Element *a = place(folio, plain, 200);
			Element *valve = place(folio, addValve(project), 400);
			Element *b = place(folio, plain, 600);
			QVERIFY(a && valve && b);
			Conductor *first = connect(folio, a, QStringLiteral("N2"), valve, QStringLiteral("N1"));
			Conductor *second = connect(folio, valve, QStringLiteral("N2"), b, QStringLiteral("N1"));
			setSize(first, 100);
			setSize(second, 80);
			Terminal *in = terminal(valve, QStringLiteral("N1"));
			QCOMPARE(in->diameterGap().kind, Terminal::DiameterGap::Inline);

			const QList<PidSizeCorrections::Proposal> found =
					PidSizeCorrections::proposals(valve, in->dockConductor());
			QCOMPARE(texts(found), QStringList({QStringLiteral("Change the line to DN 100"),
											   QStringLiteral("Change the line to DN 80")}));
				// the same from the pipe of the other side
			QCOMPARE(texts(PidSizeCorrections::proposals(
							   second, terminal(valve, QStringLiteral("N2"))->dockConductor())),
					 QStringList({QStringLiteral("Change the line to DN 80"),
								  QStringLiteral("Change the line to DN 100")}));
			QVERIFY(apply(folio, found, QStringLiteral("Change the line to DN 80")));
			QCOMPARE(dn(first), 80);
			QCOMPARE(dn(second), 80);
			QVERIFY(!in->hasDiameterGap());
			folio->undoStack().undo();
			QCOMPARE(dn(first), 100);
			QCOMPARE(dn(second), 80);
		}

			// A band: the sizes of the material which frame it, one at an end
			// of the series
		void aBandOffersTheSizesWhichFrameIt()
		{
			QETProject project;
			PidCodification codification;
			PidCodification::Entry steel;
			steel.code = QStringLiteral("M02");
			steel.addSize(0, 65);
			steel.addSize(0, 80);
			steel.addSize(0, 100);
			codification.materials = {steel};
			project.setPidCodification(codification);
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation plain = addSymbol(project, QStringLiteral("plain.elmt"),
													 QStringLiteral("{7c444444-4444-4444-4444-444444444444}"), 0);
			Element *a = place(folio, plain, 200);
			Element *b = place(folio, plain, 400);
			QVERIFY(a && b);
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			setSize(pipe, 90, QStringLiteral("M02"));
			QVERIFY(!pipe->offSeriesNote().isEmpty());

			const QList<PidSizeCorrections::Proposal> found =
					PidSizeCorrections::proposals(pipe, pipe->sceneBoundingRect().center());
			QCOMPARE(texts(found), QStringList({QStringLiteral("Change the line to DN 80"),
											   QStringLiteral("Change the line to DN 100")}));
			QVERIFY(apply(folio, found, QStringLiteral("Change the line to DN 100")));
			QCOMPARE(dn(pipe), 100);
			QVERIFY(pipe->offSeriesNote().isEmpty());
			folio->undoStack().undo();
			QCOMPARE(dn(pipe), 90);

				// above the series: the largest one only
			setSize(pipe, 150);
			QCOMPARE(texts(PidSizeCorrections::proposals(pipe, pipe->sceneBoundingRect().center())),
					 QStringList{QStringLiteral("Change the line to DN 100")});
		}

			// Both ends marked: the one nearest to the click
		void theEndNearestToTheClick()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation tank = addTank(project);
			Element *a = place(folio, tank, 200);
			Element *b = place(folio, tank, 600);
			QVERIFY(a && b);
			a->setNozzleDiameters({{QStringLiteral("N2"), PipeDiameter::nominal(65)}});
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			setSize(pipe, 100);
			const QPointF near_a = terminal(a, QStringLiteral("N2"))->dockConductor();
			const QPointF near_b = terminal(b, QStringLiteral("N1"))->dockConductor();
			QCOMPARE(texts(PidSizeCorrections::proposals(pipe, near_a)).first(),
					 QStringLiteral("Give DN 65 to the line"));
			QCOMPARE(texts(PidSizeCorrections::proposals(pipe, near_b)).first(),
					 QStringLiteral("Give DN 80 to the line"));
		}

			// Nothing reported: nothing offered; a wire is never looked at
		void nothingReportedNothingOffered()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			const ElementsLocation tank = addTank(project);
			Element *a = place(folio, tank, 200);
			Element *b = place(folio, tank, 400);
			QVERIFY(a && b);
			Conductor *pipe = connect(folio, a, QStringLiteral("N2"), b, QStringLiteral("N1"));
			setSize(pipe, 80);
			QVERIFY(PidSizeCorrections::proposals(pipe, pipe->sceneBoundingRect().center()).isEmpty());
			QVERIFY(PidSizeCorrections::proposals(b, b->scenePos()).isEmpty());
			QVERIFY(PidSizeCorrections::proposals(nullptr, QPointF()).isEmpty());
		}
};

QET_APP_TEST_MAIN(tst_sizecorrections)
#include "tst_sizecorrections.moc"

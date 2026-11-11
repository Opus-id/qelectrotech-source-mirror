// SPDX-License-Identifier: GPL-2.0-or-later
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "autoNum/numerotationcontext.h"
#include "autoNum/renumberconductorscommand.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "duplicatelabels.h"
#include "factory/elementfactory.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QSqlQuery>
#include <QUndoStack>
#include <QUuid>
#include <QtTest>

/**
	The functions of wires that came with upstream after the medium was
	added leave the pipes alone, as every other function of wires does: a
	potential is electrical, and a pipe, a signal or a drive line is never
	part of one (Conductor::relatedPotentialConductors()).
	- Renumber conductors gives no pipe a wire number, and the text of a
	  pipe on a folio not chosen keeps no wire number from being given.
	- The duplicate check never calls the pipes of one line duplicates,
	  and never follows a potential through a pipe.
	- potential_view lists no pipe.
*/
class tst_mediumguards : public QObject
{
	Q_OBJECT

		//A symbol 20 x 40, terminals north and south, of @p medium ("" for
		//electrical)
	static Element *symbol(Diagram *diagram, QPointF at, const QString &medium = {})
	{
		auto *collection = diagram->project()->embeddedElementCollection();
		const QString name = medium.isEmpty() ? QStringLiteral("wire") : medium;
		const QString file = QStringLiteral("import/%1.elmt").arg(name);
		if (!collection->exist(file)) {
			const QString attr = medium.isEmpty()
					? QString() : QStringLiteral(" medium=\"%1\"").arg(medium);
			QDomDocument definition;
			definition.setContent(QString(
				R"(<definition type="element" link_type="simple" uuid="%1" width="20" height="40" hotspot_x="10" hotspot_y="20">
<names><name lang="en">Test symbol</name></names>
<description><line x1="0" y1="-10" x2="0" y2="10"/>
<terminal x="0" y="-20" orientation="n" uuid="%2"%4/><terminal x="0" y="20" orientation="s" uuid="%3"%4/>
</description></definition>)").arg(QUuid::createUuid().toString(), QUuid::createUuid().toString(),
								   QUuid::createUuid().toString(), attr));
			if (!collection->addElementDefinition(QStringLiteral("import"), name,
												  definition.documentElement()))
				return nullptr;
		}
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(
			ElementsLocation(QStringLiteral("embed://") + file, diagram->project()),
			nullptr, &state);
		if (state || !element) return nullptr;
		diagram->addItem(element);
		element->setPos(at);
		return element;
	}

		//A conductor from @p from to @p to, with the text @p text; its medium
		//is the one of @p from
	static Conductor *draw(Terminal *from, Terminal *to, const QString &text)
	{
		auto *c = new Conductor(from, to);
		from->diagram()->addItem(c);
		auto p = c->properties();
		p.text = text;
		c->setProperties(p);
		return c;
	}

		//A conductor of its own at @p at, between two symbols of @p medium
	static Conductor *lone(Diagram *d, QPointF at, const QString &text,
						   const QString &medium = {})
	{
		Element *a = symbol(d, at, medium);
		Element *b = symbol(d, at + QPointF(0, 60), medium);
		if (!a || !b) return nullptr;
		return draw(a->terminals().last(), b->terminals().first(), text);
	}

	static void numbering(QETProject &project)
	{
		NumerotationContext rule;
		rule.addValue(QStringLiteral("string"), QStringLiteral("W"));
		rule.addValue(QStringLiteral("unit"), 1, 1, 1);
		project.addConductorAutoNum(QStringLiteral("wires"), rule);
	}

	static QString text(Conductor *c) {return c->properties().text;}

		//The labels of the wire number groups the duplicate check finds
	static QStringList signalGroups(QETProject &project)
	{
		QStringList labels;
		for (const auto &group : DuplicateLabels::find(&project))
			if (group.kind == DuplicateLabels::Kind::Signal)
				labels << group.label;
		labels.sort();
		return labels;
	}

	static int count(QETProject &project, const QString &sql)
	{
		project.dataBase()->updateDB();
		QString error;
		QSqlQuery query = project.dataBase()->newQuery(sql, &error);
		if (!error.isEmpty() || !query.next()) return -1;
		return query.value(0).toInt();
	}

private slots:
	void renumberGivesNoPipeAWireNumber()
	{
		QETProject project;
		numbering(project);
		Diagram *chosen = project.addNewDiagram();
		Diagram *other = project.addNewDiagram();
		Conductor *pipe = lone(chosen, {100, 100}, QStringLiteral("L100"), QStringLiteral("pipe"));
		Conductor *wire = lone(chosen, {300, 100}, QStringLiteral("x"));
			//On the folio not chosen, a pipe whose text looks like a wire
			//number: it reserves nothing
		Conductor *far_pipe = lone(other, {100, 100}, QStringLiteral("W1"), QStringLiteral("pipe"));
		QVERIFY(pipe && wire && far_pipe);
		QCOMPARE(pipe->medium(), TerminalData::Pipe);
		QCOMPARE(wire->medium(), TerminalData::Electrical);

		auto plan = RenumberConductorsCommand::plan(&project, QStringLiteral("wires"), {chosen});
		QCOMPARE(plan.potentials, 1);
		for (const auto &change : std::as_const(plan.changes))
			QCOMPARE(change.wire.data(), wire);
		project.undoStack()->push(new RenumberConductorsCommand(
				&project, QStringLiteral("wires"), std::move(plan), QStringLiteral("renumber")));
		QCOMPARE(text(wire), QStringLiteral("W1"));
		QCOMPARE(text(pipe), QStringLiteral("L100"));
		QCOMPARE(text(far_pipe), QStringLiteral("W1"));
	}

	void theDuplicateCheckLeavesPipesAlone()
	{
		QETProject project;
		Diagram *d = project.addNewDiagram();
			//Two pipes of one line: the same number, no duplicate
		QVERIFY(lone(d, {100, 100}, QStringLiteral("L100"), QStringLiteral("pipe")));
		QVERIFY(lone(d, {300, 100}, QStringLiteral("L100"), QStringLiteral("pipe")));
			//Two wires apart with one number: still a duplicate
		QVERIFY(lone(d, {500, 100}, QStringLiteral("W7")));
		QVERIFY(lone(d, {700, 100}, QStringLiteral("W7")));
		QCOMPARE(signalGroups(project), QStringList{QStringLiteral("W7")});
	}

		//Two wires numbered alike, joined only through a pipe drawn to one of
		//them by hand: two potentials, so a duplicate
	void theDuplicateCheckNeverGoesThroughAPipe()
	{
		QETProject project;
		Diagram *d = project.addNewDiagram();
		Element *e1 = symbol(d, {100, 100});
		Element *e2 = symbol(d, {100, 200});
		Element *e3 = symbol(d, {300, 100});
		Element *p = symbol(d, {300, 300}, QStringLiteral("pipe"));
		QVERIFY(e1 && e2 && e3 && p);
		Conductor *w1 = draw(e1->terminals().last(), e2->terminals().first(), QStringLiteral("W8"));
		Conductor *pipe = draw(p->terminals().first(), e1->terminals().last(), QString());
		Conductor *w2 = draw(e3->terminals().last(), p->terminals().first(), QStringLiteral("W8"));
		QCOMPARE(w1->medium(), TerminalData::Electrical);
		QCOMPARE(pipe->medium(), TerminalData::Pipe);
		QCOMPARE(w2->medium(), TerminalData::Electrical);
		QCOMPARE(signalGroups(project), QStringList{QStringLiteral("W8")});
	}

	void potentialViewListsNoPipe()
	{
		QETProject project;
		Diagram *d = project.addNewDiagram();
		Conductor *pipe = lone(d, {100, 100}, QStringLiteral("L100"), QStringLiteral("pipe"));
		Conductor *wire = lone(d, {300, 100}, QStringLiteral("W1"));
		QVERIFY(pipe && wire);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM potential_view")), 1);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM potential_member WHERE wire_uuid = '%1'")
					   .arg(pipe->uuid().toString())), 0);
		QCOMPARE(count(project, QStringLiteral("SELECT COUNT(*) FROM potential_member WHERE wire_uuid = '%1'")
					   .arg(wire->uuid().toString())), 1);
	}
};

QET_APP_TEST_MAIN(tst_mediumguards)
#include "tst_mediumguards.moc"

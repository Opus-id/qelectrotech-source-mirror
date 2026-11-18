// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "pidstatus.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "selectioncategories.h"
#include "signalproperties.h"
#include "ui/signalpropertieswidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QSet>
#include <QUndoStack>

// The status of works of a signal line (step 6-5): as a pipe, a signal line
// is to be built, existing or to be demolished. It is kept on the line
// (pid_status, written only when set), chosen on its page or with the
// others (PidStatus::change()), drawn in the colour the project gives the
// status, before the colour of the signal lines; the signal lines of a
// selection are sorted by type of signal and by status of works. A wire has
// no status.
class tst_signalstatus : public QObject
{
	Q_OBJECT

	using Node = SelectionCategories::Node;

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
					QStringLiteral("import/status"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/status/") + file, &m_project);
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

		// A conductor between two symbols of a row of their own
	Conductor *draw(const ElementsLocation &location)
	{
		m_next_y += 100;
		Element *a = place(location, QPointF(0, m_next_y));
		Element *b = place(location, QPointF(200, m_next_y));
		if (!a || !b) return nullptr;
		auto *conductor = new Conductor(a->terminals().at(1), b->terminals().at(0));
		m_folio->addItem(conductor);
		return conductor;
	}

	static QString statusOf(const Conductor *line)
	{
		return line->properties().m_signal.status;
	}

	static void set(Conductor *line, SignalProperties::SignalType type, const QString &status)
	{
		ConductorProperties p = line->properties();
		p.m_signal.signal_type = type;
		p.m_signal.status = status;
		line->setProperties(p);
	}

		// What a user does in the list of the statuses: pick @p status
	static void pick(QComboBox *list, const QString &status)
	{
		int index = -1;
		for (int i = 0 ; i < list->count() ; ++i) {
			if (list->itemData(i).toString() == status) index = i;
		}
		QVERIFY(index >= 0);
		list->setCurrentIndex(index);
		emit list->activated(index);
	}

	static const Node *find(const QList<Node> &nodes, const QString &label)
	{
		for (const Node &node : nodes) {
			if (node.label == label) return &node;
			if (const Node *found = find(node.children, label)) return found;
		}
		return nullptr;
	}

	static QStringList labels(const QList<Node> &nodes)
	{
		QStringList list;
		for (const Node &node : nodes) list << node.label;
		return list;
	}

	static QSet<QGraphicsItem *> asSet(const QList<QGraphicsItem *> &items)
	{
		return QSet<QGraphicsItem *>(items.begin(), items.end());
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("status"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
							   definition(a, "b1111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "b2222222-2222-2222-2222-222222222222", "pipe"));
			m_signals = add("signals.elmt",
							definition(c, "b3333333-3333-3333-3333-333333333333", "signal"));
			QVERIFY(m_electrical.exist() && m_nozzles.exist() && m_signals.exist());
			m_folio = m_project.addNewDiagram();
		}

		void cleanup()
		{
			m_project.setPidCodification(PidCodification());
		}

			// With the pipes, in one undo step; a wire is left alone
		void aSignalLineTakesAStatusAsAPipeDoes()
		{
			Conductor *line = draw(m_signals);
			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && pipe && wire);
			QVERIFY(PidStatus::applies(line) && PidStatus::applies(pipe));
			QVERIFY(!PidStatus::applies(wire));
			const ConductorProperties wire_before = wire->properties();

			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.index();
			QUndoCommand *undo = PidStatus::change({line, pipe, wire}, QStringLiteral("demolished"));
			QVERIFY(undo);
			stack.push(undo);
			QCOMPARE(stack.index(), steps + 1);
			QCOMPARE(statusOf(line), QStringLiteral("demolished"));
			QCOMPARE(pipe->properties().m_pipe.status, QStringLiteral("demolished"));
				// each keeps it in its own data, not in the other's
			QVERIFY(line->properties().m_pipe.status.isEmpty());
			QVERIFY(pipe->properties().m_signal.status.isEmpty());
			QVERIFY(wire->properties() == wire_before);

				// nothing to change, no step
			QVERIFY(!PidStatus::change({line, pipe}, QStringLiteral("demolished")));

			stack.undo();
			QVERIFY(statusOf(line).isEmpty());
			QVERIFY(pipe->properties().m_pipe.status.isEmpty());
		}

			// Written on the conductor as pid_status, only when set, and for
			// a signal line only; read back
		void theStatusIsSavedOnTheLine()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QVERIFY(!line->toXml(doc, ids, {}).hasAttribute(QStringLiteral("pid_status")));

			set(line, SignalProperties::Undefined, QStringLiteral("existing"));
			const QDomElement saved = line->toXml(doc, ids, {});
			QCOMPARE(saved.attribute(QStringLiteral("pid_status")), QStringLiteral("existing"));
			QVERIFY(!saved.hasAttribute(QStringLiteral("pid_signal_type")));

			QDomElement copy = saved;
			ConductorProperties read;
			read.fromXml(copy);
			QCOMPARE(read.m_signal.status, QStringLiteral("existing"));
			QVERIFY(read.m_pipe.status.isEmpty());

				// a wire never writes it, whatever its properties hold
			ConductorProperties wire;
			wire.m_signal.status = QStringLiteral("existing");
			QDomElement plain = doc.createElement(QStringLiteral("conductor"));
			wire.toXml(plain);
			QVERIFY(!plain.hasAttribute(QStringLiteral("pid_status")));
			QVERIFY(!plain.hasAttribute(QStringLiteral("medium")));
		}

			// The colour of the status first, then that of the signal lines,
			// then its own; a pipe and a wire are not concerned
		void theColourOfTheStatusComesFirst()
		{
			Conductor *line = draw(m_signals);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && wire);
			const QColor line_before = line->drawnColor();
			const QColor wire_before = wire->drawnColor();

			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;signal;#0000ff\n"
				"status_color;existing;#808080\n"
				"status_color;demolished;#ff0000\n")));
			QCOMPARE(line->drawnColor(), QColor(0, 0, 255));
			set(line, SignalProperties::Pneumatic, QStringLiteral("demolished"));
			QCOMPARE(line->drawnColor(), QColor(255, 0, 0));
			set(line, SignalProperties::Pneumatic, QStringLiteral("existing"));
			QCOMPARE(line->drawnColor(), QColor(0x80, 0x80, 0x80));
			QCOMPARE(wire->drawnColor(), wire_before);

				// a status without a colour falls back on what comes next
			m_project.setPidCodification(PidCodification::fromCsv(QStringLiteral(
				"object_color;signal;#0000ff\n")));
			QCOMPARE(line->drawnColor(), QColor(0, 0, 255));
			m_project.setPidCodification(PidCodification());
			QCOMPARE(line->drawnColor(), line_before);
		}

			// The page of a signal line: the three statuses, one undo step,
			// the field follows; statuses which differ show none
		void thePageOfASignalLineHasTheStatus()
		{
			Conductor *a = draw(m_signals);
			Conductor *b = draw(m_signals);
			QVERIFY(a && b);
			SignalPropertiesWidget page({a});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("status"));
			QVERIFY(list);
			const auto statuses = PidStatus::statuses();
			QCOMPARE(list->count(), statuses.size());
			for (int i = 0 ; i < statuses.size() ; ++i) {
				QCOMPARE(list->itemData(i).toString(), statuses.at(i).first);
				QCOMPARE(list->itemText(i), statuses.at(i).second);
			}
				// a new signal line is to be built
			QCOMPARE(list->currentIndex(), 0);

			QUndoStack &stack = m_folio->undoStack();
				// (the index: an earlier test left a step undone)
			const int steps = stack.index();
			pick(list, QStringLiteral("existing"));
			QCOMPARE(statusOf(a), QStringLiteral("existing"));
			QCOMPARE(stack.index(), steps + 1);
			pick(list, QStringLiteral("existing"));
			QCOMPARE(stack.index(), steps + 1);
			stack.undo();
			QVERIFY(statusOf(a).isEmpty());
			QCOMPARE(list->currentIndex(), 0);
			stack.redo();
			QCOMPARE(list->currentData().toString(), QStringLiteral("existing"));
				// the type of signal is left as it is
			QCOMPARE(int(a->properties().m_signal.signal_type), int(SignalProperties::Undefined));

			SignalPropertiesWidget both({a, b});
			auto *shared = both.findChild<QComboBox *>(QStringLiteral("status"));
			QVERIFY(shared);
			QCOMPARE(shared->currentIndex(), -1);
			pick(shared, QStringLiteral("demolished"));
			QCOMPARE(stack.index(), steps + 2);
			QCOMPARE(statusOf(a), QStringLiteral("demolished"));
			QCOMPARE(statusOf(b), QStringLiteral("demolished"));
			QCOMPARE(shared->currentData().toString(), QStringLiteral("demolished"));
		}

			// Select by criteria: a leaf while no line holds a type or a
			// status, then a branch by type of signal and one by status
		void theSignalLinesAreSortedByTypeAndByStatus()
		{
			Conductor *a = draw(m_signals);
			Conductor *b = draw(m_signals);
			Conductor *c = draw(m_signals);
			Conductor *pipe = draw(m_nozzles);
			QVERIFY(a && b && c && pipe);
			const QList<QGraphicsItem *> items {a, b, c, pipe};
			{
				const SelectionCategories plain(items, PidCodification());
				const Node *node = find(plain.tree(), QStringLiteral("Signal lines"));
				QVERIFY(node);
				QVERIFY(node->children.isEmpty());
				QVERIFY(!node->key.isEmpty());
				QCOMPARE(node->count, 3);
				QCOMPARE(asSet(plain.kept({node->key})), asSet({a, b, c}));
				QCOMPARE(plain.kept(plain.leaves()).size(), 4);
			}

			set(b, SignalProperties::Pneumatic, QString());
			set(c, SignalProperties::Pneumatic, QStringLiteral("existing"));
			const SelectionCategories sorted(items, PidCodification());
			const Node *node = find(sorted.tree(), QStringLiteral("Signal lines"));
			QVERIFY(node);
			QCOMPARE(node->count, 3);
			QCOMPARE(labels(node->children), QStringList({QStringLiteral("By type of signal"),
														  QStringLiteral("By status of works")}));
			const Node &by_type = node->children.at(0);
			QCOMPARE(labels(by_type.children), QStringList({QStringLiteral("Undefined"),
															QStringLiteral("Pneumatic")}));
			QCOMPARE(by_type.children.at(0).count, 1);
			QCOMPARE(by_type.children.at(1).count, 2);
			const Node &by_status = node->children.at(1);
			QCOMPARE(labels(by_status.children), QStringList({QStringLiteral("To be built"),
															  QStringLiteral("Existing, kept")}));
			QCOMPARE(by_status.children.at(0).count, 2);
			QCOMPARE(by_status.children.at(1).count, 1);

			const QString pneumatic = by_type.children.at(1).key;
			const QString existing = by_status.children.at(1).key;
			QCOMPARE(SelectionCategories::categoryOf(pneumatic), QStringLiteral("signal"));
			QCOMPARE(SelectionCategories::branchOf(pneumatic), QStringLiteral("signal/type"));
			QCOMPARE(SelectionCategories::branchOf(existing), QStringLiteral("signal/status"));
				// the pneumatic ones, whatever their status; never the pipe
			QCOMPARE(asSet(sorted.kept({pneumatic})), asSet({b, c}));
			QCOMPARE(asSet(sorted.kept({pneumatic, existing})), asSet({c}));
			QCOMPARE(asSet(sorted.kept({by_type.children.at(0).key, existing})),
					 QSet<QGraphicsItem *>());
			QCOMPARE(sorted.kept(sorted.leaves()).size(), 4);
		}
};

QET_APP_TEST_MAIN(tst_signalstatus)
#include "tst_signalstatus.moc"

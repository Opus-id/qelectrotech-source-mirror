// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "factory/propertieseditorfactory.h"
#include "pidsection.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "signalproperties.h"
#include "ui/pipepropertieswidget.h"
#include "ui/signalpropertieswidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QFormLayout>
#include <QLabel>
#include <QUndoStack>

// The data of a signal line on a conductor, and its page in the properties
// panel. Only a conductor of the signal medium keeps a type of signal, and
// writes it, when set, as pid_signal_type; a wire and a pipe are saved as
// before. The page is offered for signal lines only, whatever the option of
// the panel of the wires says; a change is one undo step and reaches the
// signal lines selected, no other.
class tst_signalpanel : public QObject
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
					QStringLiteral("import/signal"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/signal/") + file, &m_project);
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

		// What a user does in the list of the types: pick @p type
	static void pick(QComboBox *list, SignalProperties::SignalType type)
	{
		const int index = list->findData(int(type));
		QVERIFY(index >= 0);
		list->setCurrentIndex(index);
		emit list->activated(index);
	}

	static SignalProperties::SignalType typeOf(const Conductor *line)
	{
		return line->properties().m_signal.signal_type;
	}

		// The properties of @p medium, with the type of signal @p type
	static ConductorProperties properties(TerminalData::Medium medium,
										  SignalProperties::SignalType type)
	{
		ConductorProperties p;
		p.m_medium = medium;
		p.m_signal.signal_type = type;
		return p;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("signal"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
							   definition(a, "91111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "92222222-2222-2222-2222-222222222222", "pipe"));
			m_signals = add("signals.elmt",
							definition(c, "93333333-3333-3333-3333-333333333333", "signal"));
			QVERIFY(m_electrical.exist() && m_nozzles.exist() && m_signals.exist());
			m_folio = m_project.addNewDiagram();
		}

			// ---- the properties ----
			// Written for a signal line only, and only when set: a wire and a
			// pipe never have the attribute, whatever their properties hold
		void theTypeIsWrittenForASignalLineOnly()
		{
			QDomDocument doc;
			QDomElement line = doc.createElement(QStringLiteral("conductor"));
			properties(TerminalData::Signal, SignalProperties::Pneumatic).toXml(line);
			QCOMPARE(line.attribute(QStringLiteral("medium")), QStringLiteral("signal"));
			QCOMPARE(line.attribute(QStringLiteral("pid_signal_type")), QStringLiteral("pneumatic"));

			QDomElement plain = doc.createElement(QStringLiteral("conductor"));
			properties(TerminalData::Signal, SignalProperties::Undefined).toXml(plain);
			QCOMPARE(plain.attribute(QStringLiteral("medium")), QStringLiteral("signal"));
			QVERIFY(!plain.hasAttribute(QStringLiteral("pid_signal_type")));

			for (TerminalData::Medium medium : {TerminalData::Electrical, TerminalData::Pipe,
												TerminalData::Drive}) {
				QDomElement other = doc.createElement(QStringLiteral("conductor"));
				properties(medium, SignalProperties::Pneumatic).toXml(other);
				QVERIFY(!other.hasAttribute(QStringLiteral("pid_signal_type")));
			}
		}

			// A wire is saved exactly as before: the same attributes, in the
			// same order, with the same values
		void aWireIsSavedAsBefore()
		{
			ConductorProperties wire;
			ConductorProperties with_type = wire;
			with_type.m_signal.signal_type = SignalProperties::Hydraulic;
			QVERIFY(with_type.attributes() == wire.attributes());
			for (const auto &attribute : wire.attributes()) {
				QVERIFY2(!attribute.first.startsWith(QLatin1String("pid_")),
						 qPrintable(attribute.first));
			}
		}

		void theTypeIsReadForASignalLineOnly()
		{
			QDomDocument doc;
			QDomElement line = doc.createElement(QStringLiteral("conductor"));
			properties(TerminalData::Signal, SignalProperties::Capillary).toXml(line);
			ConductorProperties in;
			in.fromXml(line);
			QCOMPARE(in.m_medium, TerminalData::Signal);
			QCOMPARE(int(in.m_signal.signal_type), int(SignalProperties::Capillary));

				// the same attribute on a pipe or on a wire means nothing, and
				// what was read before is not kept
			line.setAttribute(QStringLiteral("medium"), QStringLiteral("pipe"));
			in.fromXml(line);
			QCOMPARE(int(in.m_signal.signal_type), int(SignalProperties::Undefined));
			in.m_signal.signal_type = SignalProperties::Capillary;
			line.removeAttribute(QStringLiteral("medium"));
			in.fromXml(line);
			QCOMPARE(in.m_medium, TerminalData::Electrical);
			QCOMPARE(int(in.m_signal.signal_type), int(SignalProperties::Undefined));
		}

		void theTypeMakesTwoSetsOfPropertiesDiffer()
		{
			QVERIFY(properties(TerminalData::Signal, SignalProperties::Pneumatic)
					!= properties(TerminalData::Signal, SignalProperties::Electrical));
			QVERIFY(properties(TerminalData::Signal, SignalProperties::Pneumatic)
					== properties(TerminalData::Signal, SignalProperties::Pneumatic));
		}

			// ---- the conductor ----
			// A new signal line has no type yet; only a signal line keeps one
		void onlyASignalLineKeepsAType()
		{
			Conductor *line = draw(m_signals);
			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && pipe && wire);
			QCOMPARE(line->medium(), TerminalData::Signal);
			QCOMPARE(int(typeOf(line)), int(SignalProperties::Undefined));

			for (Conductor *conductor : {line, pipe, wire}) {
				ConductorProperties p = conductor->properties();
				p.m_signal.signal_type = SignalProperties::Electrical;
				conductor->setProperties(p);
			}
			QCOMPARE(int(typeOf(line)), int(SignalProperties::Electrical));
			QCOMPARE(int(typeOf(pipe)), int(SignalProperties::Undefined));
			QCOMPARE(int(typeOf(wire)), int(SignalProperties::Undefined));

				// and a signal line never takes the data of a pipe
			ConductorProperties p = line->properties();
			p.m_pipe.fluid = QStringLiteral("W01");
			line->setProperties(p);
			QVERIFY(line->properties().m_pipe.isDefault());
			QCOMPARE(int(typeOf(line)), int(SignalProperties::Electrical));
		}

			// Saved and read back with its type; a signal line saved without
			// one has none
		void theFileSaysTheType()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			Terminal *from = line->terminal1;
			Terminal *to = line->terminal2;
			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QDomElement plain = line->toXml(doc, ids, {});
			QCOMPARE(plain.attribute(QStringLiteral("medium")), QStringLiteral("signal"));
			QVERIFY(!plain.hasAttribute(QStringLiteral("pid_signal_type")));

			ConductorProperties p = line->properties();
			p.m_signal.signal_type = SignalProperties::DataLink;
			line->setProperties(p);
			QDomElement saved = line->toXml(doc, ids, {});
			QCOMPARE(saved.attribute(QStringLiteral("pid_signal_type")), QStringLiteral("data_link"));

				// read again onto a conductor between the same terminals
			m_folio->removeItem(line);
			delete line;
			auto *again = new Conductor(from, to);
			again->fromXml(saved);
			QCOMPARE(again->medium(), TerminalData::Signal);
			QCOMPARE(int(typeOf(again)), int(SignalProperties::DataLink));
			again->fromXml(plain);
			QCOMPARE(int(typeOf(again)), int(SignalProperties::Undefined));
			delete again;
		}

			// ---- the page ----
		void onlySignalLinesGetThePageOfASignalLine()
		{
			Conductor *line = draw(m_signals);
			Conductor *other = draw(m_signals);
			Conductor *pipe = draw(m_nozzles);
			Conductor *wire = draw(m_electrical);
			QVERIFY(line && other && pipe && wire);

			PropertiesEditorWidget *editor = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{line}, nullptr, nullptr);
			auto *page = qobject_cast<SignalPropertiesWidget *>(editor);
			QVERIFY(page);
			QCOMPARE(page->lines(), QList<Conductor *>{line});

				// the same editor goes on with another selection
			QCOMPARE(PropertiesEditorFactory::propertiesEditor(QList<QGraphicsItem *>{line, other}, page, nullptr),
					 page);
			QCOMPARE(page->lines().size(), 2);
			delete page;

				// a pipe keeps its own page, a wire what it had
			PropertiesEditorWidget *for_pipe = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{pipe}, nullptr, nullptr);
			QVERIFY(qobject_cast<PipePropertiesWidget *>(for_pipe));
			delete for_pipe;
			PropertiesEditorWidget *for_wire = PropertiesEditorFactory::propertiesEditor(
						QList<QGraphicsItem *>{wire}, nullptr, nullptr);
			QVERIFY(!qobject_cast<SignalPropertiesWidget *>(for_wire));
			delete for_wire;

				// signal lines, pipes and wires are not mixed
			QVERIFY(!PropertiesEditorFactory::propertiesEditor(QList<QGraphicsItem *>{line, wire}, nullptr, nullptr));
			QVERIFY(!PropertiesEditorFactory::propertiesEditor(QList<QGraphicsItem *>{line, pipe}, nullptr, nullptr));
		}

			// One section with its title, and the seven types in their order
		void thePageHasOneSectionAndEveryType()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			SignalPropertiesWidget page({line});
			auto *form = qobject_cast<QFormLayout *>(page.layout());
			QVERIFY(form);
			int titles = 0;
			for (int row = 0 ; row < form->rowCount() ; ++row) {
				QLayoutItem *item = form->itemAt(row, QFormLayout::SpanningRole);
				if (item && item->widget()
					&& item->widget()->findChild<QLabel *>(PidSection::titleName())) {
					++titles;
				}
			}
			QCOMPARE(titles, 1);

			auto *list = page.findChild<QComboBox *>(QStringLiteral("signal_type"));
			QVERIFY(list);
			const QList<SignalProperties::SignalType> types = SignalProperties::types();
			QCOMPARE(list->count(), types.size());
			for (int i = 0 ; i < types.size() ; ++i) {
				QCOMPARE(list->itemData(i).toInt(), int(types.at(i)));
				QCOMPARE(list->itemText(i), SignalProperties::typeName(types.at(i)));
			}
				// a new signal line shows the undefined type
			QCOMPARE(list->currentData().toInt(), int(SignalProperties::Undefined));
		}

		void aChangeIsOneUndoStepAndTheFieldFollows()
		{
			Conductor *line = draw(m_signals);
			QVERIFY(line);
			SignalPropertiesWidget page({line});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("signal_type"));
			QVERIFY(list);
			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();

			pick(list, SignalProperties::Pneumatic);
			QCOMPARE(int(typeOf(line)), int(SignalProperties::Pneumatic));
			QCOMPARE(line->medium(), TerminalData::Signal);
			QCOMPARE(stack.count(), steps + 1);

				// picking what it already has is no change
			pick(list, SignalProperties::Pneumatic);
			QCOMPARE(stack.count(), steps + 1);

			stack.undo();
			QCOMPARE(int(typeOf(line)), int(SignalProperties::Undefined));
			QCOMPARE(list->currentData().toInt(), int(SignalProperties::Undefined));
			stack.redo();
			QCOMPARE(int(typeOf(line)), int(SignalProperties::Pneumatic));
			QCOMPARE(list->currentData().toInt(), int(SignalProperties::Pneumatic));

				// back to no type: the attribute is no longer written
			pick(list, SignalProperties::Undefined);
			QHash<Terminal *, int> ids;
			QDomDocument doc;
			QVERIFY(!line->toXml(doc, ids, {}).hasAttribute(QStringLiteral("pid_signal_type")));
		}

			// Several signal lines: one step for all of them; types which
			// differ show none, and nothing changes until one is picked
		void aChangeReachesTheSignalLinesSelectedAndNoOther()
		{
			Conductor *a = draw(m_signals);
			Conductor *b = draw(m_signals);
			Conductor *aside = draw(m_signals);
			QVERIFY(a && b && aside);
			ConductorProperties p = b->properties();
			p.m_signal.signal_type = SignalProperties::Hydraulic;
			b->setProperties(p);

			SignalPropertiesWidget page({a, b});
			auto *list = page.findChild<QComboBox *>(QStringLiteral("signal_type"));
			QVERIFY(list);
			QCOMPARE(list->currentIndex(), -1);
			auto *count = page.findChild<QLabel *>(QStringLiteral("count"));
			QVERIFY(count && !count->isHidden());

			QUndoStack &stack = m_folio->undoStack();
			const int steps = stack.count();
			pick(list, SignalProperties::Electrical);
			QCOMPARE(stack.count(), steps + 1);
			QCOMPARE(int(typeOf(a)), int(SignalProperties::Electrical));
			QCOMPARE(int(typeOf(b)), int(SignalProperties::Electrical));
			QCOMPARE(int(typeOf(aside)), int(SignalProperties::Undefined));
			QCOMPARE(list->currentData().toInt(), int(SignalProperties::Electrical));

			stack.undo();
			QCOMPARE(int(typeOf(a)), int(SignalProperties::Undefined));
			QCOMPARE(int(typeOf(b)), int(SignalProperties::Hydraulic));
			QCOMPARE(list->currentIndex(), -1);
		}

		void aSignalLineRemovedFromTheFolioLeavesThePageUsable()
		{
			Conductor *stays = draw(m_signals);
			Conductor *goes = draw(m_signals);
			QVERIFY(stays && goes);
			SignalPropertiesWidget page({stays, goes});
			m_folio->removeItem(goes);
			delete goes;
			QCOMPARE(page.lines(), QList<Conductor *>{stays});
			pick(page.findChild<QComboBox *>(QStringLiteral("signal_type")),
				 SignalProperties::Capillary);
			QCOMPARE(int(typeOf(stays)), int(SignalProperties::Capillary));
		}
};

QET_APP_TEST_MAIN(tst_signalpanel)
#include "tst_signalpanel.moc"

// SPDX-License-Identifier: GPL-2.0-or-later
#include "qetapptest.h"

#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "diagramcommands.h"
#include "diagramcontent.h"
#include "factory/elementfactory.h"
#include "itemdescription.h"
#include "pidcodification.h"
#include "qetdiagrameditor.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "shortcutbarsettings.h"
#include "shownkinds.h"
#include "undocommand/addgraphicsobjectcommand.h"

#include <QAction>
#include <QDomDocument>
#include <QUndoStack>

// The words for the lines of a P&ID (step 6-5): a pipe and a signal line are
// never called conductors, even when the signal is an electrical one. The
// status bar says "Pipe" with the line number and the size, "Signal line"
// with the type of signal; the sentences of the undo captions count the
// pipes and the signal lines apart; the labels which stand for all of them
// say "lines and conductors". A wire is called a conductor as before.
class tst_pidvocabulary : public QObject
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
					QStringLiteral("import/words"), file, definition);
		return ElementsLocation(QStringLiteral("embed://import/words/") + file, &m_project);
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

		// Two symbols of a row of their own: the terminals to join
	QPair<Terminal *, Terminal *> row(const ElementsLocation &location)
	{
		m_next_y += 100;
		Element *a = place(location, QPointF(0, m_next_y));
		Element *b = place(location, QPointF(200, m_next_y));
		if (!a || !b) return {nullptr, nullptr};
		return {a->terminals().at(1), b->terminals().at(0)};
	}

	Conductor *draw(const ElementsLocation &location)
	{
		const auto ends = row(location);
		if (!ends.first) return nullptr;
		auto *conductor = new Conductor(ends.first, ends.second);
		m_folio->addItem(conductor);
		return conductor;
	}

	QString sentenceOf(const QList<Conductor *> &lines)
	{
		m_folio->clearSelection();
		for (Conductor *line : lines) line->setSelected(true);
		const QString sentence = DiagramContent(m_folio).sentence(DiagramContent::All);
		m_folio->clearSelection();
		return sentence;
	}

	private slots:
		void initTestCase()
		{
			m_project.embeddedElementCollection()->createDir(
						QStringLiteral("import"), QStringLiteral("words"), NamesList());
			QDomDocument a, b, c;
			m_electrical = add("electrical.elmt",
							   definition(a, "c1111111-1111-1111-1111-111111111111", ""));
			m_nozzles = add("nozzles.elmt",
							definition(b, "c2222222-2222-2222-2222-222222222222", "pipe"));
			m_signals = add("signals.elmt",
							definition(c, "c3333333-3333-3333-3333-333333333333", "signal"));
			QVERIFY(m_electrical.exist() && m_nozzles.exist() && m_signals.exist());
			m_folio = m_project.addNewDiagram();
		}

			// What the status bar says of the line under the mouse
		void theStatusBarNamesAPipeAndASignalLine()
		{
			Conductor *wire = draw(m_electrical);
			Conductor *pipe = draw(m_nozzles);
			Conductor *line = draw(m_signals);
			QVERIFY(wire && pipe && line);

				// (a new wire has the text "_")
			QCOMPARE(ItemDescription::of(wire),
					 QStringLiteral("Conductor ") + wire->properties().text);
			QCOMPARE(ItemDescription::of(pipe), QStringLiteral("Pipe"));
			QCOMPARE(ItemDescription::of(line), QStringLiteral("Signal line"));
			QCOMPARE(ItemDescription::ofSelection({pipe}), QStringLiteral("Pipe"));

				// the line number, then the size as the project writes it
			ConductorProperties p = pipe->properties();
			p.m_pipe.line_number = QStringLiteral("W01-L");
			pipe->setProperties(p);
			QCOMPARE(ItemDescription::of(pipe), QStringLiteral("Pipe W01-L"));
			p.m_pipe.nominal = PipeDiameter::nominal(100);
			pipe->setProperties(p);
			const PidCodification codes = m_project.pidCodification();
			const PipeProperties data = pipe->properties().m_pipe;
			const QString size = codes.text(
						data.writtenSize(codes.outsideDiameterFor(data.material)), data.material);
			QVERIFY2(size.contains(QStringLiteral("100")), qPrintable(size));
			QCOMPARE(ItemDescription::of(pipe), QStringLiteral("Pipe W01-L · ") + size);
			p.m_pipe.line_number.clear();
			pipe->setProperties(p);
			QCOMPARE(ItemDescription::of(pipe), QStringLiteral("Pipe ") + size);

				// the type of signal, even an electrical one: no conductor
			ConductorProperties s = line->properties();
			s.m_signal.signal_type = SignalProperties::Pneumatic;
			line->setProperties(s);
			QCOMPARE(ItemDescription::of(line), QStringLiteral("Signal line: Pneumatic"));
			s.m_signal.signal_type = SignalProperties::Electrical;
			line->setProperties(s);
			QCOMPARE(ItemDescription::of(line), QStringLiteral("Signal line: Electrical"));
			QVERIFY(!ItemDescription::of(line).contains(QStringLiteral("onductor")));

				// the number of a wire as before
			ConductorProperties w = wire->properties();
			w.text = QStringLiteral("L1");
			wire->setProperties(w);
			QCOMPARE(ItemDescription::of(wire), QStringLiteral("Conductor L1"));

				// their texts
			QVERIFY(wire->textItem() && pipe->textItem() && line->textItem());
			QVERIFY(ItemDescription::of(wire->textItem()).startsWith(QStringLiteral("Conductor text: ")));
			QVERIFY(ItemDescription::of(pipe->textItem()).startsWith(QStringLiteral("Pipe text: ")));
			QVERIFY(ItemDescription::of(line->textItem()).startsWith(QStringLiteral("Signal line text: ")));
		}

			// "2 pipes", "1 signal lines"...: what is deleted, moved, pasted
		void theSentencesCountThePipesAndTheSignalLinesApart()
		{
			Conductor *wire = draw(m_electrical);
			Conductor *pipe = draw(m_nozzles);
			Conductor *other = draw(m_nozzles);
			Conductor *line = draw(m_signals);
			QVERIFY(wire && pipe && other && line);

			QCOMPARE(sentenceOf({wire}), QStringLiteral("1 conductors"));
			QCOMPARE(sentenceOf({pipe, other}), QStringLiteral("2 pipes"));
			QCOMPARE(sentenceOf({line}), QStringLiteral("1 signal lines"));
			const QString all = sentenceOf({wire, pipe, other, line});
			QVERIFY2(all.contains(QStringLiteral("1 conductors")), qPrintable(all));
			QVERIFY2(all.contains(QStringLiteral("2 pipes")), qPrintable(all));
			QVERIFY2(all.contains(QStringLiteral("1 signal lines")), qPrintable(all));

				// the old call counts conductors as before
			QCOMPARE(QET::ElementsAndConductorsSentence(0, 3), QStringLiteral("3 conductors"));
			QCOMPARE(QET::ElementsAndConductorsSentence(0, 0, 0, 0, 0, 0, 0, 0, 2, 0),
					 QStringLiteral("2 pipes"));
		}

			// Add, modify, reset
		void theUndoCaptionsNameAPipeAndASignalLine()
		{
			QUndoStack &stack = m_folio->undoStack();
			const QList<QPair<ElementsLocation, QString>> kinds {
				{m_electrical, QStringLiteral("a conductor")},
				{m_nozzles, QStringLiteral("a pipe")},
				{m_signals, QStringLiteral("a signal line")}};
			for (const auto &kind : kinds) {
				const auto ends = row(kind.first);
				QVERIFY(ends.first && ends.second);
				stack.push(new AddGraphicsObjectCommand(new Conductor(ends.first, ends.second),
														m_folio, QPointF()));
				QCOMPARE(stack.undoText(), QStringLiteral("Add ") + kind.second);
				QVERIFY(ends.first->isLinkedTo(ends.second));
			}

			Conductor *wire = draw(m_electrical);
			Conductor *pipe = draw(m_nozzles);
			Conductor *line = draw(m_signals);
			QVERIFY(wire && pipe && line);
			const auto modify = [](Conductor *c) {
				const ConductorProfile profile = c->profile(Qt::TopLeftCorner);
				return ChangeConductorCommand(c, profile, profile, Qt::TopLeftCorner).text();
			};
			QCOMPARE(modify(wire), QStringLiteral("modify a conductor"));
			QCOMPARE(modify(pipe), QStringLiteral("modify a pipe"));
			QCOMPARE(modify(line), QStringLiteral("modify a signal line"));

			const auto reset = [](const QList<Conductor *> &lines) {
				QHash<Conductor *, ConductorProfilesGroup> profiles;
				for (Conductor *c : lines) profiles.insert(c, c->profiles());
				return ResetConductorCommand(profiles).text();
			};
			QCOMPARE(reset({wire}), QStringLiteral("Reset 1 conductors"));
			QCOMPARE(reset({pipe}), QStringLiteral("Reset 1 pipes"));
			QCOMPARE(reset({line}), QStringLiteral("Reset 1 signal lines"));
			const QString all = reset({wire, pipe, line});
			QVERIFY2(all.contains(QStringLiteral("1 conductors")) && all.contains(QStringLiteral("1 pipes"))
					 && all.contains(QStringLiteral("1 signal lines")), qPrintable(all));
		}

			// The selection filter, the kinds shown and the context of the
			// shortcut bar stand for wires, pipes and signal lines together
		void theGeneralLabelsSayLinesAndConductors()
		{
			QCOMPARE(ShortcutBarSettings::title(ShortcutBarSettings::Conductor),
					 QStringLiteral("Selected lines and conductors"));

			QETDiagramEditor *editor = QetAppTest::editor();
			QVERIFY(editor);
			bool filter = false, shown = false;
			const QList<QAction *> actions = editor->findChildren<QAction *>();
			for (const QAction *action : actions) {
				if (action->text() == QStringLiteral("Lines and conductors")
					&& action->data().toInt() == int(Diagram::SelectionFilter::Conductors)) {
					filter = true;
				}
				if (action->text() == QStringLiteral("Texts of lines and conductors")
					&& action->data().toInt() == int(ShownKinds::WireNumbers)) {
					shown = true;
				}
			}
			QVERIFY(filter);
			QVERIFY(shown);
		}
};

QET_APP_TEST_MAIN(tst_pidvocabulary)
#include "tst_pidvocabulary.moc"

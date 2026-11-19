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

#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "diagramevent/diagrameventaddelement.h"
#include "factory/elementfactory.h"
#include "pidcodification.h"
#include "properties/elementoptions.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "ui/configpage/pidconfigpage.h"
#include "undocommand/changepidcodificationcommand.h"

#include <QDomDocument>
#include <QGraphicsSceneMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableWidget>
#include <QUndoStack>

/**
	The options a P&ID symbol placed new takes from its project
	(PidCodification::option_defaults): by family of symbol and name of
	option, "instrument" / "shape" = "oval" for the oval bubbles of a plant.
	Only a symbol placed new takes them, and keeps them written: changing
	the project later changes no symbol placed. Checked on the data, the
	files (project and CSV), the placing tool, and the page of the project.
*/
class tst_optiondefaults : public QObject
{
	Q_OBJECT

		// A bubble of the family @p family (none: not a P&ID symbol),
		// round by default or oval
	static QString bubble(const QString &family)
	{
		return QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"40\" height=\"40\" hotspot_x=\"20\" hotspot_y=\"20\"%1>"
			"<uuid uuid=\"{7a444444-4444-4444-4444-444444444444}\"/>"
			"<names><name lang=\"en\">Bubble</name></names>"
			"<options>"
			"<option name=\"shape\" default=\"round\"><value name=\"round\"/><value name=\"oval\"/></option>"
			"<option name=\"system\" default=\"discrete\"><value name=\"discrete\"/><value name=\"shared\"/></option>"
			"</options>"
			"<description>"
			"<line x1=\"-10\" y1=\"0\" x2=\"10\" y2=\"0\" end1=\"none\" end2=\"none\" "
			"length1=\"1.5\" length2=\"1.5\" style=\"line-weight:normal\" antialias=\"false\"/>"
			"</description></definition>")
			.arg(family.isEmpty() ? QString() : QStringLiteral(" pid_family=\"%1\"").arg(family));
	}

	static ElementsLocation addBubble(QETProject &project, const QString &name, const QString &family)
	{
		project.embeddedElementCollection()->createDir(
					QStringLiteral("import"), QStringLiteral("bubbles"), NamesList());
		QDomDocument doc;
		doc.setContent(bubble(family));
		project.embeddedElementCollection()->addElementDefinition(
					QStringLiteral("import/bubbles"), name, doc.documentElement());
		return ElementsLocation(QStringLiteral("embed://import/bubbles/") + name, &project);
	}

	static Element *build(const ElementsLocation &location)
	{
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(location, nullptr, &state);
		if (state) {
			delete element;
			return nullptr;
		}
		return element;
	}

		// Drop a copy of the symbol of @p tool at @p at, as a click does;
		// the copy dropped, nullptr when none
	static Element *drop(DiagramEventAddElement &tool, Diagram *folio, const QPointF &at)
	{
		const QList<Element *> before = folio->elements();
		for (QEvent::Type type : {QEvent::GraphicsSceneMouseMove, QEvent::GraphicsSceneMousePress,
					  QEvent::GraphicsSceneMouseRelease}) {
			QGraphicsSceneMouseEvent event(type);
			event.setButton(Qt::LeftButton);
			event.setButtons(type == QEvent::GraphicsSceneMousePress ? Qt::LeftButton : Qt::NoButton);
			event.setScenePos(at);
			if (type == QEvent::GraphicsSceneMouseMove) tool.mouseMoveEvent(&event);
			else if (type == QEvent::GraphicsSceneMousePress) tool.mousePressEvent(&event);
			else tool.mouseReleaseEvent(&event);
		}
		for (Element *element : folio->elements()) {
			if (!before.contains(element) && element != tool.pendingElement()) {
				return element;
			}
		}
		return nullptr;
	}

	static PidCodification ovalBubbles()
	{
		PidCodification codification;
		codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("shape"),
									  QStringLiteral("oval"));
		return codification;
	}

	private slots:
			// ---- the data and the files ----

		void aDefaultIsSetAndRemoved()
		{
			PidCodification codification;
			QVERIFY(codification.isEmpty());
			QVERIFY(codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("shape"),
												  QStringLiteral("oval")));
			QCOMPARE(codification.option_defaults.value(QStringLiteral("instrument"))
					 .value(QStringLiteral("shape")), QStringLiteral("oval"));
			QVERIFY(!codification.isEmpty());
				// a name which show_if could not read back
			QVERIFY(!codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("sha pe"),
												   QStringLiteral("oval")));
			QVERIFY(!codification.setOptionDefault(QString(), QStringLiteral("shape"),
												   QStringLiteral("oval")));
				// no value: none any more, and no family left empty
			QVERIFY(codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("shape"),
												  QString()));
			QVERIFY(codification.option_defaults.isEmpty());
			QVERIFY(codification.isEmpty());
		}

		void theDefaultsAreSavedWithTheProject()
		{
			PidCodification codification = ovalBubbles();
			codification.setOptionDefault(QStringLiteral("valve"), QStringLiteral("command"),
										  QStringLiteral("motor"));
			QDomDocument document;
			QDomElement root = document.createElement(QStringLiteral("project"));
			document.appendChild(root);
			root.appendChild(codification.toXml(document));
			QCOMPARE(root.elementsByTagName(QStringLiteral("option_default")).count(), 2);

			PidCodification back;
			back.fromXml(root);
			QCOMPARE(back.option_defaults, codification.option_defaults);
			QVERIFY(back == codification);
		}

		void aProjectWithoutDefaultsSavesAsBefore()
		{
			QDomDocument document;
			QVERIFY(PidCodification().toXml(document).isNull());
		}

		void theDefaultsGoWithThePresentation()
		{
			const PidCodification codification = ovalBubbles();
			const QString csv = codification.presentationToCsv();
			QVERIFY(csv.contains(QStringLiteral("option_default;instrument;shape;oval")));
			QVERIFY(!codification.codesToCsv().contains(QStringLiteral("option_default")));
			int skipped = -1;
			const PidCodification back = PidCodification::fromCsv(csv, &skipped);
			QCOMPARE(skipped, 0);
			QCOMPARE(back.option_defaults, codification.option_defaults);
		}

		void aFileMayBeWrittenInFrench()
		{
			int skipped = -1;
			const PidCodification read = PidCodification::fromCsv(
						QStringLiteral("option_défaut;instrument;shape;oval\n"
									   "option_defaut;valve;command;motor\n"
									   "option_default;instrument;shape\n"
									   "option_default;instrument;sha pe;oval\n"
									   "option_default;instrument;shape;round\n"), &skipped);
			QCOMPARE(read.option_defaults.value(QStringLiteral("instrument")).value(QStringLiteral("shape")),
					 QStringLiteral("oval"));
			QCOMPARE(read.option_defaults.value(QStringLiteral("valve")).value(QStringLiteral("command")),
					 QStringLiteral("motor"));
				// no value, a name not readable, an option said twice
			QCOMPARE(skipped, 3);
		}

		void aFileOfCodesKeepsTheDefaultsOfTheProject()
		{
			PidCodification codes;
			PidCodification::Entry fluid;
			fluid.code = QStringLiteral("W01");
			codes.fluids << fluid;
			const PidCodification completed = codes.completedWith(ovalBubbles());
			QCOMPARE(completed.option_defaults, ovalBubbles().option_defaults);
		}

			// ---- what a new symbol takes ----

		void aNewSymbolTakesTheDefaultsOfItsFamily()
		{
			QETProject project;
			const ElementsLocation location = addBubble(project, QStringLiteral("bubble.elmt"),
														QStringLiteral("instrument"));
			QScopedPointer<Element> element(build(location));
			QVERIFY(element);
			element->takeOptionDefaults(ovalBubbles());
			ElementOptions::Choices expected;
			expected.insert(QStringLiteral("shape"), QStringLiteral("oval"));
				// written in the element: the system, at its default, is not
			QCOMPARE(element->optionChoices(), expected);
		}

		void aDefaultTheSymbolHasNotIsLeftAside()
		{
			QETProject project;
			const ElementsLocation location = addBubble(project, QStringLiteral("bubble.elmt"),
														QStringLiteral("instrument"));
			QScopedPointer<Element> element(build(location));
			QVERIFY(element);
			PidCodification codification;
				// a value the option has not, an option the symbol has not,
				// another family, the default of the symbol itself
			codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("system"),
										  QStringLiteral("computer"));
			codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("motor"),
										  QStringLiteral("fixed"));
			codification.setOptionDefault(QStringLiteral("equipment"), QStringLiteral("shape"),
										  QStringLiteral("oval"));
			codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("shape"),
										  QStringLiteral("round"));
			element->takeOptionDefaults(codification);
			QVERIFY(element->optionChoices().isEmpty());
		}

		void aSymbolOfNoFamilyTakesNothing()
		{
			QETProject project;
			const ElementsLocation location = addBubble(project, QStringLiteral("plain.elmt"), QString());
			QScopedPointer<Element> element(build(location));
			QVERIFY(element);
			PidCodification codification;
			codification.setOptionDefault(QStringLiteral("instrument"), QStringLiteral("shape"),
										  QStringLiteral("oval"));
			element->takeOptionDefaults(codification);
			QVERIFY(element->optionChoices().isEmpty());
		}

		void theToolPlacesTheDefaultsAndKeepsThem()
		{
			QETProject project;
				// known by the application: the tool places the symbol by
				// the path of the project
			QETApp::registerProject(&project);
			const auto registered = qScopeGuard([&project]() { QETApp::unregisterProject(&project); });
			Diagram *folio = project.addNewDiagram();
			ElementsLocation location = addBubble(project, QStringLiteral("bubble.elmt"),
												  QStringLiteral("instrument"));
			project.setPidCodification(ovalBubbles());

			Element *oval = nullptr;
			{
				DiagramEventAddElement tool(location, folio);
				QVERIFY(tool.pendingElement());
					// the symbol which follows the mouse shows it already
				QCOMPARE(tool.pendingElement()->resolvedOptions().value(QStringLiteral("shape")),
						 QStringLiteral("oval"));
				oval = drop(tool, folio, QPointF(400, 400));
			}
			QVERIFY(oval);
			QCOMPARE(oval->optionChoices().value(QStringLiteral("shape")), QStringLiteral("oval"));

				// the project changed afterwards: the bubble placed stays oval
			project.setPidCodification(PidCodification());
			QCOMPARE(oval->resolvedOptions().value(QStringLiteral("shape")), QStringLiteral("oval"));
		}

		void aCopyLikeAnotherKeepsItsChoices()
		{
			QETProject project;
			QETApp::registerProject(&project);
			const auto registered = qScopeGuard([&project]() { QETApp::unregisterProject(&project); });
			Diagram *folio = project.addNewDiagram();
			ElementsLocation location = addBubble(project, QStringLiteral("bubble.elmt"),
												  QStringLiteral("instrument"));
				// a round bubble, placed before the project said oval
			Element *round = build(location);
			QVERIFY(round);
			folio->addItem(round);
			round->setPos(100, 100);
			project.setPidCodification(ovalBubbles());

			DiagramEventAddElement tool(location, folio);
			QVERIFY(tool.pendingElement());
			tool.setLike(round);
			QCOMPARE(tool.pendingElement()->resolvedOptions().value(QStringLiteral("shape")),
					 QStringLiteral("round"));
			Element *copy = drop(tool, folio, QPointF(600, 600));
			QVERIFY(copy);
			QCOMPARE(copy->resolvedOptions().value(QStringLiteral("shape")), QStringLiteral("round"));
		}

			// ---- the choices of a symbol made the defaults ----

		void theChoicesOfASymbolBecomeTheDefaults()
		{
			QETProject project;
			ElementsLocation location = addBubble(project, QStringLiteral("bubble.elmt"),
												  QStringLiteral("instrument"));
			QScopedPointer<Element> element(build(location));
			QVERIFY(element);
			ElementOptions::Choices choices;
			choices.insert(QStringLiteral("shape"), QStringLiteral("oval"));
			element->setOptionChoices(choices);

			PidCodification codification = project.pidCodification();
			QVERIFY(codification.setOptionDefaults(element->pidFamily(), element->resolvedOptions()));
				// every option of the symbol, the one at its default too, so
				// that another symbol of the family whose default differs
				// takes it as well
			QCOMPARE(codification.option_defaults.value(QStringLiteral("instrument")),
					 element->resolvedOptions());
				// said again: nothing changes
			QVERIFY(!codification.setOptionDefaults(element->pidFamily(), element->resolvedOptions()));

			project.undoStack()->push(new ChangePidCodificationCommand(&project, codification));
			QCOMPARE(project.pidCodification().option_defaults, codification.option_defaults);
			project.undoStack()->undo();
			QVERIFY(project.pidCodification().option_defaults.isEmpty());
		}

			// ---- the page of the project ----

		void thePageShowsAndRemovesTheDefaults()
		{
			QETProject project;
			PidCodification codification = ovalBubbles();
			codification.setOptionDefault(QStringLiteral("valve"), QStringLiteral("command"),
										  QStringLiteral("motor"));
			project.setPidCodification(codification);
			PidConfigPage page(&project);
			auto *table = page.findChild<QTableWidget *>(QStringLiteral("option_default_table"));
			QVERIFY(table);
			QCOMPARE(table->rowCount(), 2);

				// a line added by hand, then removed
			page.addOptionDefault(QStringLiteral("equipment"), QStringLiteral("motor"),
								  QStringLiteral("fixed"));
			QCOMPARE(table->rowCount(), 3);
			table->clearSelection();
			for (int row = 0 ; row < table->rowCount() ; ++row) {
				if (table->item(row, 1)->text() == QLatin1String("command")) {
					table->selectRow(row);
				}
			}
			page.removeSelectedOptionDefaults();
			QCOMPARE(table->rowCount(), 2);

			page.applyProjectConf();
			QVERIFY(!project.pidCodification().option_defaults.contains(QStringLiteral("valve")));
			QCOMPARE(project.pidCodification().option_defaults.value(QStringLiteral("equipment"))
					 .value(QStringLiteral("motor")), QStringLiteral("fixed"));
			QCOMPARE(project.pidCodification().option_defaults.value(QStringLiteral("instrument"))
					 .value(QStringLiteral("shape")), QStringLiteral("oval"));
		}
};

QET_APP_TEST_MAIN(tst_optiondefaults)
#include "tst_optiondefaults.moc"

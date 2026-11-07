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
#include "pidtag.h"

#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "diagramcontext.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/elementtextitemgroup.h"
#include "qetproject.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

	/// A tag held in a project; element is null on a folio not built
struct Held
{
	const Element *element = nullptr;
	QString unit;
	QString id;
	QString train;
	QString order;
};

	/// A number as its value ("05" is "5"), any other text trimmed
QString normalized(const QString &text)
{
	bool number = false;
	const int value = text.trimmed().toInt(&number);
	return number ? QString::number(value) : text.trimmed();
}

	/// What numbers a tag: its unit, identifier and train; no train is
	/// train 0, as a tag numbered before the train was required has it
QString keyOf(const QString &unit, const QString &id, const QString &train)
{
	const QString train_number = normalized(train);
	return unit.trimmed() + QChar(0x1f) + id.trimmed() + QChar(0x1f)
			+ (train_number.isEmpty() ? QStringLiteral("0") : train_number);
}

	/// Every tag of @p project, folios not built included
QList<Held> heldTags(QETProject *project)
{
	QList<Held> list;
	if (!project) {
		return list;
	}
	QList<QUuid> unbuilt;
		//folios(), not diagrams(), which would build them all
	const QList<Diagram *> folios = project->folios();
	for (Diagram *folio : folios)
	{
		if (!folio->isLoaded()) {
			unbuilt << folio->uuid();
			continue;
		}
		const QList<QGraphicsItem *> items = folio->items();
		for (QGraphicsItem *item : items)
		{
			const auto *element = qgraphicsitem_cast<Element *>(item);
			if (!element) continue;
			const DiagramContext &information = element->elementInformations();
			if (!PidTag::hasFields(information)) continue;
			list << Held{element,
						 information.value(PidTag::unitKey()).toString(),
						 element->getPrefix(),
						 information.value(PidTag::trainKey()).toString(),
						 information.value(PidTag::orderKey()).toString()};
		}
	}
	if (unbuilt.isEmpty()) {
		return list;
	}

		//A folio not built: its symbols as the database has them, the
		//identifier being the prefix the placed symbol was saved with
	projectDataBase *base = project->dataBase();
	if (!base) {
		return list;
	}
	const QList<projectDataBase::FolioSymbol> symbols = base->symbolsOnFolios(unbuilt);
	for (const projectDataBase::FolioSymbol &symbol : symbols)
	{
		const DiagramContext information = base->elementInformation(symbol.uuid);
		if (!PidTag::hasFields(information)) continue;
		QString id;
		const QList<projectDataBase::ProjectSetting> values = base->elementValueRows(symbol.uuid);
		for (const projectDataBase::ProjectSetting &value : values) {
			if (value.name == QLatin1String("prefix")) id = value.value;
		}
		list << Held{nullptr,
					 information.value(PidTag::unitKey()).toString(),
					 id,
					 information.value(PidTag::trainKey()).toString(),
					 information.value(PidTag::orderKey()).toString()};
	}
	return list;
}

	/// Give @p element @p information, its texts staying where they are
void setInformation(Element *element, const DiagramContext &information)
{
	for (DynamicElementTextItem *text : element->dynamicTextItems())
		text->m_block_alignment = true;
	for (ElementTextItemGroup *group : element->textGroups())
		group->blockAlignmentUpdate(true);
	element->setElementInformations(information);
	for (DynamicElementTextItem *text : element->dynamicTextItems())
		text->m_block_alignment = false;
	for (ElementTextItemGroup *group : element->textGroups())
		group->blockAlignmentUpdate(false);
}

}

QString PidTag::unitKey()  { return QStringLiteral("pid_unit"); }
QString PidTag::trainKey() { return QStringLiteral("pid_train"); }
QString PidTag::orderKey() { return QStringLiteral("pid_order"); }

bool PidTag::showsTrain(const QString &model)
{
	static const QRegularExpression train(QStringLiteral("\\{train(?::\\d{1,2})?\\}"));
	return model.contains(train);
}

bool PidTag::hasFields(const DiagramContext &information)
{
	for (const QString &key : {unitKey(), trainKey(), orderKey()}) {
		if (!information.value(key).toString().trimmed().isEmpty()) {
			return true;
		}
	}
	return false;
}

QString PidTag::label(const QString &model,
					  const QString &unit,
					  const QString &id,
					  const QString &train,
					  const QString &order)
{
	static const QRegularExpression field(QStringLiteral("\\{(unit|id|train|order)(?::(\\d{1,2}))?\\}"));
	static const QRegularExpression spaces(QStringLiteral(" {2,}"));

	QString result;
	int from = 0;
	auto it = field.globalMatch(model);
	while (it.hasNext())
	{
		const QRegularExpressionMatch match = it.next();
		result += model.mid(from, match.capturedStart() - from);
		from = match.capturedEnd();
		const QString name = match.captured(1);
		QString value = name == QLatin1String("unit")  ? unit.trimmed()
					  : name == QLatin1String("id")    ? id.trimmed()
					  : name == QLatin1String("train") ? train.trimmed()
					  : order.trimmed();
		bool number = false;
		value.toInt(&number);
		if (number && !match.captured(2).isEmpty()) {
			value = value.rightJustified(match.captured(2).toInt(), QLatin1Char('0'));
		}
		result += value;
	}
	result += model.mid(from);

	QStringList lines = result.split(QStringLiteral("\\n"));
	for (QString &line : lines) {
		line = line.replace(spaces, QStringLiteral(" ")).trimmed();
	}
	return lines.join(QLatin1Char('\n'));
}

int PidTag::nextOrder(QETProject *project,
					  const QString &unit,
					  const QString &id,
					  const QString &train,
					  const Element *besides)
{
	const QString key = keyOf(unit, id, train);
	int highest = 0;
	for (const Held &held : heldTags(project))
	{
		if (besides && held.element == besides) continue;
		if (keyOf(held.unit, held.id, held.train) != key) continue;
		bool number = false;
		const int order = held.order.trimmed().toInt(&number);
		if (number && order > highest) highest = order;
	}
	return highest + 1;
}

int PidTag::numberPasted(QETProject *project,
						 const QList<Element *> &pasted,
						 bool renumber,
						 bool fresh)
{
	if (!project) {
		return 0;
	}
	QList<Element *> tagged;
	for (Element *element : pasted) {
		if (element && !element->pidTagModel().isEmpty()
				&& hasFields(element->elementInformations())) {
			tagged << element;
		}
	}
	if (tagged.isEmpty()) {
		return 0;
	}

		//Folio by folio, then from left to right and from top to bottom
	const QList<Diagram *> folios = project->folios();
	std::stable_sort(tagged.begin(), tagged.end(), [&folios](Element *a, Element *b) {
		const int folio_a = folios.indexOf(a->diagram());
		const int folio_b = folios.indexOf(b->diagram());
		if (folio_a != folio_b) return folio_a < folio_b;
		if (!qFuzzyCompare(a->pos().x() + 1, b->pos().x() + 1)) return a->pos().x() < b->pos().x();
		return a->pos().y() < b->pos().y();
	});

	QHash<QString, QSet<QString>> held;   //orders held, by key
	QHash<QString, int> highest;          //highest numeric order, by key
	auto hold = [&held, &highest](const QString &key, const QString &order) {
		held[key].insert(normalized(order));
		bool number = false;
		const int value = order.trimmed().toInt(&number);
		if (number && value > highest.value(key)) highest.insert(key, value);
	};
	const QSet<const Element *> pasted_set(pasted.cbegin(), pasted.cend());
	for (const Held &tag : heldTags(project)) {
		if (tag.element && pasted_set.contains(tag.element)) continue;
		if (tag.order.trimmed().isEmpty()) continue;
		hold(keyOf(tag.unit, tag.id, tag.train), tag.order);
	}

		//The pasted elements that keep their order come first, so that a
		//new number goes past theirs
	QList<Element *> clashing;
	for (Element *element : std::as_const(tagged))
	{
			//A new device: the next order, in the order of the sort above
		if (fresh) {
			clashing << element;
			continue;
		}
		const DiagramContext &information = element->elementInformations();
		const QString order = information.value(orderKey()).toString();
		if (order.trimmed().isEmpty()) continue;
		const QString key = keyOf(information.value(unitKey()).toString(),
								  element->getPrefix(),
								  information.value(trainKey()).toString());
		if (renumber && held.value(key).contains(normalized(order))) {
			clashing << element;
		} else {
			hold(key, order);
		}
	}

	QHash<Element *, QString> new_orders;
	for (Element *element : std::as_const(clashing))
	{
		const DiagramContext &information = element->elementInformations();
		const QString key = keyOf(information.value(unitKey()).toString(),
								  element->getPrefix(),
								  information.value(trainKey()).toString());
		const QString order = QString::number(highest.value(key) + 1);
		hold(key, order);
		new_orders.insert(element, order);
	}

	for (Element *element : std::as_const(tagged))
	{
		DiagramContext information = element->elementInformations();
		if (new_orders.contains(element)) {
			information.addValue(orderKey(), new_orders.value(element));
		}
		information = element->withPidLabel(information);
		if (information != element->elementInformations()) {
			setInformation(element, information);
		}
	}
	return new_orders.size();
}

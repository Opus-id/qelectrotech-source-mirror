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
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pidduplicatetags.h"

#include "diagram.h"
#include "pidtag.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/elementtextitemgroup.h"
#include "qetinformation.h"
#include "qetpalette.h"
#include "qetproject.h"
#include "undocommand/changeelementinformationcommand.h"

#include <QCoreApplication>
#include <QPainter>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <vector>

namespace {

		//0 is never current: a fresh cache is always read
	quint64 s_generation = 1;
	bool s_pending = false;

		/// What is kept of a project: find(), as it was at a generation
	struct Cache {
		QPointer<QETProject> project;
		quint64 generation = 0;
		QHash<QUuid, QList<PidDuplicateTags::Other>> found;
	};
	std::vector<Cache> s_caches;

	Cache &cacheOf(QETProject *project)
	{
		s_caches.erase(std::remove_if(s_caches.begin(), s_caches.end(),
									  [](const Cache &cache) { return cache.project.isNull(); }),
					   s_caches.end());
		for (Cache &cache : s_caches) {
			if (cache.project == project) {
				return cache;
			}
		}
			//A folio added or removed brings or takes its tags
		QObject::connect(project, &QETProject::diagramAdded, project, []() { PidDuplicateTags::changed(); });
		QObject::connect(project, &QETProject::diagramRemoved, project, []() { PidDuplicateTags::changed(); });
		s_caches.push_back(Cache{project, 0, {}});
		return s_caches.back();
	}

	QETProject *projectOf(const Element *element)
	{
		const Diagram *folio = element ? element->diagram() : nullptr;
		return folio ? folio->project() : nullptr;
	}

		/// Which tags of @p tags clash, by index: a tag and those it clashes with
	QHash<int, QSet<int>> clashes(const QList<PidTag::Tagged> &tags, Qt::CaseSensitivity sensitivity)
	{
		auto folded = [sensitivity](const QString &text) {
			return sensitivity == Qt::CaseSensitive ? text : text.toCaseFolded();
		};
		QHash<QString, QList<int>> by_key, by_label;
		for (int i = 0 ; i < tags.size() ; ++i)
		{
			const PidTag::Tagged &tag = tags.at(i);
			const QString key = PidTag::sameTagKey(tag);
			if (!key.isEmpty()) {
				by_key[folded(key)] << i;
			}
				//A tag of the model without an order is never a duplicate,
				//not even of a label typed by hand
			if (!tag.label.isEmpty() && (!tag.fields || !key.isEmpty())) {
				by_label[folded(tag.label)] << i;
			}
		}
		QHash<int, QSet<int>> found;
		for (const QList<int> &same : std::as_const(by_key)) {
			for (int a : same) {
				for (int b : same) {
					if (a != b) found[a].insert(b);
				}
			}
		}
			//A label typed by hand is the same as any label it equals; two
			//tags of the model are only compared by their fields
		for (const QList<int> &same : std::as_const(by_label)) {
			for (int a : same) {
				for (int b : same) {
					if (a != b && (!tags.at(a).fields || !tags.at(b).fields)) found[a].insert(b);
				}
			}
		}
		return found;
	}

		/// The folio as the Check panel names it: "3 — Lifting"
	QString folioText(QETProject *project, const QUuid &uuid)
	{
		Diagram *folio = project ? project->diagramByUuid(uuid) : nullptr;
		if (!folio) {
			return QString();
		}
		QString number = folio->border_and_titleblock.finalfolio().trimmed();
		if (number.isEmpty()) {
			number = QString::number(project->folioIndex(folio) + 1);
		}
		const QString title = folio->title().trimmed();
		return title.isEmpty() ? number : QStringLiteral("%1 — %2").arg(number, title);
	}

		/// The texts of @p element which show its tag (or part of it, in a
		/// bubble), in a group or not
	QList<DynamicElementTextItem *> tagTexts(const Element *element)
	{
		QList<DynamicElementTextItem *> texts;
		if (!element) {
			return texts;
		}
		QList<DynamicElementTextItem *> all = element->dynamicTextItems();
		for (ElementTextItemGroup *group : element->textGroups()) {
			all << group->texts();
		}
		for (DynamicElementTextItem *text : std::as_const(all)) {
			if (text->isLabel()) texts << text;
		}
		return texts;
	}

	bool showsItsTag(const Element *element)
	{
		for (const DynamicElementTextItem *text : tagTexts(element)) {
			if (text->isVisible() && !text->toPlainText().trimmed().isEmpty()) {
				return true;
			}
		}
		return false;
	}

		/// @p element is marked: on the screen (on paper when asked), its
		/// tag held elsewhere
	bool marked(const Element *element)
	{
		const Diagram *folio = element ? element->diagram() : nullptr;
		return folio && folio->drawWarnings() && !PidDuplicateTags::othersOf(element).isEmpty();
	}

	QColor veilColour()
	{
		QColor colour = QET::Palette::signalColor();
		colour.setAlpha(90);
		return colour;
	}

	void repaint(Element *element)
	{
		element->update();
		for (DynamicElementTextItem *text : tagTexts(element)) {
			text->update();
		}
	}

		/// A numbered tag whose model shows the train has one: 0 for none
		/// (ElementTagWidget::edited())
	void giveTrain(const Element *element, DiagramContext &information)
	{
		if (information.value(PidTag::trainKey()).toString().trimmed().isEmpty()
				&& !information.value(PidTag::orderKey()).toString().trimmed().isEmpty()
				&& PidTag::showsTrain(element->pidTagModel())) {
			information.addValue(PidTag::trainKey(), QStringLiteral("0"));
		}
	}

	DiagramContext withOrder(const Element *element, const QString &order)
	{
		DiagramContext information = element->elementInformations();
		information.addValue(PidTag::orderKey(), order);
		giveTrain(element, information);
		return element->withPidLabel(information);
	}

		/// One step of its own, never merged with the next change of
		/// information (ChangeElementInformationCommand::id() is 1)
	class NextNumberCommand : public ChangeElementInformationCommand
	{
		public:
			NextNumberCommand(Element *element, const DiagramContext &old_information,
							  const DiagramContext &new_information, const QString &text) :
				ChangeElementInformationCommand(element, old_information, new_information)
			{
				setText(text);
			}
			int id() const override {return -1;}
	};

}

QHash<QUuid, QList<PidDuplicateTags::Other>> PidDuplicateTags::find(QETProject *project,
																	  Qt::CaseSensitivity sensitivity)
{
	QHash<QUuid, QList<Other>> found;
	const QList<PidTag::Tagged> tags = PidTag::tagged(project);
	const QHash<int, QSet<int>> pairs = clashes(tags, sensitivity);
	if (pairs.isEmpty()) {
		return found;
	}
		//The others folio by folio, then by label
	QHash<QUuid, int> folio_index;
	const QList<Diagram *> folios = project->folios();
	for (int i = 0 ; i < folios.size() ; ++i) {
		folio_index.insert(folios.at(i)->uuid(), i);
	}
	for (auto it = pairs.cbegin() ; it != pairs.cend() ; ++it)
	{
		QList<int> others(it.value().cbegin(), it.value().cend());
		std::sort(others.begin(), others.end(), [&tags, &folio_index](int a, int b) {
			const int folio_a = folio_index.value(tags.at(a).folio);
			const int folio_b = folio_index.value(tags.at(b).folio);
			if (folio_a != folio_b) return folio_a < folio_b;
			return tags.at(a).label < tags.at(b).label;
		});
		QList<Other> list;
		for (int other : std::as_const(others)) {
			list << Other{tags.at(other).element, tags.at(other).folio, tags.at(other).label};
		}
		found.insert(tags.at(it.key()).element, list);
	}
	return found;
}

QList<QList<QUuid>> PidDuplicateTags::groups(QETProject *project, Qt::CaseSensitivity sensitivity)
{
	const QList<PidTag::Tagged> tags = PidTag::tagged(project);
	const QHash<int, QSet<int>> pairs = clashes(tags, sensitivity);
	QList<QList<QUuid>> result;
	QSet<int> seen;
	for (int start = 0 ; start < tags.size() ; ++start)
	{
		if (seen.contains(start) || !pairs.contains(start)) continue;
		QList<QUuid> group;
		QList<int> pending{start};
		seen.insert(start);
		while (!pending.isEmpty())
		{
			const int index = pending.takeFirst();
			group << tags.at(index).element;
			for (int other : pairs.value(index)) {
				if (!seen.contains(other)) {
					seen.insert(other);
					pending << other;
				}
			}
		}
		result << group;
	}
	return result;
}

bool PidDuplicateTags::comparedHere(const Element *element)
{
	return element && !element->pidFamily().isEmpty() && !element->pidTagModel().isEmpty();
}

QList<PidDuplicateTags::Other> PidDuplicateTags::othersOf(const Element *element)
{
		//An electrical symbol, a P&ID one without a model: nothing read
	if (!comparedHere(element)) {
		return {};
	}
	QETProject *project = projectOf(element);
	Cache &cache = cacheOf(project);
	if (cache.generation != s_generation) {
		cache.found = find(project);
		cache.generation = s_generation;
	}
	return cache.found.value(element->uuid());
}

QStringList PidDuplicateTags::warnings(const Element *element)
{
	const QList<Other> others = othersOf(element);
	if (others.isEmpty()) {
		return {};
	}
	QETProject *project = projectOf(element);
	QStringList lines{QCoreApplication::translate("PidDuplicateTags",
						"This tag is held elsewhere in the project too:",
						"followed by one line per symbol which holds it")};
		//A tag held ten times: the first ones say enough
	const int shown = others.size() > 6 ? 5 : int(others.size());
	for (int i = 0 ; i < shown ; ++i) {
		const Other &other = others.at(i);
		const QString folio = folioText(project, other.folio);
		lines << (folio.isEmpty() ? other.label
								  : QCoreApplication::translate("PidDuplicateTags", "%1, folio %2",
																"a tag held elsewhere, and its folio")
									.arg(other.label, folio));
	}
	if (shown < others.size()) {
		lines << QCoreApplication::translate("PidDuplicateTags", "and %n more", "symbols",
											 int(others.size()) - shown);
	}
	return lines;
}

void PidDuplicateTags::drawBehind(const DynamicElementTextItem *text, QPainter *painter)
{
	if (!text || !text->isLabel() || !marked(text->parentElement())) {
		return;
	}
	painter->fillRect(text->boundingRect(), veilColour());
}

void PidDuplicateTags::drawOnSymbol(const Element *element, QPainter *painter)
{
	if (!marked(element) || showsItsTag(element)) {
		return;
	}
	painter->fillRect(element->drawingRect(), veilColour());
}

void PidDuplicateTags::changed()
{
	++s_generation;
	if (s_pending || s_caches.empty()) {
		return;
	}
	s_pending = true;
	QTimer::singleShot(0, []() {
		s_pending = false;
		refresh();
	});
}

void PidDuplicateTags::refresh()
{
		//A copy: reading a project may bring a new cache in
	std::vector<QPointer<QETProject>> projects;
	for (const Cache &cache : s_caches) {
		if (cache.project && cache.generation != s_generation) {
			projects.push_back(cache.project);
		}
	}
	for (const QPointer<QETProject> &project : projects)
	{
		if (!project) continue;
		Cache &cache = cacheOf(project);
		QSet<QUuid> moved;
		for (auto it = cache.found.cbegin() ; it != cache.found.cend() ; ++it) {
			moved.insert(it.key());
		}
		cache.found = find(project);
		cache.generation = s_generation;
		for (auto it = cache.found.cbegin() ; it != cache.found.cend() ; ++it) {
				//Marked before and after: its veil stays
			if (!moved.remove(it.key())) moved.insert(it.key());
		}
		if (moved.isEmpty()) continue;
		for (Diagram *folio : project->folios()) {
			if (!folio->isLoaded()) continue;
			for (Element *element : folio->elements()) {
				if (moved.contains(element->uuid())) repaint(element);
			}
		}
	}
}

std::optional<PidDuplicateTags::Proposal> PidDuplicateTags::proposal(QGraphicsItem *item)
{
	Element *element = dynamic_cast<Element *>(item);
	if (!element) {
		if (auto *text = dynamic_cast<DynamicElementTextItem *>(item)) {
			element = text->parentElement();
		}
	}
	if (!element || othersOf(element).isEmpty()
			|| !PidTag::hasFields(element->elementInformations())) {
		return std::nullopt;
	}
	QETProject *project = projectOf(element);
	const QHash<Element *, QString> orders = PidTag::newOrders(project, {element},
															   PidTag::Numbering::Fresh);
	if (!orders.contains(element)) {
		return std::nullopt;
	}
	const QString order = orders.value(element);
	const QString label = withOrder(element, order).value(QETInformation::ELMT_LABEL).toString()
						  .replace(QLatin1Char('\n'), QLatin1Char(' '));
	Proposal proposal;
	proposal.element = element;
	proposal.order = order;
	proposal.text = QCoreApplication::translate("PidDuplicateTags", "Give the next number (%1)",
												"Correct submenu: %1 the tag it gives").arg(label);
	return proposal;
}

QUndoCommand *PidDuplicateTags::apply(const Proposal &proposal)
{
	Element *element = proposal.element.data();
	if (!element) {
		return nullptr;
	}
	const DiagramContext old_information = element->elementInformations();
	const DiagramContext information = withOrder(element, proposal.order);
	if (information == old_information) {
		return nullptr;
	}
	return new NextNumberCommand(element, old_information, information, proposal.text);
}

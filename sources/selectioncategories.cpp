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
#include "selectioncategories.h"

#include "QetGraphicsItemModeler/qetgraphicshandleritem.h"
#include "TerminalStrip/GraphicsItem/terminalstripitem.h"
#include "diagram.h"
#include "pidcodification.h"
#include "pidstatus.h"
#include "pidtag.h"
#include "pipeproperties.h"
#include "qetgraphicsitem/ViewItem/qetgraphicstableitem.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/diagramimageitem.h"
#include "qetgraphicsitem/dimensionitem.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/elementtextitemgroup.h"
#include "qetgraphicsitem/independenttextitem.h"
#include "qetgraphicsitem/qetshapeitem.h"

#include <QCollator>
#include <QHash>
#include <QMap>

#include <algorithm>
#include <functional>

namespace {

	using Node = SelectionCategories::Node;
	using Needs = QHash<QGraphicsItem *, QStringList>;

		/// What an item holds in a branch: the key of the value, empty
		/// for none, and how it is written
	struct Value
	{
		QString key;
		QString label;
	};

		/// A branch which crosses the others: "By size"...
	struct Branch
	{
		QString id;
		QString title;
			/// The items it concerns; all when not set
		std::function<bool(QGraphicsItem *)> applies;
		std::function<Value(QGraphicsItem *)> value;
			/// The value of an item which holds nothing: the branch is
			/// left out when every item has it
		QString default_key;
			/// The keys of the values in their order; by their text when
			/// empty, none last
		QStringList order;
	};

	QCollator collator()
	{
		QCollator c;
		c.setNumericMode(true);
		c.setCaseSensitivity(Qt::CaseInsensitive);
		return c;
	}

		/// The children of @p node by their text, as a list of a file
		/// manager does: DN 80 before DN 100
	void sortByLabel(Node &node)
	{
		const QCollator c = collator();
		std::stable_sort(node.children.begin(), node.children.end(),
						 [&c](const Node &a, const Node &b) {
			return c.compare(a.label, b.label) < 0;
		});
	}

		/// A text typed on an item: none when it is empty
	Value typed(const QString &text, const QString &none)
	{
		const QString trimmed = text.trimmed();
		return trimmed.isEmpty() ? Value{QString(), none} : Value{trimmed, trimmed};
	}

		/// A code of the codification, with what it is when the project
		/// says so
	Value coded(const QString &code, const QString &name, const QString &none)
	{
		const QString trimmed = code.trimmed();
		if (trimmed.isEmpty()) {
			return {QString(), none};
		}
		if (name.isEmpty() || name == trimmed) {
			return {trimmed, trimmed};
		}
		return {trimmed, QStringLiteral("%1 – %2").arg(trimmed, name)};
	}

	QString nameOf(const PidCodification::Entry *entry)
	{
		return entry ? entry->name : QString();
	}

		/// The statuses of works, as keys and names, in their order
	QStringList statusKeys()
	{
		QStringList keys;
		for (const auto &status : PidStatus::statuses()) {
			keys << status.first;
		}
		return keys;
	}

	Value statusValue(const QString &status)
	{
		for (const auto &known : PidStatus::statuses()) {
			if (known.first == status) {
				return {status, known.second};
			}
		}
		return {status, status};
	}

		/**
			Add to @p parent the node of @p branch over @p items, its leaves
			keyed after @p prefix, and to @p needs the key of its value for
			each item it concerns; nothing when every one holds the default.
		*/
	void addBranch(const Branch &branch, const QString &prefix,
				   const QList<QGraphicsItem *> &items, Needs &needs,
				   QList<Node> &parent)
	{
		QList<QPair<QGraphicsItem *, Value>> values;
		bool other_than_default = false;
		for (QGraphicsItem *item : items) {
			if (branch.applies && !branch.applies(item)) {
				continue;
			}
			const Value value = branch.value(item);
			values << qMakePair(item, value);
			other_than_default |= value.key != branch.default_key;
		}
		if (!other_than_default) {
			return;
		}

		Node node;
		node.label = branch.title;
		node.crossed = true;
		QHash<QString, int> index_of;
		for (const auto &pair : std::as_const(values)) {
			const QString key = prefix + branch.id + QLatin1Char('/') + pair.second.key;
			needs[pair.first] << key;
			if (!index_of.contains(key)) {
				index_of.insert(key, node.children.size());
				Node leaf;
				leaf.key = key;
				leaf.label = pair.second.label;
				leaf.count = 0;
				node.children << leaf;
			}
			++node.children[index_of.value(key)].count;
		}

		const int key_start = int((prefix + branch.id).size()) + 1;
		if (branch.order.isEmpty()) {
			sortByLabel(node);
				//None last
			std::stable_partition(node.children.begin(), node.children.end(),
								  [key_start](const Node &leaf) {
				return leaf.key.size() > key_start;
			});
		} else {
			const QStringList &order = branch.order;
			std::stable_sort(node.children.begin(), node.children.end(),
							 [&order, key_start](const Node &a, const Node &b) {
				auto rank = [&order, key_start](const Node &leaf) {
					const int i = int(order.indexOf(leaf.key.mid(key_start)));
					return i < 0 ? int(order.size()) : i;
				};
				return rank(a) < rank(b);
			});
		}
		parent << node;
	}

	PipeProperties pipeOf(QGraphicsItem *item)
	{
		return static_cast<Conductor *>(item)->properties().m_pipe;
	}

	SignalProperties signalOf(QGraphicsItem *item)
	{
		return static_cast<Conductor *>(item)->properties().m_signal;
	}

		/// The categories of the items which are neither pipes nor
		/// symbols, in the order of the tree; the signal lines make a
		/// leaf of that kind when none holds a type or a status
	enum class Kind {
		Signal, Conductor,
		FreeText, SymbolText, LineText,
		Image, Shape, Dimension, Table, TerminalStrip, Other
	};
}

SelectionCategories::SelectionCategories(const QList<QGraphicsItem *> &items,
										 const PidCodification &codes)
{
	QList<QGraphicsItem *> pipes, signal_lines, symbols;
	QMap<int, QList<QGraphicsItem *>> others;
	for (QGraphicsItem *item : items)
	{
		if (!item || dynamic_cast<QetGraphicsHandlerItem *>(item)) {
			continue;
		}
		if (auto *conductor = qgraphicsitem_cast<Conductor *>(item))
		{
			switch (conductor->medium()) {
				case TerminalData::Pipe:   pipes << item; break;
				case TerminalData::Signal: signal_lines << item; break;
				default:                   others[int(Kind::Conductor)] << item; break;
			}
		}
		else if (qgraphicsitem_cast<Element *>(item)) {
			symbols << item;
		}
		else if (dynamic_cast<IndependentTextItem *>(item)) {
			others[int(Kind::FreeText)] << item;
		}
		else if (dynamic_cast<DynamicElementTextItem *>(item)
				 || dynamic_cast<ElementTextItemGroup *>(item)) {
			others[int(Kind::SymbolText)] << item;
		}
		else if (dynamic_cast<ConductorTextItem *>(item)) {
			others[int(Kind::LineText)] << item;
		}
		else if (dynamic_cast<DiagramImageItem *>(item)) {
			others[int(Kind::Image)] << item;
		}
		else if (dynamic_cast<QetShapeItem *>(item)) {
			others[int(Kind::Shape)] << item;
		}
		else if (dynamic_cast<DimensionItem *>(item)) {
			others[int(Kind::Dimension)] << item;
		}
		else if (dynamic_cast<QetGraphicsTableItem *>(item)) {
			others[int(Kind::Table)] << item;
		}
		else if (dynamic_cast<TerminalStripItem *>(item)) {
			others[int(Kind::TerminalStrip)] << item;
		}
		else {
			others[int(Kind::Other)] << item;
		}
	}

	if (!symbols.isEmpty()) {
		addSymbols(symbols, codes);
	}
	if (!pipes.isEmpty()) {
		addPipes(pipes, codes);
	}
	if (!signal_lines.isEmpty()) {
		addSignalLines(signal_lines);
	}

	const QHash<int, QString> labels {
		{int(Kind::Conductor),     tr("Conductors")},
		{int(Kind::FreeText),      tr("Free texts")},
		{int(Kind::SymbolText),    tr("Symbol texts")},
		{int(Kind::LineText),      tr("Texts of lines and conductors")},
		{int(Kind::Image),         tr("Images")},
		{int(Kind::Shape),         tr("Shapes")},
		{int(Kind::Dimension),     tr("Dimensions")},
		{int(Kind::Table),         tr("Tables")},
		{int(Kind::TerminalStrip), tr("Terminal strips")},
		{int(Kind::Other),         tr("Other objects")}};
		//A leaf for each kind there is, its items needing it alone
	const auto leaf_of = [this, &others, &labels](Kind kind) {
		Node leaf;
		leaf.key = QStringLiteral("kind/%1").arg(int(kind));
		leaf.label = labels.value(int(kind));
		const QList<QGraphicsItem *> list = others.value(int(kind));
		leaf.count = int(list.size());
		for (QGraphicsItem *item : list) {
			m_needs << qMakePair(item, QStringList{leaf.key});
		}
		return leaf;
	};
	if (others.contains(int(Kind::Conductor))) {
		m_tree << leaf_of(Kind::Conductor);
	}
		//The texts together
	Node texts;
	texts.label = tr("Texts");
	texts.count = 0;
	for (Kind kind : {Kind::FreeText, Kind::SymbolText, Kind::LineText}) {
		if (others.contains(int(kind))) {
			texts.children << leaf_of(kind);
			texts.count += texts.children.last().count;
		}
	}
	if (!texts.children.isEmpty()) {
		m_tree << texts;
	}
	for (Kind kind : {Kind::Image, Kind::Shape, Kind::Dimension, Kind::Table,
					  Kind::TerminalStrip, Kind::Other}) {
		if (others.contains(int(kind))) {
			m_tree << leaf_of(kind);
		}
	}
}

/**
	@brief SelectionCategories::addPipes
	The node of the pipes: one branch by data of a pipe, or a leaf when no
	pipe holds any.
*/
void SelectionCategories::addPipes(const QList<QGraphicsItem *> &pipes,
								   const PidCodification &codes)
{
	const QString main = PipeProperties::lineKindToString(PipeProperties::Main);
	const QList<QPair<PipeProperties::LineKind, QString>> kinds {
		{PipeProperties::Main,      tr("Main flow line")},
		{PipeProperties::Secondary, tr("Secondary flow line")},
		{PipeProperties::Auxiliary, tr("Auxiliary system line")}};
	QStringList kind_order;
	for (const auto &kind : kinds) {
		kind_order << PipeProperties::lineKindToString(kind.first);
	}
	const QList<QPair<PipeProperties::InsulationKind, QString>> insulation_kinds
			= PipeProperties::insulationKinds();
	QStringList insulation_order;
	for (const auto &kind : insulation_kinds) {
		if (kind.first != PipeProperties::NoInsulation) {
			insulation_order << PipeProperties::insulationKindToString(kind.first);
		}
	}
		//None last, as in the other branches
	insulation_order << QString();

	const QList<Branch> branches {
		{QStringLiteral("size"), tr("By size"), nullptr,
		 [&codes](QGraphicsItem *item) {
			const PipeProperties p = pipeOf(item);
			if (!p.hasSize()) {
				return Value{QString(), tr("No size")};
			}
			const QString text = codes.text(p.writtenSize(codes.outsideDiameterFor(p.material)),
											p.material);
			return Value{text, text};
		 }, QString(), {}},
		{QStringLiteral("fluid"), tr("By fluid"), nullptr,
		 [&codes](QGraphicsItem *item) {
			const QString code = pipeOf(item).fluid;
			return coded(code, nameOf(codes.fluid(code.trimmed())), tr("No fluid"));
		 }, QString(), {}},
		{QStringLiteral("material"), tr("By material"), nullptr,
		 [&codes](QGraphicsItem *item) {
			const QString code = pipeOf(item).material;
			return coded(code, nameOf(codes.material(code.trimmed())), tr("No material"));
		 }, QString(), {}},
		{QStringLiteral("line_number"), tr("By line number"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).line_number, tr("No line number")); },
		 QString(), {}},
		{QStringLiteral("piping_class"), tr("By piping class"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).piping_class, tr("No piping class")); },
		 QString(), {}},
		{QStringLiteral("nominal_pressure"), tr("By nominal pressure"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).nominal_pressure, tr("No nominal pressure")); },
		 QString(), {}},
		{QStringLiteral("line_kind"), tr("By kind of line"), nullptr,
		 [kinds](QGraphicsItem *item) {
			const PipeProperties::LineKind kind = pipeOf(item).line_kind;
			for (const auto &known : kinds) {
				if (known.first == kind) {
					return Value{PipeProperties::lineKindToString(kind), known.second};
				}
			}
			return Value{PipeProperties::lineKindToString(kind), PipeProperties::lineKindToString(kind)};
		 }, main, kind_order},
		{QStringLiteral("status"), tr("By status of works"), nullptr,
		 [](QGraphicsItem *item) { return statusValue(pipeOf(item).status); },
		 QString(), statusKeys()},
		{QStringLiteral("flow"), tr("By flow"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).flow, tr("No flow")); },
		 QString(), {}},
		{QStringLiteral("spool"), tr("By spool"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).spool, tr("No spool")); },
		 QString(), {}},
		{QStringLiteral("external_finish"), tr("By external finish"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).external_finish, tr("No external finish")); },
		 QString(), {}},
		{QStringLiteral("insulation_kind"), tr("By insulation"), nullptr,
		 [insulation_kinds](QGraphicsItem *item) {
			const PipeProperties::InsulationKind kind = pipeOf(item).insulation_kind;
			for (const auto &known : insulation_kinds) {
				if (known.first == kind) {
					return Value{PipeProperties::insulationKindToString(kind),
								 kind == PipeProperties::NoInsulation ? tr("No insulation") : known.second};
				}
			}
			return Value{PipeProperties::insulationKindToString(kind), PipeProperties::insulationKindToString(kind)};
		 }, QString(), insulation_order},
		{QStringLiteral("insulation"), tr("By insulation text"), nullptr,
		 [](QGraphicsItem *item) { return typed(pipeOf(item).insulation, tr("No insulation text")); },
		 QString(), {}}};

	Node node;
	node.label = tr("Pipe lines");
	node.count = int(pipes.size());
	Needs needs;
	for (const Branch &branch : branches) {
		addBranch(branch, QStringLiteral("pipe/"), pipes, needs, node.children);
	}
	if (node.children.isEmpty()) {
		node.key = QStringLiteral("kind/pipe");
		for (QGraphicsItem *pipe : pipes) {
			needs[pipe] << node.key;
		}
	}
	for (QGraphicsItem *pipe : pipes) {
		m_needs << qMakePair(pipe, needs.value(pipe));
	}
	m_tree << node;
}

/**
	@brief SelectionCategories::addSignalLines
	The node of the signal lines: a branch by type of signal and one by
	status of works, or a leaf when every line is of undefined type, to be
	built.
*/
void SelectionCategories::addSignalLines(const QList<QGraphicsItem *> &lines)
{
	QStringList type_order;
	for (SignalProperties::SignalType type : SignalProperties::types()) {
		type_order << SignalProperties::typeToString(type);
	}

	const QList<Branch> branches {
		{QStringLiteral("type"), tr("By type of signal"), nullptr,
		 [](QGraphicsItem *item) {
			const SignalProperties::SignalType type = signalOf(item).signal_type;
			return Value{SignalProperties::typeToString(type), SignalProperties::typeName(type)};
		 }, SignalProperties::typeToString(SignalProperties::Undefined), type_order},
		{QStringLiteral("status"), tr("By status of works"), nullptr,
		 [](QGraphicsItem *item) { return statusValue(signalOf(item).status); },
		 QString(), statusKeys()}};

	Node node;
	node.label = tr("Signal lines");
	node.count = int(lines.size());
	Needs needs;
	for (const Branch &branch : branches) {
		addBranch(branch, QStringLiteral("signal/"), lines, needs, node.children);
	}
	if (node.children.isEmpty()) {
		node.key = QStringLiteral("kind/%1").arg(int(Kind::Signal));
		for (QGraphicsItem *line : lines) {
			needs[line] << node.key;
		}
	}
	for (QGraphicsItem *line : lines) {
		m_needs << qMakePair(line, needs.value(line));
	}
	m_tree << node;
}

/**
	@brief SelectionCategories::addSymbols
	The node of the symbols: by family of objects, then by symbol; the
	family left out when every symbol has none (electrical); then the
	branches by functional unit and by status of works.
*/
void SelectionCategories::addSymbols(const QList<QGraphicsItem *> &symbols,
									 const PidCodification &codes)
{
	const QList<QPair<QString, QString>> families {
		{QStringLiteral("equipment"),  tr("Equipment")},
		{QStringLiteral("valve"),      tr("Valves")},
		{QStringLiteral("instrument"), tr("Instruments")},
		{QStringLiteral("pipe"),       tr("Pipe fittings (tee, reducer…)")},
		{QString(),                    tr("Other symbols")}};

	Needs needs;
	QMap<int, Node> by_family;
	for (QGraphicsItem *item : symbols)
	{
		auto *element = static_cast<Element *>(item);
		int family = int(families.size()) - 1;
		for (int i = 0 ; i < families.size() ; ++i) {
			if (families.at(i).first == element->pidFamily()) {
				family = i;
				break;
			}
		}
		Node &family_node = by_family[family];
		if (family_node.label.isEmpty()) {
			family_node.label = families.at(family).second;
			family_node.count = 0;
		}
		++family_node.count;

		const QString key = QStringLiteral("symbol/") + element->location().toString();
		needs[item] << key;
		auto leaf = std::find_if(family_node.children.begin(), family_node.children.end(),
								 [&key](const Node &n) { return n.key == key; });
		if (leaf == family_node.children.end()) {
			Node n;
			n.key = key;
			n.label = element->name();
			n.count = 0;
			family_node.children << n;
			leaf = family_node.children.end() - 1;
		}
		++leaf->count;
	}

	Node node;
	node.label = tr("Symbols");
	node.count = int(symbols.size());
	for (auto it = by_family.begin() ; it != by_family.end() ; ++it) {
		sortByLabel(it.value());
	}
	if (by_family.size() == 1 && by_family.firstKey() == int(families.size()) - 1) {
		node.children = by_family.first().children;
	} else {
		node.children = by_family.values();
	}

	const QList<Branch> branches {
		{QStringLiteral("unit"), tr("By functional unit"), nullptr,
		 [&codes](QGraphicsItem *item) {
			const QString code = static_cast<Element *>(item)->elementInformations()
								 .value(PidTag::unitKey()).toString();
			QString name;
			for (const auto &unit : codes.functional_units) {
				if (unit.code == code.trimmed()) {
					name = unit.name;
					break;
				}
			}
			return coded(code, name, tr("No functional unit"));
		 }, QString(), {}},
		{QStringLiteral("status"), tr("By status of works"),
		 [](QGraphicsItem *item) { return PidStatus::applies(item); },
		 [](QGraphicsItem *item) {
			return statusValue(static_cast<Element *>(item)->optionChoices()
							   .value(PidStatus::option()));
		 }, QString(), statusKeys()}};
	for (const Branch &branch : branches) {
		addBranch(branch, QStringLiteral("symbol-by/"), symbols, needs, node.children);
	}

	for (QGraphicsItem *symbol : symbols) {
		m_needs << qMakePair(symbol, needs.value(symbol));
	}
	m_tree << node;
}

QList<QGraphicsItem *> SelectionCategories::itemsOf(const Diagram *diagram)
{
	if (!diagram) {
		return {};
	}
	const QList<QGraphicsItem *> selected = diagram->selectedItems();
	return selected.isEmpty() ? folioItems(diagram) : selected;
}

QList<QGraphicsItem *> SelectionCategories::folioItems(const Diagram *diagram)
{
	if (!diagram) {
		return {};
	}
	QList<QGraphicsItem *> all;
	for (QGraphicsItem *item : diagram->items()) {
		if ((item->flags() & QGraphicsItem::ItemIsSelectable)
			&& item->isVisible()
			&& !dynamic_cast<QetGraphicsHandlerItem *>(item)) {
			all << item;
		}
	}
	return all;
}

QSet<QString> SelectionCategories::leaves() const
{
	QSet<QString> keys;
	std::function<void(const Node &)> walk = [&keys, &walk](const Node &node) {
		if (node.children.isEmpty()) {
			keys << node.key;
		}
		for (const Node &child : node.children) {
			walk(child);
		}
	};
	for (const Node &node : m_tree) {
		walk(node);
	}
	return keys;
}

QString SelectionCategories::branchOf(const QString &key)
{
		//"pipe/<branch>/<value>", "signal/<branch>/<value>",
		//"symbol-by/<branch>/<value>": the value may hold a slash
		//("DN 60/65"), the branch does not
	for (const QString &prefix : {QStringLiteral("pipe/"), QStringLiteral("signal/"),
								  QStringLiteral("symbol-by/")}) {
		if (key.startsWith(prefix)) {
			const qsizetype slash = key.indexOf(QLatin1Char('/'), prefix.size());
			return slash < 0 ? key : key.left(slash);
		}
	}
	if (key.startsWith(QStringLiteral("symbol/"))) {
		return QStringLiteral("symbol");
	}
	return key;
}

QString SelectionCategories::categoryOf(const QString &key)
{
	if (key.startsWith(QStringLiteral("pipe/"))) {
		return QStringLiteral("pipe");
	}
	if (key.startsWith(QStringLiteral("signal/"))) {
		return QStringLiteral("signal");
	}
	if (key.startsWith(QStringLiteral("symbol/")) || key.startsWith(QStringLiteral("symbol-by/"))) {
		return QStringLiteral("symbol");
	}
	return key;
}

QList<QGraphicsItem *> SelectionCategories::kept(const QSet<QString> &checked) const
{
	QSet<QString> branches, categories;
	for (const QString &key : checked) {
		branches << branchOf(key);
		categories << categoryOf(key);
	}
	QList<QGraphicsItem *> list;
	for (const auto &pair : m_needs) {
		const QStringList &needs = pair.second;
		if (needs.isEmpty() || !categories.contains(categoryOf(needs.first()))) {
			continue;
		}
		if (std::all_of(needs.cbegin(), needs.cend(), [&](const QString &key) {
				return checked.contains(key) || !branches.contains(branchOf(key));
			})) {
			list << pair.first;
		}
	}
	return list;
}

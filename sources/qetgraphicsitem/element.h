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
#ifndef ELEMENT_H
#define ELEMENT_H

#include <ElementsCollection/elementslocation.h>
#include <NameList/nameslist.h>
#include <autoNum/assignvariables.h>
#include <diagramcontext.h>
#include <dataBase/elementplacement.h>
#include <qet.h>
#include "qetgraphicsitem.h"
#include <properties/elementdata.h>
#include "../pipeconnection.h"
#include "../pipediameter.h"

#include <QHash>
#include <optional>
#include <QSet>
#include <QPicture>
#include <algorithm>

class QETProject;
class Terminal;
class Conductor;
class DynamicElementTextItem;
class ElementTextItemGroup;
class PidCodification;
class QUndoCommand;

/**
	This is the base class for electrical elements.
*/
class projectDataBase;

class Element : public QetGraphicsItem
{
	friend class DiagramEventAddElement;
	friend class DynamicElementTextItem;
	friend class ElementTextItemGroup;

	Q_OBJECT
	Q_PROPERTY(bool horizontalMirror READ hasHorizontalMirror WRITE setHorizontalMirror NOTIFY mirrorChanged)
	Q_PROPERTY(bool verticalMirror READ hasVerticalMirror WRITE setVerticalMirror NOTIFY mirrorChanged)
	Q_PROPERTY(QVariantMap potentialSources READ potentialSourcesMap WRITE setPotentialSourcesMap)
	Q_PROPERTY(bool shownAsDot READ isShownAsDot WRITE setShownAsDot)

	public:
			/**
				@brief The kind enum
				Used to know the kind of this element
				(master, slave, report ect...)
			*/
		enum kind {
            Simple = 1,
            NextReport = 2,
            PreviousReport = 4,
            AllReport = 6,
            Master = 8,
            Slave = 16,
            Terminale = 32,
            Thumbnail = 64,
            ConductorDefinition = 128,
            Junction = 256};

		/**
		 * @brief The LinkInfo struct
		 * Stores link data for element connections.
		 * For slave elements, group_index indicates which contact group
		 * of the master this slave is assigned to (-1 = no group assigned).
		 */
		struct LinkInfo {
			QUuid uuid;
			int group_index = -1;  ///< Index into master's slaveContactGroups, -1 = not assigned

			LinkInfo() = default;
			LinkInfo(const QUuid &u, int gi = -1) : uuid(u), group_index(gi) {}
		};

		Element(const ElementsLocation &location,
			QGraphicsItem * = nullptr,
			int *state = nullptr,
			Element::kind link_type = Element::Simple);
		~Element() override;
	private:
		bool definitionGeometryMatches(const QDomElement &definition,
									   bool check_frame = true) const;
		Element(const Element &);

		// attributes
	public:
			/**
				Enable the use of qgraphicsitem_cast
				to safely cast a QGraphicsItem into an Element.
				@return the QGraphicsItem type
			*/
		enum { Type = UserType + 1000 };
		int type() const override { return Type; }

	signals:
		void linkedElementChanged(); //This signal is emitted when the linked elements with this element change
		void elementInfoChange(
				DiagramContext old_info,
				DiagramContext new_info);
		void textAdded(DynamicElementTextItem *deti);
		void textRemoved(DynamicElementTextItem *deti);
		void textsGroupAdded(ElementTextItemGroup *group);
		void textsGroupAboutToBeRemoved(ElementTextItemGroup *group);
		void textAddedToGroup(
				DynamicElementTextItem *text,
				ElementTextItemGroup *group);
		void textRemovedFromGroup(
				DynamicElementTextItem *text,
				ElementTextItemGroup *group);
		void mirrorChanged();
			/// The options of the symbol were given other values
			/// (setOptionChoices())
		void optionChoicesChanged();
			/// Its nozzles were given other diameters (setNozzleDiameters()),
			/// other connections (setNozzleConnections()), other choices of
			/// their marks (setNozzleMarks()) or other links
			/// (setUnlinkedNozzles())
		void nozzleDiametersChanged();
			/// The element took another symbol (changeLocation())
		void symbolChanged();

	public slots:
		void updateConductorTexts();
		void watchConductorText(Conductor *conductor);

	public:
		QList<Terminal *> terminals() const;
		QList<Conductor *> conductors() const;
		QList<QPair<Terminal *,Terminal *>> AlignedFreeTerminals() const;

		void clearPendingLinks() {
			tmp_uuids_link.clear();
			m_group_index_map.clear();
		}

			//METHODS related to information
		DiagramContext elementInformations()const;
			/// This symbol's own copy of its information, whatever the store holds
		DiagramContext ownInformations() const {return m_data.m_informations;}
		virtual void setElementInformations(DiagramContext dc);

		ElementData elementData() const;
		void setElementData (ElementData data);

		/**
		 * @brief kindInformations
		 * @deprecated
		 * use elementData function instead
		 * @return
		 */
		DiagramContext kindInformations() const;
			/// A symbol that only steers the drawing (kind information
			/// drawing_aid="true"): drawn light grey, never printed
		bool isDrawingAid() const;
			/// This symbol's own copy of its kind information, whatever the store holds
		DiagramContext ownKindInformations() const {return m_kind_informations;}
			/// Where this symbol comes from and sits, from its own members,
			/// never from the store
		ElementPlacement ownPlacement() const;
		void storePlacement();
			/// This symbol's texts and text groups as it saves them, in an
			/// <element> made in @p document, from its own items
		QDomElement ownTextsXml(QDomDocument &document) const;
			/// What this symbol saves that no other table holds, in an
			/// <element> made in @p document, from its own members
		QDomElement ownValuesXml(QDomDocument &document) const;
			/// The potential types set on terminals of this placed symbol,
			/// for a project that takes them from where the potential
			/// starts (PotentialTypes::Mode::AtSource): Terminal::stableUuid()
			/// to type name. Saved in the project, never in the definition.
		QHash<QUuid, QString> potentialSources() const {return m_potential_sources;}
		void setPotentialSources(const QHash<QUuid, QString> &sources);
			/// The same, keyed by the uuid as text, for QPropertyUndoCommand
		QVariantMap potentialSourcesMap() const;
		void setPotentialSourcesMap(const QVariantMap &sources);
		void setMovable(bool movable) override;
		//@kind_information_ is used to store more information
		//about the herited class like contactelement for know
		// kind of contact (simple tempo) or number of contact show by the element.

		autonum::sequentialNumbers sequenceStruct() const
		{return m_autoNum_seq;}
			/// Replace the numbers and tell the project database
		void setSequenceStruct(const autonum::sequentialNumbers &numbers);
		void storeSequence();
		void setUpFormula(bool code_letter = true, QUndoCommand *parent_undo = nullptr);
		void setFormulaSchemeId(const QUuid &id);
		void setPrefix(QString);
		QString getPrefix() const;
		void freezeLabel(bool freeze);
		bool isFreezeLabel() const {return m_freeze_label;}
		void freezeNewAddedElement();
		QString actualLabel() const;
			/**
				What the drawing shows of the label and of the terminal
				number, the terminal labelling of the project being
				read (@sa QETProject::terminalLabelSplit). The label itself
				is never changed by any of them.
			*/
		QString labelForDisplay();
		QString terminalNumberForDisplay();
		QString conductorTextForDisplay();
		QString conductorInfoForDisplay(const QString &info_name);
		void addDisplayValues(DiagramContext &dc);
			/**
				The part @p key (QETInformation::pidTagPartKeys()) of the
				tag of this P&ID symbol, for a text of its own: its
				identifier (PID_TAG_ID: the letters of an instrument), or
				the label its model makes without it (PID_TAG_REST). With
				no model or no field, the label typed by hand: its first
				line, then the others. Display only, nothing is stored.
			*/
		QString pidTagPart(const QString &key);
		QString infoForDisplay(const QString &info_name,
							   const Element *drawn_on = nullptr);
		QString designationForDisplay(const QString &label,
									  const Element *drawn_on = nullptr) const;
		bool hasTerminalNumberText() const;
		bool labelShown() const;
			/**
				The fields which draw the label draw it again: called when
				what they show of the label has changed under them, a
				field for the terminal number added or removed, or the
				terminal labelling setting of the project switched (@sa
				QETProject::terminalLabelSplit).
			*/
		void refreshLabelFields();

		QString name() const override;
		QString englishName() const;
		ElementsLocation location() const;
		/// Result of Element::reloadPicture()
		enum class ReloadPictureResult {
			Reloaded,          ///< drawing replaced by the current definition
			Unavailable,       ///< definition missing or unreadable, old drawing kept
			GeometryChanged    ///< size, hotspot or terminals changed, old drawing kept
		};
		ReloadPictureResult reloadPicture(bool adopt_frame = false);
		bool canChangeLocationTo(const ElementsLocation &location) const;
		bool changeLocation(const ElementsLocation &location);
			/// The texts the P&ID symbol at @p location shows from an
			/// information of the element (its comment, its label) that no
			/// text of this element shows yet: made from that symbol, not
			/// added. Change type adds them (ChangeElementLocationCommand),
			/// the texts the element has staying as they are (step 8b)
		QList<DynamicElementTextItem *> textsOfNewType(const ElementsLocation &location);
			/// The symbols which can replace each of @p elements as they
			/// stand ("Change type"), sorted by name; none on a read-only
			/// folio
		static QList<ElementsLocation> otherTypes(const QList<Element *> &elements);
			/// Swap the symbol of @p elements for the one at @p location,
			/// with undo: one step for them all. False when none could
		static bool changeType(const QList<Element *> &elements, const ElementsLocation &location);
			/// The options of the symbol (ElementOptions) given a value on
			/// this element; any other is at its default
		ElementOptions::Choices optionChoices() const { return m_option_choices; }
		void setOptionChoices(const ElementOptions::Choices &choices);
			/// The value of every option of the symbol for this element
		ElementOptions::Choices resolvedOptions() const;
			/// The diameters this element gives its nozzles, by name of
			/// nozzle; any other has the one of its symbol
		NozzleDiameters nozzleDiameters() const { return m_nozzle_diameters; }
		void setNozzleDiameters(const NozzleDiameters &diameters);
			/// Those of the <pid_nozzles> block of @p element
		static NozzleDiameters nozzleDiametersFromXml(const QDomElement &element);
			/// The connections this element gives its nozzles, by name of
			/// nozzle; any other has the one of its symbol
		NozzleConnections nozzleConnections() const { return m_nozzle_connections; }
		void setNozzleConnections(const NozzleConnections &connections);
			/// Those of the <pid_nozzles> block of @p element
		static NozzleConnections nozzleConnectionsFromXml(const QDomElement &element);
			/// Whether the nozzles of this element draw the mark of their
			/// connection, by name of nozzle; any other does as its project
			/// says (Terminal::drawsConnectionMark())
		NozzleMarks nozzleMarks() const { return m_nozzle_marks; }
		void setNozzleMarks(const NozzleMarks &marks);
			/// Those of the <pid_nozzles> block of @p element
		static NozzleMarks nozzleMarksFromXml(const QDomElement &element);
			/// The links of the tab of its nozzles the user undid
			/// (NozzleLinks): none for an element whose links follow its
			/// nozzles, linked when they are alike
		NozzleLinks unlinkedNozzles() const { return m_unlinked_nozzles; }
		void setUnlinkedNozzles(NozzleLinks unlinked);
			/// Those of the <pid_nozzles> block of @p element
		static NozzleLinks unlinkedNozzlesFromXml(const QDomElement &element);
			/// Its P&ID family (ElementData::m_pid_family), without copying
			/// all its data: none for any other symbol
		const QString &pidFamily() const { return m_data.m_pid_family; }
		bool isPipeInline() const;
		QStringList pipeRun() const;
			/// True for a reducer (pid_role="reducer"): the size of the line
			/// changes there, whatever the sizes of its nozzles, and a change
			/// of DN stops there
		bool isReducer() const {
			return m_data.m_pid_role == QLatin1String("reducer");
		}
			/// The name of the nozzle on the large side of a reducer
			/// (pid_large); none when the symbol says nothing of it
		const QString &largeNozzle() const { return m_data.m_pid_large; }
			/// The two sizes a reducer writes in its label while it has
			/// none of its own (step 8r-3 e): "100 × 80", "Ø 125 × 110",
			/// in the order of the drawing (from the left; a reducer
			/// standing, as its vertical texts read). Empty for any other
			/// symbol, a side without a size, or a project which writes
			/// them not (PidCodification::reducer_sizes). Display only:
			/// never stored, never its label (actualLabel())
		QString reducerSizesText() const;
			/// Work out reducerSizesText() again, and draw the label
			/// fields again when it changed: called when a size, the way
			/// the reducer lies or the setting of its project changes
		void refreshReducerSizes();
			/// True for the free end of a pipe (pid_role="free_end"), which
			/// goes when its pipe is deleted, a strainer or a vent staying
		bool isFreeEnd() const {
			return m_data.m_pid_role == QLatin1String("free_end");
		}
			/// True for a symbol a tool placed by itself where the user did not
			/// place one: the blind flange which closes a free end a process
			/// connection is drawn from (PidProcessConnection). Saved as
			/// pid_tool_placed="true", only when set
		bool isToolPlaced() const { return m_tool_placed; }
		void setToolPlaced(bool placed) { m_tool_placed = placed; }
			/// Whether this symbol goes when the pipe it ends is deleted: a free
			/// end, or a symbol a tool placed there (isToolPlaced())
		bool goesWithItsPipe() const { return isFreeEnd() || m_tool_placed; }
			/// True for a closure of a line (pid_role="closure": blind flange,
			/// dished end, cap), or a symbol a tool placed to close one: deleted
			/// alone, it leaves its pipe on a free end; its signal terminal
			/// takes one process connection only (6b-3c2)
		bool isClosure() const {
			return m_data.m_pid_role == QLatin1String("closure") || m_tool_placed;
		}
			/// True for a spec break (pid_role="spec_break"): the mark where
			/// the material, piping class and nominal pressure of a line
			/// change; its size goes on through it
		bool isSpecBreak() const {
			return m_data.m_pid_role == QLatin1String("spec_break");
		}
			/// True for a junction (pid_role="junction"): a tee, a cross, an
			/// injection point. Its symbol does not draw its lines: each is
			/// drawn from the origin of the symbol to a nozzle, as the pipe
			/// of that nozzle (drawJunction())
		bool isJunction() const {
			return m_data.m_pid_role == QLatin1String("junction");
		}
			/// True for an insulation limit (pid_role="insulation_limit",
			/// step 7d): an in-line symbol where the insulation of a line
			/// stops or changes. The insulated pipe on each side draws its
			/// stub there; what else a line carries goes through it.
		bool isInsulationLimit() const {
			return m_data.m_pid_role == QLatin1String("insulation_limit");
		}
			/// True for a jacket limit (pid_role="jacket_limit", step 8r-1):
			/// an in-line symbol where the jacket of a double containment
			/// line stops or changes, as an insulation limit for the
			/// insulation; what else a line carries goes through it.
		bool isJacketLimit() const {
			return m_data.m_pid_role == QLatin1String("jacket_limit");
		}
			/// True for a flow arrow (pid_role="flow_arrow", step 8a): the
			/// arrow of the direction of flow, a drawing on the line, as
			/// the PipeFlowArrow of DEXPI. Nothing stops there.
		bool isFlowArrow() const {
			return m_data.m_pid_role == QLatin1String("flow_arrow");
		}
			/// A mark on the line, no piece: a spec break, an insulation
			/// limit, a jacket limit, a flow arrow (ElementData::isLineMarkRole()). It
			/// never draws a connection, the pipes are drawn into its
			/// middle, and a symbol placed against it meets the connection
			/// beyond it, as if it were not there.
		bool isLineMark() const {
			return ElementData::isLineMarkRole(m_data.m_pid_role);
		}
			/// Draw the symbol again in the colour its project sets now
		void updateInk();
			/// The colour the project gives the label of this symbol: that
			/// of the symbol, or a fixed one; none when it gives none, or
			/// when the folio is drawn in black
		QColor labelInk() const;
			/// The colour the symbol is drawn in: that of its status of
			/// works, of its pipe or of its family; black when it has none or
			/// when the folio is drawn in black
		QColor symbolInk() const;
			/// The texts of this element showing its label, grouped or not
		QList<DynamicElementTextItem *> labelTexts() const;
			/// The size of the labels of this symbol under @p codification:
			/// the one of its family, else the one of the symbol. 0 for a
			/// symbol which is not of a P&ID
		int labelSize(const PidCodification &codification) const;
			/// Give the labels of this new element the size of its family,
			/// if @p codification sets one
		void takeLabelSize(const PidCodification &codification);
			/// Give this new P&ID element the options its family takes in
			/// @p codification (PidCodification::newChoices()), written in
			/// its choices: a change of the project later does not reach it.
			/// Only for a symbol placed new, not a copy of another one.
		void takeOptionDefaults(const PidCodification &codification);
			/// The model of the tags of the family of this P&ID symbol in
			/// its project (PidTag), empty when there is none
		QString pidTagModel() const;
			/// @p information with the label the model makes of its tag
			/// fields and the identifier of this element; as it is when
			/// it has no field or the project no model
		DiagramContext withPidLabel(const DiagramContext &information) const;
			/// Give this element the label its tag makes now: after its
			/// identifier or the model of its project changed. Nothing to
			/// undo, the label follows them
		void updatePidLabel();
			/// True for a fitting (a tee, the end of a pipe), drawn after
			/// the pipe it is connected to
		bool followsPipes() const;
			/// True when the colour of this symbol depends on its pipes: a
			/// fitting (followsPipes()), or a valve or an instrument whose
			/// project says so (PidCodification::symbols_follow_pipes, step
			/// 8r-2); its drawing is taken again when one of them changes
		bool takesPipeInk() const;
			/// The colour of the pipes joined to this symbol when they all
			/// have the same, one being enough; an invalid one when they
			/// differ, when one has none, or without a pipe
		QColor agreedPipeColor() const;
			/// What its tool tip says of the directions of flow this symbol
			/// is in disagreement with (step 8a-bis-2); nothing when none
		QStringList flowDisagreements() const;
			/// The radius of the orange disc of drawFlowDisagreement(): 2 mm
		static constexpr qreal flowDisagreementRadius = 8.0;
			/// Move this symbol back by the offset of its insertion point
			/// when its definition moved as a whole (upstream #1896)
		bool followMovedOrigin();
			/// Whether updating the elements from the collections moves a
			/// placed symbol whose insertion point moved, so that its
			/// terminals stay where they are: off unless set
		static bool updateFollowsMovedOrigin();
		static void setUpdateFollowsMovedOrigin(bool follows);
		virtual void setHighlighted(bool);
		void showAsJumpTarget();
		bool isShownAsJumpTarget() const { return m_jump_highlight > 0; }
		Element *jumpTarget() const;
		static void jumpTo(Element *target);
		void displayHelpLine(bool b = true);
		QSize size() const;
		QPixmap pixmap();
		QPoint setHotspot(QPoint);
		QPoint hotspot() const;
			/// The area the symbol is drawn on, in its own coordinates
		QRectF drawingRect() const;
		void editProperty() override;
		static bool valideXml(QDomElement &);
		static QUuid savedTerminalUuid(const QList<QDomElement> &records, int index);
		virtual bool fromXml(
				QDomElement &,
				QHash<int,
				Terminal *> &);
		virtual QDomElement toXml(
				QDomDocument &,
				QHash<Terminal *,
				int> &) const;
		QUuid uuid() const;
		int orientation() const;
		bool hasHorizontalMirror() const {return m_horizontal_mirror;}
		bool hasVerticalMirror() const {return m_vertical_mirror;}
		bool isMirrored() const {return m_horizontal_mirror || m_vertical_mirror;}
		void setHorizontalMirror(bool mirror);
		void setVerticalMirror(bool mirror);
		QTransform mirrorTransform() const;
		bool hasUprightSymbolTexts() const;
		QTransform symbolTextsTransform() const;
		void updateSymbolPictures(bool force = false);
		void keepReadable(QGraphicsItem *child) const;
			/// What moves the label @p text of a P&ID symbol whose line is
			/// upright on the folio (turned a quarter), kept horizontal,
			/// beside the line rather than across it: added to its
			/// transform() by keepReadable(), never saved. None for another
			/// text or symbol, an electrical one among them.
		QTransform labelBesideLine(const DynamicElementTextItem *text) const;
			/// The vertical texts of this symbol read from the bottom: for
			/// its label, its bubble and its nozzles
			/// (DynamicElementTextItem::verticalFromBottom())
		bool readsVerticalFromBottom() const;
			/// What places a part of the tag (the two texts of a bubble,
			/// DynamicElementTextItem::isLabelPart()) of this P&ID symbol as
			/// on the symbol upright, turned with it as it is read
			/// (readingAngle()), even when the project keeps the texts of
			/// its symbols level: whatever the turn and the mirrors of the
			/// symbol, the letters come first. Its transform().
		QTransform labelPartPlace(const DynamicElementTextItem *text) const;
			/// The angle a text of this symbol at @p angle on the folio
			/// (0, 90, 180 or 270) is read at: never upside down, and from
			/// the bottom when readsVerticalFromBottom()
		qreal readingAngle(qreal angle) const;

			//METHODS related to texts
		void addDynamicTextItem(DynamicElementTextItem *deti = nullptr);
		void removeDynamicTextItem(DynamicElementTextItem *deti);
		QList<DynamicElementTextItem *> dynamicTextItems() const;
		ElementTextItemGroup *addTextGroup(const QString &name);
		void addTextGroup(ElementTextItemGroup *group);
		void removeTextGroup(ElementTextItemGroup *group);
		ElementTextItemGroup *textGroup(const QString &name) const;
		QList<ElementTextItemGroup *> textGroups() const;
		bool addTextToGroup(
				DynamicElementTextItem *text,
				ElementTextItemGroup *group);
		bool removeTextFromGroup(
				DynamicElementTextItem *text,
				ElementTextItemGroup *group);

			//METHODS related to linked element
		bool isFree() const;
		virtual void linkToElement(Element *) {}
		virtual void unlinkAllElements() {}
		virtual void unlinkElement(Element *) {}
		virtual void initLink(QETProject *);
			/// The links this symbol was read with (its file's links_uuids),
			/// until it is linked: the project database writes its link rows
			/// from them while it holds none for it (opening)
		QList<std::pair<QUuid, int>> savedLinkList() const;
		void dropSavedLinks() {tmp_uuids_link.clear();}
		void restoreLinkOrder(const QList<QUuid> &order);
			/**
				Resolve tmp_uuids_link against a caller-supplied candidate
				list instead of a project-wide search (bugtracker #607).
				Used right after an XML round-trip (paste, folio
				duplication) and before the pasted/duplicated elements'
				uuids are renewed: at that moment a copy's tmp_uuids_link
				still holds its source's original partner uuid, which
				still matches the not-yet-renewed uuid of that partner's
				own copy if it was carried along in the same batch.
				Resolving only within @p candidates -- not the whole
				project -- is what stops a linked pair pasted together
				from matching an original element left elsewhere that
				happens to still carry that same soon-to-be-replaced
				uuid. If only one half of a linked group is in
				@p candidates, its entry finds no match and is dropped,
				same as initLink(QETProject *) leaving an unresolvable
				link unlinked.
			*/
		void initLink(const QList<Element *> &candidates);
		QList<Element *> linkedElements () const;
			/**
				@brief Element::allLinkedElements
				@return every partner this element keeps, the ones whose
				folio left the project included, in the order it saves
				them in.

				linkedElements() is the list to show and to count, and it
				leaves those away (standsInProject()). This one is the
				list itself: undo() has to put back what was really
				there, and the link commands act on it.
			*/
		QList<Element *> allLinkedElements () const;
			/**
				Whether this element still stands on a folio of its own
				project.

				A folio taken out of the project is detached rather than
				destroyed -- undo() puts it back -- so its elements keep
				their links while they are away. Those links may not be
				counted or shown by the elements that stay: a master
				which goes on counting a contact whose folio is gone is
				full, takes no new one, and lists a partner nobody can
				see any more.
			*/
		bool standsInProject() const;

		int groupIndexForElement(Element *elmt) const;
		void setGroupIndexForElement(Element *elmt, int index);
			/**
				@brief Element::linkedSlaveForIo
				@param io_idx index of a PLC contact group of this master
				@return the slave drawn in that contact group, or nullptr
				when no linked slave stands in it any more. Both drawings
				of the PLC table (Element::drawPlcTable and
				CrossRefItem::drawAsPlcTable) ask here, so the contact
				they show and the contact they let click on are the same.
			*/
		Element *linkedSlaveForIo(int io_idx) const;
		void applyContactGroupLabels(Element *slave, int index) const;

		/**
		 * @brief linkType
		 * use elementData function instead
		 * @return
		 */
		virtual kind linkType() const {return m_link_type;} // return the linkable type
			/// A corner only turns a wire: QElectroTech's own corner
			/// symbol, or any symbol whose definition says so with the
			/// kind information "corner" set to "true"
		bool isCorner() const {return m_corner;}
			/// A T-junction drawn as its lines and a dot where they meet,
			/// instead of its own drawing
		bool isShownAsDot() const {return m_shown_as_dot;}
		void setShownAsDot(bool dot);
		std::optional<QPointF> dotPoint() const;
		/**
		 * @brief takesSchemeNumber
		 * Whether this element is given a number by an element numbering
		 * scheme (@sa ElementData::takesSchemeNumber): a slave, a report or
		 * a conductor definition is not numbered.
		 * @return true when the element numbering schemes apply to it
		 */
		bool takesSchemeNumber() const;
		/**
		 * @brief linkTypeToString
		 * use elementData function instead
		 * @return
		 */
		QString linkTypeToString() const;

		void newUuid() {setUuid(QUuid::createUuid());} 	//create new uuid for this element
		void setUuid(const QUuid &uuid);

	protected:
		void drawAxes(QPainter *, const QStyleOptionGraphicsItem *);
		void setSize(int, int);

	private:
		void drawHighlight(
				QPainter *,
				const QStyleOptionGraphicsItem *);
		bool buildFromXml(const QDomElement &, int * = nullptr);
		bool parseElement(const QDomElement &dom);
		bool parseInput(const QDomElement &dom_element);
		DynamicElementTextItem *parseDynamicText(
				const QDomElement &dom_element);
		Terminal *parseTerminal(const QDomElement &dom_element);
		void matchMovedTerminals(const QList<QDomElement> &records,
								 const QSet<int> &matched_records,
								 const QSet<Terminal *> &matched_terminals,
								 QHash<int, Terminal *> &id_adr);

	public:
		Terminal *terminalFromOldPlace(const QUuid &old_place_uuid) const;
		bool terminalWasMoved(const Terminal *terminal) const;

	private:
			/// How far, in x and y, a terminal may have moved from where
			/// a project saved it and still be recognised
		static constexpr qreal moved_terminal_reach = 5.0;
			/// Terminals paired with a saved one that was elsewhere,
			/// and the uuid derived from that old place
		QHash<QUuid, Terminal *> m_old_place_terminals;
		QSet<Terminal *> m_moved_terminals;

		//Reimplemented from QGraphicsItem
	public:
		void paint(
				QPainter *,
				const QStyleOptionGraphicsItem *,
				QWidget *) override;
		QRectF boundingRect() const override;
	protected:
		void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent(
				QGraphicsSceneMouseEvent *event) override;
		void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
		void hoverEnterEvent(QGraphicsSceneHoverEvent *) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override;
		QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

	protected:
			//ATTRIBUTES related to linked element
		QList <Element *> connected_elements;
		QList <LinkInfo>  tmp_uuids_link;
		bool m_corner = false;
		QHash <Element *, int> m_group_index_map;  ///< Maps linked elements to their group index (for slave->master links)
		QUuid             m_uuid;
		kind              m_link_type = Element::Simple;

			//ATTRIBUTES related to informations
		DiagramContext m_kind_informations;
		autonum::sequentialNumbers m_autoNum_seq;
		bool m_freeze_label = false;
		bool m_shown_as_dot = false;
		bool m_tool_placed = false;
			/// What reducerSizesText() gave last (refreshReducerSizes())
		QString m_reducer_sizes;
		void drawAsDot(QPainter *painter, const QPointF &meet) const;
		QHash<QUuid, QString> m_potential_sources;
		QString m_F_str;

	ElementsLocation m_location;
	QList <Terminal *> m_terminals;
	ElementOptions::Choices inkedChoices(const ElementOptions &options, const QString &family) const;
	const Conductor *leadingPipe() const;
	const Conductor *inkingPipe() const;
	QColor statusInk() const;
	void drawJunction(QPainter *painter, qreal detail) const;
	void drawFreeNozzles(QPainter *painter) const;
	void drawFlowDisagreement(QPainter *painter) const;
	void takePictures();

	QPicture m_picture;
	QPicture m_low_zoom_picture;
		/// The drawing with its texts kept readable, when the symbol's
		/// mirror or turn would not leave them so (m_readable_transform)
	QPicture m_readable_picture;
	QPicture m_readable_low_zoom_picture;
	QTransform m_readable_transform;
		/// What the drawings cover beyond the symbol's frame, if
		/// anything: see boundingRect()
	QRectF m_drawn_rect;
	void updateDrawnRect();
	ElementData m_data;
		/// See optionChoices()
	ElementOptions::Choices m_option_choices;
		/// See nozzleDiameters()
	NozzleDiameters m_nozzle_diameters;
		/// See nozzleConnections()
	NozzleConnections m_nozzle_connections;
		/// See nozzleMarks()
	NozzleMarks m_nozzle_marks;
		/// See unlinkedNozzles()
	NozzleLinks m_unlinked_nozzles;
	QList<QPointF> m_plc_table_positions;  // Positions of plc_table parts in the element definition

	void drawPlcTable(QPainter *painter);
	void setMirror(bool horizontal, bool vertical);
	void applyMirrorTransform();
	void keepChildrenReadable() const;

	public:
		/// Positions where the PLC IO table is drawn (from the .elmt file).
		QList<QPointF> plcTablePositions() const { return m_plc_table_positions; }

	private:
		void storeInformation();
		projectDataBase *placedStore() const;
		void appendTextsXml(QDomDocument &document, QDomElement &element) const;
		void appendDataXml(QDomDocument &document, QDomElement &element) const;
		void scheduleTextsStore();
		void connectTextStore(DynamicElementTextItem *deti);
		void connectTextStore(ElementTextItemGroup *group);
			/// Set while the texts are being written: taking a group's
			/// texts out to write them moves them, which is no change
		mutable bool m_writing_texts = false;
		bool m_must_highlight = false;
		int m_jump_highlight = 0;  ///< pending jump-target highlights, see showAsJumpTarget()
		bool m_jump_cursor = false;
		QSize   dimensions;
		QPoint  hotspot_coord;
		bool m_mouse_over = false;
		bool m_horizontal_mirror = false;
		bool m_vertical_mirror = false;
		QString m_prefix;
		QList <DynamicElementTextItem *> m_dynamic_text_list;
		QList <ElementTextItemGroup *> m_texts_group;

};

bool comparPos(const Element * elmt1, const Element * elmt2);

inline bool Element::isFree() const
{
	return linkedElements().isEmpty();
}

/**
	Indicate the current orientation of this element
	O = 0°
	1 = 90°
	2 = 180°
	3 = 270°
	@return the current orientation of this element
*/
inline int Element::orientation() const
{
	return(QET::correctAngle(rotation())/90);
}

/**
	@brief Element::uuid
	@return the uuid of this element
*/
inline QUuid Element::uuid() const
{return m_uuid;}

/**
	@brief Element::linkedElements
	@return the list of linked elements, sorted by position. A sorted copy:
	the element's own list keeps the order it is saved in.

	Only the partners which still stand on a folio of their own project
	are in it -- see standsInProject(). The element's own list keeps the
	ones which are away, so undo() finds them again; they are simply not
	counted, listed or written out until their folio comes back.
*/
inline QList <Element *> Element::linkedElements() const
{
	QList <Element *> sorted;
	sorted.reserve(connected_elements.size());
	for (Element *elmt : connected_elements)
		if (elmt && elmt->standsInProject())
			sorted << elmt;
	std::sort(sorted.begin(), sorted.end(), comparPos);
	return sorted;
}

/**
	@brief Element::allLinkedElements
	@return every partner this element keeps, sorted by position like
	linkedElements() returns them, the partners whose folio left the
	project included.
*/
inline QList <Element *> Element::allLinkedElements () const
{
	QList <Element *> all = connected_elements;
	std::sort(all.begin(), all.end(), comparPos);
	return all;
}

#endif

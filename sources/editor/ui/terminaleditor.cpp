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
#include "terminaleditor.h"
#include "ui_terminaleditor.h"
#include <qeticons.h>
#include <qet.h>
#include <editor/graphicspart/partterminal.h>
#include <QPropertyUndoCommand/qpropertyundocommand.h>
#include <ui/alignmenttextdialog.h>

#include <QColorDialog>
#include <QFontDialog>
#include <QToolTip>
#include <editor/elementscene.h>
#include <ui/diameterlist.h>
#include "qetelementeditor.h"

namespace {
	/**
		@brief sizesOfKind
		@return the sizes a symbol offers for the kind @p kind of
		PipeDiameter::Kind: the DN of ISO 6708, the pipe threads, and no list
		for an outside diameter, which may be any
	*/
QList<PipeDiameter> sizesOfKind(int kind)
{
	switch (kind) {
		case PipeDiameter::Outside: return {};
		case PipeDiameter::Thread:  return PipeDiameter::threadSeries();
		default:                    return DiameterList::isoSizes();
	}
}
}

/**
 * @brief TerminalEditor::TerminalEditor
 * Default constructor
 * @param editor : element editor of which this terminal editor belong
 * @param parent : parent widget
 */
TerminalEditor::TerminalEditor(QETElementEditor *editor, QWidget *parent) :
	ElementItemEditor(editor, parent),
	ui(new Ui::TerminalEditor)
{
	ui->setupUi(this);

#ifdef BUILD_WITHOUT_KF
	m_color_pb = new QPushButton(this);
	m_color_pb->setMinimumSize(40, 24);
	connect(m_color_pb, &QPushButton::clicked, this, &TerminalEditor::labelColorClicked);
#else
	m_color_pb = new KColorButton(this);
	m_color_pb->setMinimumSize(40, 24);
	connect(m_color_pb, &KColorButton::changed, this, &TerminalEditor::labelColorClicked);
#endif

	QLayout *layout = ui->m_color_widget->parentWidget()->layout();
	layout->replaceWidget(ui->m_color_widget, m_color_pb);
	delete ui->m_color_widget;
	ui->m_color_widget = nullptr;

	init();
}

/**
 * @brief TerminalEditor::~TerminalEditor
 * Destructor
 */
TerminalEditor::~TerminalEditor()
{
	delete ui;
}

/**
 * @brief TerminalEditor::updateForm
 * Reimplemented from ElementItemEditor
 * Update the content of this widget
 */
void TerminalEditor::updateForm()
{
	if (!m_part) {
		return;
	}
	activeConnections(false);

	ui->m_x_dsb->setValue(m_part->property("x").toReal());
	ui->m_y_dsb->setValue(m_part->property("y").toReal());
	ui->m_orientation_cb->setCurrentIndex(ui->m_orientation_cb->findData(m_part->property("orientation")));
	ui->m_name_le->setText(m_part->terminalName());
	ui->m_type_cb->setCurrentIndex(ui->m_type_cb->findData(m_part->terminalType()));
	ui->m_medium_cb->setCurrentIndex(ui->m_medium_cb->findData(m_part->medium()));
		//The type (terminal blocks, contacts) and the potential (function
		//separation of a terminal block) mean nothing for a terminal of
		//another medium: hidden, and kept, until it is electrical again
	const bool electrical = m_part->medium() == TerminalData::Electrical;
	ui->label_5->setVisible(electrical);
	ui->m_type_cb->setVisible(electrical);
	ui->label_potential->setVisible(electrical);
	ui->m_potential_le->setVisible(electrical);
	ui->m_potential_le->setText(m_part->potential());
		//Only a nozzle has a nominal diameter
	const bool nozzle = m_part->medium() == TerminalData::Pipe;
	ui->label_dn->setVisible(nozzle);
	ui->m_dn_cb->setVisible(nozzle);
	ui->m_dn_kind_cb->setVisible(nozzle);
	ui->label_connection->setVisible(nozzle);
	ui->m_connection_cb->setVisible(nozzle);
	ui->m_connection_cb->setCurrentIndex(ui->m_connection_cb->findData(int(m_part->connection())));
		//The kinds of size the connection takes
	fillKinds(m_part->connection());
		//The kind of a diameter given; else the one chosen last
	if (m_part->diameter().isSet()) {
		ui->m_dn_kind_cb->setCurrentIndex(ui->m_dn_kind_cb->findData(int(m_part->diameter().kind())));
	}
	DiameterList::fill(ui->m_dn_cb, sizesOfKind(ui->m_dn_kind_cb->currentData().toInt()),
					   PidCodification().locale());
	ui->m_dn_cb->lineEdit()->setText(m_part->diameterText());
		//A nozzle that follows its pipe has no size of its own: greyed, empty
	const bool follows = m_part->followsPipe();
	ui->m_dn_cb->setEnabled(!follows);
	ui->m_dn_kind_cb->setEnabled(!follows);
	if (follows) {
		ui->m_dn_cb->lineEdit()->clear();
	}
	if (!ui->m_dn_cb->property("ownTip").isValid()) {
		ui->m_dn_cb->setProperty("ownTip", ui->m_dn_cb->toolTip());
	}
	ui->m_dn_cb->setToolTip(follows
		? tr("This nozzle takes the size of its pipe: it has no size of its own. "
			 "See Edit the properties of the element, block Piping.")
		: ui->m_dn_cb->property("ownTip").toString());

	ui->m_show_name_cb->setChecked(m_part->showName());
	ui->m_label_x_dsb->setValue(m_part->labelPos().x());
	ui->m_label_y_dsb->setValue(m_part->labelPos().y());
	ui->m_font_pb->setText(m_part->labelFont().family());
	ui->m_label_size_sb->setValue(m_part->labelFont().pointSize());
	ui->m_label_rotation_sb->setValue(static_cast<int>(m_part->labelRotation()));
	ui->m_label_frame_cb->setChecked(m_part->labelFrame());

#ifdef BUILD_WITHOUT_KF
	QPixmap px(16, 16);
	px.fill(m_part->labelColor());
	m_color_pb->setIcon(QIcon(px));
#else
	m_color_pb->setColor(m_part->labelColor());
#endif

	ui->m_text_props_gb->setEnabled(m_part->showName());

	// Update master label fields
	bool is_slave = updateMasterLabelVisibility();
	if (is_slave) {
		PartTerminal *pt = m_part;
		if (pt) {
			ui->m_use_master_label_cb->setChecked(pt->useMasterLabel());
			ui->m_master_label_cb->setEnabled(pt->useMasterLabel());
			ui->m_name_le->setEnabled(!pt->useMasterLabel());
			int idx = ui->m_master_label_cb->findData(pt->masterLabelIndex());
			if (idx >= 0) {
				ui->m_master_label_cb->setCurrentIndex(idx);
			}
			// Show T-label in name field when master label is active
			if (pt->useMasterLabel()) {
				int label_idx = pt->masterLabelIndex();
				ui->m_name_le->setText(tr("T%1").arg(label_idx + 1));
			}
		}
	}

	activeConnections(true);
}

/**
 * @brief TerminalEditor::setPart
 * Set the part to edit.
 * The part must be a PartTerminal, in other case return false.
 * @param new_part : the part to edit
 * @return true if the part can be edited.
 */
bool TerminalEditor::setPart(CustomElementPart *new_part)
{
	if (m_part == new_part) {
		return true;
	}

	activeChangeConnections(false);

	if (!new_part)
	{
		m_part = nullptr;
		return(true);
	}

	if (PartTerminal *part_terminal = dynamic_cast<PartTerminal *>(new_part))
	{
		m_part = part_terminal;
		updateForm();
		activeChangeConnections(true);
		return(true);
	}
	return(false);
}

/**
 * @brief TerminalEditor::currentPart
 * @return the current edited part
 * or nullptr if there is no part or several part
 * @see QList<CustomElementPart *> TerminalEditor::currentParts() const
 */
CustomElementPart *TerminalEditor::currentPart() const
{
	return m_part;
}

/**
 * @brief TerminalEditor::init
 * Some init about this class
 */
void TerminalEditor::init()
{
	ui->m_orientation_cb->addItem(QET::Icons::North, tr("North"),  Qet::North);
	ui->m_orientation_cb->addItem(QET::Icons::East,  tr("East"),   Qet::East);
	ui->m_orientation_cb->addItem(QET::Icons::South, tr("South"),   Qet::South);
	ui->m_orientation_cb->addItem(QET::Icons::West,  tr("West"), Qet::West);

	ui->m_type_cb->addItem(tr("Generic"),         TerminalData::Generic);
	ui->m_type_cb->addItem(tr("Indoor terminal block"), TerminalData::Inner);
	ui->m_type_cb->addItem(tr("External terminal block"), TerminalData::Outer);
	ui->m_type_cb->addItem(tr("NO (contact SW)"),   TerminalData::No);
	ui->m_type_cb->addItem(tr("NC (contact SW)"),   TerminalData::Nc);
	ui->m_type_cb->addItem(tr("Common (contact SW)"), TerminalData::Common);

	ui->m_medium_cb->addItem(tr("Electrical", "terminal medium"),      TerminalData::Electrical);
	ui->m_medium_cb->addItem(tr("Pipe (nozzle)", "terminal medium"),   TerminalData::Pipe);
	ui->m_medium_cb->addItem(tr("Signal", "terminal medium"),          TerminalData::Signal);
	ui->m_medium_cb->addItem(tr("Drive (shaft)", "terminal medium"),   TerminalData::Drive);

	for (PipeConnection::Type type : PipeConnection::all()) {
		ui->m_connection_cb->addItem(PipeConnection::name(type), int(type));
	}
	DiameterList::setUp(ui->m_dn_cb, DiameterList::isoSizes(), PidCodification().locale());
	fillKinds(PipeConnection::Flange);

	ui->m_text_props_gb->setEnabled(false);

	// Populate master label dropdown (T1-T20)
	for (int i = 1; i <= 20; ++i) {
		ui->m_master_label_cb->addItem(tr("T%1").arg(i), i - 1);
	}

	// Check if parent element is a Slave to show/hide master label group
	updateMasterLabelVisibility();
}

/**
 * @brief TerminalEditor::posEdited
 */
void TerminalEditor::posEdited()
{
	if (m_locked) {
		return;
	}
	m_locked = true;

	QPointF new_pos(ui->m_x_dsb->value(),
					ui->m_y_dsb->value());

	if (m_part->pos() != new_pos)
	{
		auto undo = new QPropertyUndoCommand(m_part, "pos", m_part->property("pos"), new_pos);
		undo->setText(tr("Move a terminal"));
		undo->setAnimated(true, false);
		undoStack().push(undo);
	}

	m_locked = false;
}

/**
 * @brief TerminalEditor::orientationEdited
 */
void TerminalEditor::orientationEdited()
{
	if (m_locked) {
		return;
	}
	m_locked = true;

	auto ori_ = ui->m_orientation_cb->currentData();
	if (m_part->orientation() != ori_)
	{
		auto undo = new QPropertyUndoCommand(m_part, "orientation", m_part->property("orientation"), ori_);
		undo->setText(tr("Change the orientation of a terminal"));
		undoStack().push(undo);
	}

	m_locked = false;
}

/**
 * @brief TerminalEditor::nameEdited
 */
void TerminalEditor::nameEdited()
{
	if (m_locked) {
		return;
	}

	m_locked = true;
	QString name_(ui->m_name_le->text());

	if (m_part->terminalName() != name_)
	{
		auto undo = new QPropertyUndoCommand(m_part, "terminal_name", m_part->property("terminal_name"), name_);
		undo->setText(tr("Change the terminal name"));
		undoStack().push(undo);
	}
	m_locked=false;
}

void TerminalEditor::potentialEdited()
{
	if (m_locked) {
		return;
	}

	m_locked = true;
	QString potential_(ui->m_potential_le->text());

	if (m_part->potential() != potential_)
	{
		auto undo = new QPropertyUndoCommand(m_part, "potential", m_part->property("potential"), potential_);
		undo->setText(tr("Change the potential of a terminal"));
		undoStack().push(undo);
	}
	m_locked = false;
}

/**
 * @brief TerminalEditor::typeEdited
 */
void TerminalEditor::typeEdited()
{
	if (m_locked) {
		return;
	}
	m_locked = true;

	auto type = ui->m_type_cb->currentData();
	if (type != m_part->terminalType()) {
		auto undo = new QPropertyUndoCommand(m_part, "terminal_type", m_part->terminalType(), type);
		undo->setText(tr("Change the type of a terminal"));
		undoStack().push(undo);
	}
	m_locked = false;
}

/**
	@brief TerminalEditor::dnEdited
	The diameter of a nozzle, the default of the placed symbols. A symbol
	belongs to no project: a DN of ISO 6708, or any outside diameter; a number
	alone is a DN. Anything else gives the field its former value back.
*/
void TerminalEditor::dnEdited()
{
	if (m_locked) {
		return;
	}
	m_locked = true;

	bool ok = false;
	const QString typed = ui->m_dn_cb->lineEdit()->text().trimmed();
		//A number alone is of the kind chosen beside it
	const auto kind = PipeDiameter::Kind(ui->m_dn_kind_cb->currentData().toInt());
	PipeDiameter diameter = PipeDiameter::fromText(typed, kind, &ok);
	if (ok && !PipeConnection::allows(m_part->connection(), diameter))
	{
		ui->m_dn_cb->lineEdit()->setText(m_part->diameterText());
		QToolTip::showText(ui->m_dn_cb->mapToGlobal(QPoint(0, ui->m_dn_cb->height())),
						   tr("%1 does not name a nozzle of this connection (%2).")
						   .arg(typed, PipeConnection::name(m_part->connection())),
						   ui->m_dn_cb);
		m_locked = false;
		return;
	}
	if (ok && !PidCodification().acceptsForNozzle(diameter))
	{
			//A DN which is none of ISO 6708, a thread none of the table:
			//the nearest one
		diameter = PidCodification::nearest(diameter, PidCodification().nozzleSizes());
		ok = diameter.isSet();
		if (ok) {
			QToolTip::showText(ui->m_dn_cb->mapToGlobal(QPoint(0, ui->m_dn_cb->height())),
							   (diameter.isThread() ? tr("%1 → %2: the nearest pipe thread.")
													: tr("%1 → %2: the nearest DN of ISO 6708."))
							   .arg(typed, diameter.text(PidCodification().locale())),
							   ui->m_dn_cb);
		}
	}
	if (!ok)
	{
		ui->m_dn_cb->lineEdit()->setText(m_part->diameterText());
		QToolTip::showText(ui->m_dn_cb->mapToGlobal(QPoint(0, ui->m_dn_cb->height())),
						   tr("%1 is not a diameter: a DN of ISO 6708, an outside diameter "
							  "such as 88,9 with Ø chosen beside, or a pipe thread such as "
							  "G 1\"1/2.").arg(typed),
						   ui->m_dn_cb);
	}
	else if (m_part->diameter() != diameter)
	{
		auto undo = new QPropertyUndoCommand(m_part, "diameter", m_part->diameterText(),
											 diameter.text(QLocale::c()));
		undo->setText(tr("Change the diameter of a nozzle"));
		undoStack().push(undo);
	}
		//Written as the lists write it
	ui->m_dn_cb->lineEdit()->setText(m_part->diameterText());
	m_locked = false;
}

/**
	@brief TerminalEditor::fillKinds
	Offer beside the diameter the kinds of size a nozzle of @p connection is
	named by (PipeConnection::kinds()), keeping the one chosen if it is one
*/
void TerminalEditor::fillKinds(PipeConnection::Type connection)
{
	const QSignalBlocker blocker(ui->m_dn_kind_cb);
	const QVariant chosen = ui->m_dn_kind_cb->currentData();
	ui->m_dn_kind_cb->clear();
	for (PipeDiameter::Kind kind : PipeConnection::kinds(connection)) {
		ui->m_dn_kind_cb->addItem(kind == PipeDiameter::Outside ? QString(PipeDiameter::outsideSign())
								  : kind == PipeDiameter::Thread ? QString(PipeDiameter::threadSign())
																 : QStringLiteral("DN"),
								  int(kind));
	}
	const int index = ui->m_dn_kind_cb->findData(chosen);
	ui->m_dn_kind_cb->setCurrentIndex(index < 0 ? 0 : index);
}

/**
	@brief TerminalEditor::connectionEdited
	How a pipe is joined to the nozzle. A diameter of a kind the new
	connection does not take is changed in the same undo step: a DN and a
	thread into each other by the fixed table; any other removed, as a symbol
	belongs to no project, whose table could convert it.
*/
void TerminalEditor::connectionEdited()
{
	if (m_locked) {
		return;
	}
	const auto connection = PipeConnection::Type(ui->m_connection_cb->currentData().toInt());
	if (connection == m_part->connection()) {
		return;
	}
	m_locked = true;
	undoStack().beginMacro(tr("Change the connection of a nozzle"));
	undoStack().push(new QPropertyUndoCommand(m_part, "connection", m_part->connectionWord(),
											  PipeConnection::toString(connection)));
	const PipeDiameter fitted = PipeConnection::fitted(connection, m_part->diameter());
	if (fitted != m_part->diameter())
	{
		const QLocale locale = PidCodification().locale();
		QToolTip::showText(ui->m_dn_cb->mapToGlobal(QPoint(0, ui->m_dn_cb->height())),
						   fitted.isSet()
						   ? tr("%1 → %2: the size the table of the pipe threads joins to it.")
							 .arg(m_part->diameter().text(locale), fitted.text(locale))
						   : tr("%1 does not name a nozzle of this connection (%2): removed.")
							 .arg(m_part->diameterText(), PipeConnection::name(connection)),
						   ui->m_dn_cb);
		undoStack().push(new QPropertyUndoCommand(m_part, "diameter", m_part->diameterText(),
												  fitted.text(QLocale::c())));
	}
	undoStack().endMacro();
	m_locked = false;
	updateForm();
}

/**
	@brief TerminalEditor::mediumEdited
	The medium the terminal connects: electrical, or for a process symbol a
	pipe, a signal or a drive
*/
void TerminalEditor::mediumEdited()
{
	if (m_locked) {
		return;
	}
	m_locked = true;

	const auto medium = ui->m_medium_cb->currentData();
	if (medium != m_part->medium())
	{
			//A name that follows the convention of another medium (E1 for
			//a drive turned into a nozzle) or no name takes the first free
			//name of the new medium, in the same undo step
		QStringList taken;
		if (elementEditor() && elementEditor()->elementScene()) {
			for (QGraphicsItem *item : elementEditor()->elementScene()->items()) {
				auto *other = qgraphicsitem_cast<PartTerminal *>(item);
				if (other && other != m_part) {
					taken << other->terminalName();
				}
			}
		}
		const QString name = TerminalData::suggestedName(
					TerminalData::Medium(medium.toInt()),
					m_part->terminalName(), taken);

		undoStack().beginMacro(tr("Change the medium of a terminal"));
		undoStack().push(new QPropertyUndoCommand(m_part, "medium",
							  m_part->medium(), medium));
		if (name != m_part->terminalName()) {
			undoStack().push(new QPropertyUndoCommand(m_part, "terminal_name",
								  m_part->terminalName(), name));
		}
		undoStack().endMacro();
	}
	m_locked = false;
}

/**
 * @brief TerminalEditor::activeConnections
 * Active connection between the widgets used in this editor
 * and method of this class.
 * @param active
 */

void TerminalEditor::showNameEdited()
{
	if (m_locked) return;
	m_locked = true;

	bool show = ui->m_show_name_cb->isChecked();
	if (m_part->showName() != show) {
		auto undo = new QPropertyUndoCommand(m_part, "show_name", m_part->showName(), show);
		undo->setText(tr("Show/hide the terminal name"));
		undoStack().push(undo);
	}
	ui->m_text_props_gb->setEnabled(show);
	m_locked = false;
}

void TerminalEditor::labelPosEdited()
{
	if (m_locked) return;
	m_locked = true;

	QPointF new_pos(ui->m_label_x_dsb->value(), ui->m_label_y_dsb->value());
	if (m_part->labelPos() != new_pos) {
		auto undo = new QPropertyUndoCommand(m_part, "label_pos", m_part->labelPos(), new_pos);
		undo->setText(tr("Change the position of the label"));
		undoStack().push(undo);
	}
	m_locked = false;
}

void TerminalEditor::labelFontClicked()
{
	if (m_locked) return;
	m_locked = true;

	bool ok;
	QFont font = QFontDialog::getFont(&ok, m_part->labelFont(), this, QString(),
									  QFontDialog::DontUseNativeDialog);
	if (ok && font != m_part->labelFont()) {
		ui->m_font_pb->setText(font.family());
		ui->m_label_size_sb->blockSignals(true);
		ui->m_label_size_sb->setValue(font.pointSize());
		ui->m_label_size_sb->blockSignals(false);

		auto undo = new QPropertyUndoCommand(m_part, "label_font", m_part->labelFont(), font);
		undo->setText(tr("Change the label font"));
		undoStack().push(undo);
	}
	m_locked = false;
}

void TerminalEditor::labelSizeEdited()
{
	if (m_locked) return;
	m_locked = true;

	QFont new_font = m_part->labelFont();
	new_font.setPointSize(ui->m_label_size_sb->value());
	if (m_part->labelFont() != new_font) {
		auto undo = new QPropertyUndoCommand(m_part, "label_font", m_part->labelFont(), new_font);
		undo->setText(tr("Change the font size of the label"));
		undoStack().push(undo);
	}
	m_locked = false;
}

void TerminalEditor::labelRotationEdited()
{
	if (m_locked) return;
	m_locked = true;

	qreal rot = static_cast<qreal>(ui->m_label_rotation_sb->value());
	if (!qFuzzyCompare(m_part->labelRotation(), rot)) {
		auto undo = new QPropertyUndoCommand(m_part, "label_rotation", m_part->labelRotation(), rot);
		undo->setText(tr("Change the rotation of the label"));
		undoStack().push(undo);
	}
	m_locked = false;
}

void TerminalEditor::labelAlignClicked()
{
	Qt::Alignment align = m_part->labelHAlignment() | m_part->labelVAlignment();
	AlignmentTextDialog dialog(align, this);
	if (dialog.exec() == QDialog::Accepted) {
		Qt::Alignment new_align = dialog.alignment();
		Qt::Alignment new_h = new_align & Qt::AlignHorizontal_Mask;
		Qt::Alignment new_v = new_align & Qt::AlignVertical_Mask;

		if (new_h != m_part->labelHAlignment()) {
			auto undo = new QPropertyUndoCommand(m_part, "label_halignment",
				QVariant::fromValue(m_part->labelHAlignment()), QVariant::fromValue(new_h));
			undo->setText(tr("Change the alignment of the label"));
			undoStack().push(undo);
		}
		if (new_v != m_part->labelVAlignment()) {
			auto undo = new QPropertyUndoCommand(m_part, "label_valignment",
				QVariant::fromValue(m_part->labelVAlignment()), QVariant::fromValue(new_v));
			undo->setText(tr("Change the alignment of the label"));
			undoStack().push(undo);
		}
	}
}

void TerminalEditor::labelFrameEdited()
{
	if (m_locked) return;
	m_locked = true;

	bool frame = ui->m_label_frame_cb->isChecked();
	if (m_part->labelFrame() != frame) {
		auto undo = new QPropertyUndoCommand(m_part, "label_frame", m_part->labelFrame(), frame);
		undo->setText(tr("Show/hide the label border"));
		undoStack().push(undo);
	}
	m_locked = false;
}

void TerminalEditor::labelColorClicked()
{
	if (m_locked) return;
	m_locked = true;

#ifdef BUILD_WITHOUT_KF
	QColor new_color = QColorDialog::getColor(m_part->labelColor(), this, QString(),
											QColorDialog::DontUseNativeDialog);
	if (new_color.isValid() && m_part->labelColor() != new_color) {
		auto undo = new QPropertyUndoCommand(m_part, "label_color", m_part->labelColor(), new_color);
		undo->setText(tr("Change the label colour"));
		undoStack().push(undo);
	}
#else
	QColor new_color = m_color_pb->color();
	if (new_color.isValid() && m_part->labelColor() != new_color) {
		auto undo = new QPropertyUndoCommand(m_part, "label_color", m_part->labelColor(), new_color);
		undo->setText(tr("Change the label colour"));
		undoStack().push(undo);
	}
#endif
	m_locked = false;
}

void TerminalEditor::activeConnections(bool active)
{
	if (active) {
		m_editor_connections << connect(ui->m_x_dsb, QOverload<qreal>::of(&QDoubleSpinBox::valueChanged),
										[this]() { TerminalEditor::posEdited(); ui->m_x_dsb->setFocus();} ) ;
		m_editor_connections << connect(ui->m_y_dsb, QOverload<qreal>::of(&QDoubleSpinBox::valueChanged),
										[this]() { TerminalEditor::posEdited(); ui->m_y_dsb->setFocus(); } ) ;
		m_editor_connections << connect(ui->m_orientation_cb,  QOverload<int>::of(&QComboBox::activated),
										this, &TerminalEditor::orientationEdited);
		m_editor_connections << connect(ui->m_name_le, &QLineEdit::editingFinished,
										this, &TerminalEditor::nameEdited);
		m_editor_connections << connect(ui->m_type_cb, QOverload<int>::of(&QComboBox::activated),
										this, &TerminalEditor::typeEdited);
		m_editor_connections << connect(ui->m_medium_cb, QOverload<int>::of(&QComboBox::activated),
										this, &TerminalEditor::mediumEdited);
		m_editor_connections << connect(ui->m_connection_cb, QOverload<int>::of(&QComboBox::activated),
										this, &TerminalEditor::connectionEdited);
		m_editor_connections << connect(ui->m_dn_cb->lineEdit(), &QLineEdit::editingFinished,
										this, &TerminalEditor::dnEdited);
		m_editor_connections << connect(ui->m_dn_cb, QOverload<int>::of(&QComboBox::activated),
										this, &TerminalEditor::dnEdited);
			//Another kind: its list, for the next size typed
		m_editor_connections << connect(ui->m_dn_kind_cb, QOverload<int>::of(&QComboBox::activated),
										this, [this]() {
			const QString text = ui->m_dn_cb->lineEdit()->text();
			DiameterList::fill(ui->m_dn_cb, sizesOfKind(ui->m_dn_kind_cb->currentData().toInt()),
							   PidCodification().locale());
			ui->m_dn_cb->lineEdit()->setText(text);
		});
		m_editor_connections << connect(ui->m_show_name_cb, &QCheckBox::toggled,
										this, &TerminalEditor::showNameEdited);
		m_editor_connections << connect(ui->m_label_x_dsb, QOverload<qreal>::of(&QDoubleSpinBox::valueChanged),
										[this]() { TerminalEditor::labelPosEdited(); ui->m_label_x_dsb->setFocus(); });
		m_editor_connections << connect(ui->m_label_y_dsb, QOverload<qreal>::of(&QDoubleSpinBox::valueChanged),
										[this]() { TerminalEditor::labelPosEdited(); ui->m_label_y_dsb->setFocus(); });
		m_editor_connections << connect(ui->m_font_pb, &QPushButton::clicked,
										this, &TerminalEditor::labelFontClicked);
		m_editor_connections << connect(ui->m_label_size_sb, QOverload<int>::of(&QSpinBox::valueChanged),
										[this]() { TerminalEditor::labelSizeEdited(); ui->m_label_size_sb->setFocus(); });
		m_editor_connections << connect(ui->m_label_rotation_sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
										[this]() { TerminalEditor::labelRotationEdited(); ui->m_label_rotation_sb->setFocus(); });
		m_editor_connections << connect(ui->m_align_pb, &QPushButton::clicked,
										this, &TerminalEditor::labelAlignClicked);
		m_editor_connections << connect(ui->m_label_frame_cb, &QCheckBox::toggled,
										this, &TerminalEditor::labelFrameEdited);
		m_editor_connections << connect(ui->m_potential_le, &QLineEdit::editingFinished,
								this, &TerminalEditor::potentialEdited);
		m_editor_connections << connect(ui->m_use_master_label_cb, &QCheckBox::toggled,
										this, &TerminalEditor::useMasterLabelEdited);
		m_editor_connections << connect(ui->m_master_label_cb, QOverload<int>::of(&QComboBox::activated),
										this, &TerminalEditor::masterLabelIndexEdited);
	} else {
		for (auto const & con : std::as_const(m_editor_connections)) {
			QObject::disconnect(con);
		}
		m_editor_connections.clear();
	}
}

void TerminalEditor::activeChangeConnections(bool active)
{
	if (active)
	{
		m_change_connections << connect(m_part, &PartTerminal::xChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::yChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::orientationChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::nameChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::terminalTypeChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::mediumChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::diameterChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::connectionChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::showNameChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelPosChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelFontChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelRotationChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelHAlignmentChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelVAlignmentChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelFrameChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::labelColorChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::potentialChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::useMasterLabelChanged, this, &TerminalEditor::updateForm);
		m_change_connections << connect(m_part, &PartTerminal::masterLabelIndexChanged, this, &TerminalEditor::updateForm);
	} else {
		for (auto &con : m_change_connections) {
			QObject::disconnect(con);
		}
		m_change_connections.clear();
	}
}

void TerminalEditor::useMasterLabelEdited()
{
	if (m_locked) return;
	m_locked = true;

	bool use = ui->m_use_master_label_cb->isChecked();
	ui->m_master_label_cb->setEnabled(use);

	QSignalBlocker name_blocker(ui->m_name_le);

	if (m_part->useMasterLabel() != use) {
		auto undo = new QPropertyUndoCommand(m_part, "use_master_label",
			m_part->useMasterLabel(), use);
		undo->setText(tr("Edit the master label"));
		undoStack().push(undo);
	}

	if (use) {
		int idx = ui->m_master_label_cb->currentData().toInt();
		QString t_label = tr("T%1").arg(idx + 1);
		ui->m_name_le->setText(t_label);
		ui->m_name_le->setEnabled(false);
		if (m_part->terminalName() != t_label) {
			auto undo = new QPropertyUndoCommand(m_part, "terminal_name",
				m_part->terminalName(), t_label);
			undo->setText(tr("Change the name of the terminal"));
			undoStack().push(undo);
		}
	} else {
		ui->m_name_le->setEnabled(true);
		if (!m_part->terminalName().isEmpty()) {
			auto undo = new QPropertyUndoCommand(m_part, "terminal_name",
				m_part->terminalName(), QString());
			undo->setText(tr("Change the name of the terminal"));
			undoStack().push(undo);
		}
		ui->m_name_le->clear();
	}

	m_locked = false;
}

void TerminalEditor::masterLabelIndexEdited()
{
	if (m_locked) return;
	m_locked = true;

	int idx = ui->m_master_label_cb->currentData().toInt();

	if (m_part->masterLabelIndex() != idx) {
		auto undo = new QPropertyUndoCommand(m_part, "master_label_index",
			m_part->masterLabelIndex(), idx);
		undo->setText(tr("Change the master label index"));
		undoStack().push(undo);
	}

	if (ui->m_use_master_label_cb->isChecked()) {
		QString t_label = tr("T%1").arg(idx + 1);
		ui->m_name_le->setText(t_label);
		if (m_part->terminalName() != t_label) {
			auto undo = new QPropertyUndoCommand(m_part, "terminal_name",
				m_part->terminalName(), t_label);
			undo->setText(tr("Change the name of the terminal"));
			undoStack().push(undo);
		}
	}

	m_locked = false;
}

bool TerminalEditor::updateMasterLabelVisibility()
{
	QETElementEditor *editor = elementEditor();
	if (!editor || !editor->elementScene()) {
		ui->m_master_label_gb->setVisible(false);
		return false;
	}

	ElementData data = editor->elementScene()->elementData();
	bool is_slave = (data.m_type == ElementData::Slave);
	ui->m_master_label_gb->setVisible(is_slave);
	return is_slave;
}

void TerminalEditor::refreshMasterLabelVisibility()
{
	updateMasterLabelVisibility();
	if (m_part) {
		updateForm();
	}
}

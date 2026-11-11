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
#ifndef PIPINGBLOCKWIDGET_H
#define PIPINGBLOCKWIDGET_H

#include <properties/elementdata.h>

#include <QGroupBox>

class QCheckBox;
class QComboBox;

/**
	@brief The PipingBlockWidget class
	The block "Piping" of the element editor: the family of the symbol, the
	case "in-line symbol" and one case per nozzle for the run of the line (the
	nozzles whose size follows the pipe, and through which a change of size
	goes), and the role of the symbol for the lines (pid_role: a reducer).
	Shown in the panel Informations, when no part is selected.
*/
class PipingBlockWidget : public QGroupBox
{
	Q_OBJECT

	public:
		explicit PipingBlockWidget(QWidget *parent = nullptr);

			/// Show @p data and the nozzles of the symbol, without emitting
			/// edited(). The block is hidden for a symbol that has no nozzle
			/// and none of these data.
		void setData(const ElementData &data, const QStringList &nozzles);
			/// @return @p data with the family, the in-line case and the run
			/// of this block. A run equal to the one @p data already gave (an
			/// in-line symbol without a run: all its nozzles) is left as it was.
		ElementData applyTo(ElementData data) const;

	signals:
			/// The user changed something in the block
		void edited();

	private:
		QComboBox *m_family_cb = nullptr;
		QComboBox *m_role_cb = nullptr;
		QCheckBox *m_inline_cb = nullptr;
		QWidget *m_run_widget = nullptr;
		QList<QCheckBox *> m_run_cbs;
		QStringList m_nozzles;
		ElementData m_data;
		bool m_locked = false;
};

#endif // PIPINGBLOCKWIDGET_H

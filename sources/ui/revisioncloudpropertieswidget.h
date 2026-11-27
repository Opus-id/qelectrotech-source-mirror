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
#ifndef REVISIONCLOUDPROPERTIESWIDGET_H
#define REVISIONCLOUDPROPERTIESWIDGET_H

#include "../PropertiesEditor/propertieseditorwidget.h"

#include <QPointer>

class KColorButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class RevisionCloudItem;

/**
	@brief The RevisionCloudPropertiesWidget class
	Edits a RevisionCloudItem: its kind; the index of a revision, offered
	from the project's list of revisions; the comment, author, date, state
	and reply of a review note; its line and the size of its text and arcs.
	One undo step for each Apply.
*/
class RevisionCloudPropertiesWidget : public PropertiesEditorWidget
{
		Q_OBJECT

	public:
		explicit RevisionCloudPropertiesWidget(RevisionCloudItem *item, QWidget *parent = nullptr);

		void apply() override;
		void reset() override;
		void updateUi() override;
		QString title() const override;
		bool setLiveEdit(bool live_edit) override;

			/// The index typed or chosen
		QString revisionText() const;

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:
		void showRowsOfKind();

		QPointer<RevisionCloudItem> m_item;
		QFormLayout *m_layout = nullptr;
		QComboBox *m_kind = nullptr;
		QComboBox *m_revision = nullptr;
		QPlainTextEdit *m_comment = nullptr;
		QLineEdit *m_author = nullptr;
		QLineEdit *m_date = nullptr;
		QCheckBox *m_done = nullptr;
		QLineEdit *m_reply = nullptr;
		KColorButton *m_color = nullptr;
		QDoubleSpinBox *m_width = nullptr;
		QComboBox *m_style = nullptr;
		QDoubleSpinBox *m_label_size = nullptr;
		QDoubleSpinBox *m_arc_size = nullptr;
		QList<QMetaObject::Connection> m_edit_connections;
		bool m_updating = false;
		double m_label_size_shown = 0;   ///< what updateUi() put in m_label_size
		double m_arc_size_shown = 0;     ///< the same for m_arc_size
		double m_width_shown = 0;        ///< the same for m_width
};

#endif // REVISIONCLOUDPROPERTIESWIDGET_H

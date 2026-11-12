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
#include "pipingblockwidget.h"

#include <pidcodification.h>
#include <pidtooltip.h>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>

PipingBlockWidget::PipingBlockWidget(QWidget *parent) :
	QGroupBox(tr("Piping"), parent)
{
	auto *grid = new QGridLayout(this);

	grid->addWidget(new QLabel(tr("Family:"), this), 0, 0);
	m_family_cb = new QComboBox(this);
	m_family_cb->setToolTip(PidToolTip::wrapped(tr("Family of the symbol. \"pipe\": a free end or a vent, its only "
							   "nozzle takes the size of its pipe.")));
	grid->addWidget(m_family_cb, 0, 1);

	m_inline_cb = new QCheckBox(tr("In-line symbol (a line runs through it)"), this);
	m_inline_cb->setToolTip(PidToolTip::wrapped(tr("Without a run chosen below, all the nozzles take the size of "
							   "their pipe, and a change of size goes through the symbol.")));
	grid->addWidget(m_inline_cb, 1, 0, 1, 2);

	grid->addWidget(new QLabel(tr("Run of the line:"), this), 2, 0);
	m_run_widget = new QWidget(this);
	new QHBoxLayout(m_run_widget);
	m_run_widget->layout()->setContentsMargins(0, 0, 0, 0);
	grid->addWidget(m_run_widget, 2, 1);

	grid->addWidget(new QLabel(tr("Role:"), this), 3, 0);
	m_role_cb = new QComboBox(this);
	m_role_cb->setToolTip(PidToolTip::wrapped(tr("\"Reducer\": the size of the line changes at this symbol. Each of "
							 "its nozzles takes the size of its pipe or a size of its own, and a "
							 "change of size stops there. Tick no nozzle of the run of the line.")));
	grid->addWidget(m_role_cb, 3, 1);

	for (QComboBox *list : {m_family_cb, m_role_cb}) {
		connect(list, QOverload<int>::of(&QComboBox::activated), this, [this]() {
			if (!m_locked) {
				emit edited();
			}
		});
	}
		//In-line with no nozzle of the run ticked: all of them
	connect(m_inline_cb, &QCheckBox::toggled, this, [this](bool on) {
		if (m_locked) {
			return;
		}
		if (on) {
			bool any = false;
			for (const QCheckBox *cb : std::as_const(m_run_cbs)) {
				any = any || cb->isChecked();
			}
			if (!any) {
				for (QCheckBox *cb : std::as_const(m_run_cbs)) {
					cb->setChecked(true);
				}
			}
		}
		emit edited();
	});
}

void PipingBlockWidget::setData(const ElementData &data, const QStringList &nozzles)
{
	m_locked = true;
	m_data = data;
	m_nozzles = nozzles;

	m_family_cb->clear();
	m_family_cb->addItem(tr("None"), QString());
	for (const QString &family : PidCodification::objectFamilies()) {
		m_family_cb->addItem(family, family);
	}
		//A family the list does not know is kept as it is
	if (!data.m_pid_family.isEmpty() && m_family_cb->findData(data.m_pid_family) < 0) {
		m_family_cb->addItem(data.m_pid_family, data.m_pid_family);
	}
	m_family_cb->setCurrentIndex(m_family_cb->findData(data.m_pid_family));

	m_role_cb->clear();
	m_role_cb->addItem(tr("None"), QString());
	m_role_cb->addItem(tr("Reducer"), QStringLiteral("reducer"));
		//A role the list does not know is kept as it is
	if (!data.m_pid_role.isEmpty() && m_role_cb->findData(data.m_pid_role) < 0) {
		m_role_cb->addItem(data.m_pid_role, data.m_pid_role);
	}
	m_role_cb->setCurrentIndex(m_role_cb->findData(data.m_pid_role));

	m_inline_cb->setChecked(data.m_pipe_inline);

	qDeleteAll(m_run_cbs);
	m_run_cbs.clear();
	const QStringList effective = !data.m_pipe_run.isEmpty() ? data.m_pipe_run
								: (data.m_pipe_inline ? nozzles : QStringList());
	for (const QString &name : nozzles) {
		auto *cb = new QCheckBox(name, m_run_widget);
		cb->setChecked(effective.contains(name));
		cb->setToolTip(PidToolTip::wrapped(tr("This nozzle takes the size of its pipe and passes a change of size "
						  "to the other nozzles of the run.")));
		connect(cb, &QCheckBox::toggled, this, [this]() {
			if (!m_locked) {
				emit edited();
			}
		});
		m_run_widget->layout()->addWidget(cb);
		m_run_cbs << cb;
	}

	setVisible(!nozzles.isEmpty() || !data.m_pid_family.isEmpty()
			   || data.m_pipe_inline || !data.m_pipe_run.isEmpty() || !data.m_pid_role.isEmpty());
	m_locked = false;
}

ElementData PipingBlockWidget::applyTo(ElementData data) const
{
	QStringList checked;
	for (int i = 0; i < m_run_cbs.size(); ++i) {
		if (m_run_cbs.at(i)->isChecked()) {
			checked << m_nozzles.at(i);
		}
	}
	const QStringList before = !data.m_pipe_run.isEmpty() ? data.m_pipe_run
							 : (data.m_pipe_inline ? m_nozzles : QStringList());
	auto sameSet = [](QStringList a, QStringList b) {
		a.sort();
		b.sort();
		return a == b;
	};
	if (!sameSet(checked, before)) {
		data.m_pipe_run = checked;
	}
	data.m_pipe_inline = m_inline_cb->isChecked();
	data.m_pid_family = m_family_cb->currentData().toString();
	data.m_pid_role = m_role_cb->currentData().toString();
	return data;
}

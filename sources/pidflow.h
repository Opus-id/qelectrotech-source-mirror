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
#ifndef PIDFLOW_H
#define PIDFLOW_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class Conductor;
class Diagram;
class Element;
class Terminal;

/**
	@brief The PidFlow namespace
	The direction of flow of the pipes (step 8a-bis), found from the
	symbols each time it is asked, never written: a pipe has a first and a
	last terminal, and the flow goes one way or the other along it.

	What gives it: a nozzle whose symbol says the fluid comes in or goes out
	there (TerminalData::FlowRole: a flow arrow, a check valve, the suction
	and the discharge of a pump). It goes on through the symbols a line runs
	through (a run of two nozzles: a valve, an instrument, a mark on the
	line, an injection point straight on), through a reducer, and between
	two nozzles placed against each other, and through a chamber of two
	nozzles of equipment (a side of a heat exchanger, pid_chamber).
	Equipment stops it otherwise, but for its nozzles which have a role; a
	chamber of three nozzles or more is a vessel, which may fill or empty,
	and gives nothing. At a tee or a cross, the last nozzle whose
	direction is not known goes the other way when all the others come in,
	or all go out; otherwise nothing is found there.

	Several sources which do not agree, or a tee into which everything
	comes, are a disagreement: said, never refused nor corrected. A line
	used both ways in turn (a closed sand filter: production, then
	backwash) says so by the arrow of both ways: "both ways", never a
	disagreement, unless another source gives it one way only.
	Pipes only: a signal line has no direction here.
*/
namespace PidFlow
{
		/**
			@brief The Solver class
			The logic alone, kept free of any QGraphicsItem so it can be
			tested on its own. Its nodes are the nozzles: each one lets the
			fluid in to its symbol or out of it. Two nodes linked go opposite
			ways: a pipe takes out of one symbol what it brings into the
			other; through a valve, what comes in at one nozzle goes out at
			the other.
		*/
	class Solver
	{
		public:
			enum class State {
				Unknown,
				In,       ///< the fluid comes into the symbol there
				Out,      ///< the fluid goes out of the symbol there
				Both,     ///< both ways in turn (the arrow of both ways)
				Conflict  ///< sources which do not agree
			};

				/// A new node; its number
			int addNode();
				/// @p a and @p b go opposite ways
			void link(int a, int b);
				/// The fluid comes in at @p node (@p in), or goes out, as
				/// @p source says
			void fix(int node, bool in, int source);
				/// The fluid goes both ways in turn at @p node, as @p source
				/// says: its stretch too, unless another source gives it one
				/// way only, a disagreement
			void fixBoth(int node, int source);
				/// A tee, a cross, of the nozzles @p nodes; @p source names
				/// it among the sources of what it gives
			void addJunction(const QList<int> &nodes, int source);
				/// Find what can be found; call it once everything is given
			void solve();

			State state(int node) const;
				/// The sources of the direction of @p node: those which
				/// gave it to its stretch of line, tees included
			QList<int> sources(int node) const;
				/// The junction added @p index-th (from 0) has everything
				/// coming in, or everything going out; or so as soon as its
				/// only nozzle used both ways comes in, or goes out
			bool junctionInConflict(int index) const;
				/// The nozzles of the junction added @p index-th
			QList<int> junctionNodes(int index) const;
				/// The sources which give the stretch of a node @p source
				/// gave a direction another one: one way against the
				/// other, or one way only against both ways (step 8a-bis-2)
			QList<int> opposed(int source) const;
				/// @p source says both ways
			bool saysBoth(int source) const;

		private:
				/// The node which stands for the stretch of line of @p node;
				/// in @p parity, whether the two go opposite ways
			int find(int node, bool *parity) const;
			State stateOf(int node) const;

				/// For each node, another one of its stretch (itself for
				/// the one which stands for it), and whether they go
				/// opposite ways
			QList<int> m_parent;
			QList<bool> m_parity;
				/// Links which close a stretch on itself the wrong way
			QList<int> m_bad_links;

			struct Fix {
				int node;
				bool in;
				int source;
				bool both = false;
			};
			QList<Fix> m_fixes;

			struct Junction {
				QList<int> nodes;
				int source;
				bool conflict = false;
			};
			QList<Junction> m_junctions;

				/// What a source says of a stretch, from the node which
				/// stands for it
			enum class Way { In, Out, Both };
			struct Claim {
				int source;
				Way way;
			};
				/// What is known of a stretch, by the node which stands for
				/// it: the fluid comes in there, goes out, or both (a
				/// disagreement), and from which sources, each with what it
				/// says
			struct Group {
				bool in = false;
				bool out = false;
				bool both = false;
				QList<int> sources;
				QList<Claim> claims;
			};
			QHash<int, Group> m_groups;
	};

		/// The direction of flow along a pipe, from its first terminal to its
		/// last one (Forward) or the other way
	enum class Direction {
		Unknown,
		Forward,
		Backward,
		Both,     ///< both ways in turn (the arrow of both ways)
		Conflict
	};
		/// The direction of flow along @p pipe; Unknown for a signal line
		/// or a wire
	Direction direction(const Conductor *pipe);
		/// The direction of @p pipe as the user reads it: "from PC-01 (N2)
		/// to VC-03 (N1)", "both ways", "unknown" or "disagreement (FL-01,
		/// CV-02)", the symbols in cause; nothing for a signal line or a wire
	QString text(const Conductor *pipe);
		/// The way a nozzle is named in text(): the label of its symbol,
		/// else the name of the symbol, then the name of the nozzle
	QString nozzleName(const Terminal *nozzle);
		/// The way a symbol is named: its label, else its name
	QString symbolName(const Element *symbol);
		/// What the tool tip of @p symbol says of the directions of flow it
		/// is in disagreement with, one line each; nothing when it is in
		/// none (step 8a-bis-2). Its folio is worked out once, again only
		/// when a line or a symbol changed; then the symbols whose marks
		/// change are drawn again.
	QStringList disagreements(const Element *symbol);
		/// Work out the marks of @p folio again if a line or a symbol
		/// changed since: a pipe drawn calls it, so that the symbols of its
		/// line are drawn again when it joins them or leaves them
	void refreshMarks(const Diagram *folio);
}

#endif // PIDFLOW_H

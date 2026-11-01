# QElectroTech for P&ID drawings (experimental fork)

This branch, `pipe-connector`, is a **personal and experimental fork** of
[QElectroTech](https://qelectrotech.org) that adds what is needed to draw
**P&IDs** (piping and instrumentation diagrams), for example for water
treatment plants: pipe lines between the nozzles of process symbols, with
their own data.

**It is not official.** Nothing here has been reviewed or accepted by the
QElectroTech maintainers. The idea is discussed in
[Discussion #1475](https://github.com/qelectrotech/qelectrotech-source-mirror/discussions/1475);
comments are welcome there.

The goal is a tool **centred on the drawing**, not a data platform. A P&ID is
not an electrical diagram: a pipe line is not a wire. It has no potential, no
cable, no wire number. It has a nominal diameter, a material, a fluid.

## What works today

**Terminals that say what they take**
- A terminal of a symbol may be a **nozzle** (pipe line), a **signal**
  terminal or a **drive** terminal (shaft), besides the usual electrical
  terminal. In the element editor: terminal properties, *Connection:*
  *Electrical (wires)*, *Pipe line (nozzle)*, *Signal line*, *Drive (shaft)*.
- Saved as an optional `connection` attribute of `<terminal>` in the `.elmt`
  (`pipe`, `signal`, `drive`). A terminal without it is electrical, as before.
- A non-electrical terminal refuses new wires. Its stem is drawn in a colour
  of its own, the electrical fields (type, potential) are hidden, and its name
  follows its kind when it is changed: N1, N2… for nozzles, S1… for signals,
  E1… for drives.

**Pipe lines**
- Drag from a nozzle to another nozzle, as for a wire: a pipe line is drawn
  with horizontal and vertical segments, attached to both nozzles, and it
  follows the symbols when they are moved, turned or mirrored.
- One pipe line per nozzle. Selection, deletion and undo work. Deleting a
  symbol leaves its pipe lines in place, detached (undo attaches them again).
- Three kinds of line, after ISO 10628-1: main (strong), secondary (medium),
  auxiliary (dashed).
- Each pipe line carries its own data (`PipeProperties`): line number,
  fluid, nominal diameter, material, flow, piping class, nominal pressure,
  spool number, external finish, kind of line. Nothing is passed on to the
  pipe lines it is connected to.
- **Properties panel**: selecting pipe lines shows their data in the
  properties dock, each field applied with undo. With several pipe lines
  selected, a field that differs shows none, and editing a field changes that
  field only, on all of them.
- **Select the line** (right-click on a pipe line): selects the pipe lines
  reached through the valves and check valves, up to the equipment; *Select
  the line (same DN)* also stops where the nominal diameter changes. The panel
  then edits the whole line at once. A symbol a pipe line runs through says
  so with `pipe_inline="true"` in its `.elmt` (a pump has two nozzles too,
  but stops the walk).
- **Codification of the project**: *Project > Import a P&ID codification*
  reads the codes of the fluids and materials from a CSV file
  (`kind;code;name;family`, kind being `fluid` or `material`), kept by the
  company: none is shipped. The project saves it in a `<pid_codification>`
  block; the panel then offers these codes, and a pipe line keeps the code.
- Saved with each folio in a `<pipes>` block of `.qet` and `.qetz` files,
  with the symbol and nozzle each end is attached to. The output is stable
  (sorted by uuid), and a folio without pipe lines is saved exactly as before.

**Symbols**
- *Change type* (right-click on a placed symbol): swaps it in place for
  another symbol of the same collection folder with the same size, hotspot and
  terminals (name, position, kind). Position, information and connections are
  kept; it can be undone. Useful to place a generic valve or pump first and
  choose its exact type later.
- **Symbols with options**: a symbol may declare options, such as the motor
  of a pump (none, fixed speed, variable speed), in an `<options>` block of
  its `.elmt`; a part of its drawing carries `show-if="motor=fixed,variable"`
  and is drawn only for those values. Each placed element chooses its values
  in the *Options* tab of its properties (with undo); the choice is saved
  with the project, and kept by *Change type* when the new symbol offers the
  same option. So one pump symbol per technology is enough, instead of one
  per motor variant. The element editor keeps the options and conditions,
  but cannot edit them yet.
- Element editor: *Line ends:* (square, round, flat) and *Corners:* (miter,
  round, bevel) for the lines of a symbol, saved as `line-cap` and
  `line-join` in the `style` attribute.

**Signal lines**: only their data so far (`SignalProperties`, the type of
signal: pneumatic, electrical, hydraulic, capillary, guided electromagnetic,
data link, undefined). They cannot be drawn yet.

## What is missing

- The line designation shown along the pipe line, from a template set per
  project, and a line style per family of fluids (with an editor).
- Data of the nozzles (kind of connection, DN, PN), the connections drawn
  from them, and a list of the connection points.
- Branches (tees) on a pipe line, valves dropped on a pipe line.
- Drawing signal lines and drive links (motor to pump).
- Copy and paste of pipe lines (a copy leaves them out for now).
- A library of P&ID symbols with nozzles. Symbols are drawn *inspired by*
  ISO 10628-2, never copied from it; none is published here yet.
- A line list, an export to spreadsheets, an export to DEXPI.
- Declaring options in the element editor, a preview of each case there, and
  options for the actuators of valves.

## Read this before using it on real work

- **The official QElectroTech does not know these additions.** A project
  opened and saved by it loses its pipe lines, the choices of its elements and
  its codification; it draws every part of a symbol with options.
- **`.qetdb` files** (experimental database format of QElectroTech) cannot
  keep them yet: saving such a project as `.qetdb` is refused, with a
  message. Use `.qet` or `.qetz`.
- The file format of these additions may still change, in particular with
  the work on the project file format
  ([Discussion #1512](https://github.com/qelectrotech/qelectrotech-source-mirror/discussions/1512)).

## Trying it

Build the branch as QElectroTech itself, following [INSTALL.md](INSTALL.md).
On Debian with Qt 6 and the KDE Frameworks of the system:

```bash
git clone https://github.com/Opus-id/qelectrotech-source-mirror.git
cd qelectrotech-source-mirror
git checkout pipe-connector
git submodule update --init --recursive
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_WITH_KF=ON -DBUILD_KF=OFF
cmake --build . --parallel
./qelectrotech
```

Then make a test symbol with two nozzles: in the element editor, draw a
valve, add two terminals and set their *Connection:* to *Pipe line
(nozzle)*. Place two of them on a folio and drag from a nozzle to the other.

## Design choices

- `PipeLine` is a class of its own (its own item type, numbered from
  `UserType + 4000`), not a kind of `Conductor`: it only reuses the
  orthogonal routing of the wires. The data are in `PipeProperties`.
- The connection kind of a terminal is a new, optional attribute: existing
  symbols and projects behave as before.
- DEXPI is a long-term goal for an export: a `PipeLine` matches a DEXPI
  *Pipe*, a nozzle a *PipingNode*, and the pipe lines sharing a line number
  form a *PipingNetworkSystem*. No DEXPI code exists yet.

Main files: `sources/pipeproperties.*`, `sources/signalproperties.*`,
`sources/qetgraphicsitem/pipeline.*`, `sources/properties/terminaldata.*`,
`sources/qetgraphicsitem/terminal.*`; tests in
`tests/qttest/tst_pipe*.cpp`, `tst_terminalconnection.cpp`,
`tst_elementchangetype.cpp`, `tst_elementlinecap.cpp`.

## Licence

Same as QElectroTech: GNU GPL, version 2 or later (see `LICENSE`).

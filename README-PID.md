# QElectroTech for P&ID drawings (experimental fork)

This branch, `pipe-medium`, is a **personal and experimental fork** of
[QElectroTech](https://qelectrotech.org) that adds what is needed to draw
**P&IDs** (piping and instrumentation diagrams), for example for water
treatment plants.

**It is not official.** Nothing here has been reviewed or accepted by the
QElectroTech maintainers. The idea is discussed in
[Discussion #1475](https://github.com/qelectrotech/qelectrotech-source-mirror/discussions/1475).
This branch is a proof of concept; no pull request is proposed for now.

## The approach

Following the suggestion made in #1475, a pipe is **not a new kind of
object**: it is an ordinary QElectroTech conductor that carries a
**medium**. Terminals and conductors carry the same medium:

- `electrical` -- the default, not written in the files: every existing
  symbol and project is read and saved exactly as before;
- `pipe` -- the nozzles of process symbols and the pipes between them;
- `signal` and `drive` -- reserved for signal lines and shafts (not drawn
  yet).

So a pipe gets, for free, what a wire already has: drawing, routing,
selection, undo, copy and paste, saving in `.qet`, `.qetz` and `.qetdb`
files, the project database. What only makes sense for a wire is left out
of a pipe (see below).

The earlier attempt, with a separate pipe class, is kept frozen under the
tag `pipe-connector-freeze-2026-11-03`.

## What works today

**Terminals**
- A terminal of a symbol may connect the `pipe`, `signal` or `drive`
  medium (attribute `medium` of `<terminal>` in the `.elmt`, written only
  when it is not electrical).
- Element editor: a *Medium* list in the terminal properties. The stem of a
  terminal takes the color of its medium; *Type* and *Potential* are
  hidden for a non-electrical terminal; its name follows its medium (N1,
  N2… for nozzles, S1… for signals, E1… for drives).
- Joining two terminals of different media by hand is allowed but
  signalled (orange terminal, tooltip). A wiring rule, **off by default**,
  refuses it (*Refuse to join terminals of different media*, with the other
  conductors-per-terminal rules). Automatic wiring never joins two media.
- A nozzle takes **one** pipe: a branch is made with a tee symbol.

**Pipes**
- Drawn by dragging from a nozzle, as a wire; released near a free nozzle
  of the same medium (20 pixels of the screen), the pipe is caught by it.
- **Draw a pipe** tool (key `P`, *Edit > Add*, the *Add* toolbar, the
  context menu of the folio): a click near a free nozzle starts a pipe, a
  second click or the release of a drag near another one ends it. A click
  never picks nor moves a symbol, the free nozzles are marked, the tool
  stays on after each pipe; right click or Esc cancels, then quits. The
  context menu of a symbol offers *Draw a pipe from here*: the pipe starts
  at its free nozzle nearest to the click. Lasso (Ctrl + drag) around two
  nozzles: one pipe between them.
- A conductor takes the medium
  of the terminal it starts from; other properties applied to it never
  change its medium. A file is read as it is: a conductor saved without a
  medium stays electrical.
- **No potential**: nothing is ever passed on from a pipe to the pipes or
  wires connected to it (numbering, shared properties, cables, bridges,
  search and replace… all go through the potential).
- **No wire fields**: no wire number, no single-line symbols, definition
  mark, cable, wire color or section, voltage, bus or function.
- **Data of a pipe**: nominal diameter, fluid, material, flow, line
  number, piping class, nominal pressure, kind of line, spool, external
  finish, saved as `pipe_…` attributes of the conductor, only those set.
  They go through `.qet`, `.qetz` and `.qetdb` files with no code of their
  own (checked by a round trip through a `.qetdb` file).
- **A page of its own** in the properties panel, and in the dialog a double
  click opens, in place of the page of a wire. Several pipes can be edited
  at once: only the field changed reaches them, in one undo step.
- **A codification per project**: *Project > Import a P&ID codification*
  reads the codes of the fluids and materials from a CSV file the company
  keeps (lines `kind;code;name;family`); none is shipped. The page of a pipe
  offers them ("W01 – Raw water"), a pipe keeps the code, free text stays
  possible. Saved in a `<pid_codification>` block, written only when there
  is one, in `.qet`, `.qetz` and `.qetdb` files.
  It is also edited in the *Codification* tab of the *P&ID* page of the
  project properties: fluids, materials and functional units in tables, a
  line added or removed, a CSV file imported or exported (codes, then
  colors and texts, for another project). A name changed touches no pipe; a
  code changed reaches the pipes and symbols bearing it in the same undo
  step; a code removed stays on them as free text, after a question; a code
  is unique in its list. The codes are kept in their natural order (W2
  before W10).
- **Colors set for the project**, in a *P&ID* page of the project properties
  or imported from the same CSV file (`fluid_color;family;#rrggbb`,
  `object_color;pipe;#rrggbb`). A pipe takes the color set on it, else that
  of the family of its fluid, else that of the pipes; a symbol takes that
  of its family of objects (`pid_family` attribute of the `.elmt`: only its
  ink changes, not the white that masks). The code knows no color of its
  own, what has none stays black, and a folio drawn without colors stays
  black too.
- **Defaults for new projects**: two buttons of the *P&ID* page,
  *Save as default codification* (codes, series of the materials, table
  of diameters) and *Save as default palette* (colors and texts), saved
  apart. A new project starts from both; projects that already exist do
  not change.
- **A status of works** on pipes and P&ID symbols: to be built, the default,
  existing and kept, to be demolished. Its color, set for the project,
  comes before any other. Chosen on the page of the object, or for a whole
  selection in its context menu, in one undo step. On a symbol it is kept
  with the options, so a `.qetdb` file refuses it as it refuses them.
- **Drawn after its kind of line** (ISO 10628-1): main line 1 mm (4 units),
  secondary 0.5 mm (2 units), auxiliary 0.5 mm dashed.
- A symbol dropped on a pipe breaks it, as for a wire; a tee takes the pipe
  through its run and keeps its branch free.

**Sizes: DN or outside diameter**
- A size is a DN, as for a metal pipe or a flange, or an outside diameter
  to the tenth (Ø 110, Ø 88,9), as for a plastic pipe or a socket; never one
  read as the other (`pipe_dn` or `pipe_od` on a pipe, `dn` or `od` on a
  nozzle). Files always write a point; the project chooses the decimal
  separator shown (*P&ID* page, *Texts* tab), whatever the computer.
- Each material of the codification says whether its pipes are named by
  their outside diameter, and has its **series** of sizes found in trade
  (CSV line `series;M05;25/32/40`). A pipe takes the sizes of its material
  only: the field is greyed without a material of the codification, or for
  a material without series. A project without material keeps the DN of
  ISO 6708. A size typed out of the series becomes the nearest one, and is
  said.
- A **table** of the project joins outside diameters to the DN of the
  flanges they fit (CSV line `od;110;100`, *Diameters* tab). When the
  material of a pipe changes, its size follows the table (Ø 110 of a
  plastic becomes DN 100 of a steel, or Ø 114,3 of a stainless steel), the
  user choosing when it gives several, else the nearest size of the series.
  Several CSV files can be imported at once.
- A nozzle may have a size in its symbol (the element editor chooses DN or
  Ø beside the field), changed on a placed symbol in a *Nozzles* tab of its
  properties (`<nozzles>` block of `<element>`, written only when set;
  undoable). It is shown under the name of the nozzle, when terminal names
  are shown and the project shows the sizes. A `.qetdb` file refuses it, as
  it refuses the options.
- A diameter fills what is empty and never replaces a value: a new pipe
  without a diameter takes that of its nozzle, once; a nozzle without a
  diameter of its own follows its pipe, nothing being saved.
- A difference is marked without being blocked: an orange diamond in front
  of the pipe, on screen only, and a tooltip. A DN and an outside diameter
  differ when the table does not join them. No reducer is ever inserted.
- An inline symbol (`pipe_inline` attribute of the `.elmt`, exactly two
  nozzles) has one diameter. A change made on the page of a pipe goes through
  these symbols, in one undo step, and replaces the diameter of the pipes
  it reaches. It stops at tees, equipment and three-way valves.

**Connections of the nozzles**
- Each nozzle has a kind of connection: flange (the default, not written),
  plain end, solvent socket, fusion socket, gasket socket, pipe thread,
  butt weld or grooved (`connection` attribute of `<terminal>` in the
  `.elmt`, changed on a placed symbol in its *Nozzles* tab, in the same
  undo step as its size). The list is fixed in the code.
- The connection limits the kind of size: a flange takes a DN, a solvent
  socket an outside diameter, a pipe thread inches, the others a DN or an
  outside diameter. When the connection changes, a size it allows is kept,
  else it is converted when the table gives one answer, else it is removed
  and said.
- A pipe thread is sized in inches (`thread="1 1/2"`), shown `G 1"1/2`,
  and typed as `G 1"1/2`, `1"1/2`, `3/4"` or `1-1/2`. A fixed table (the
  usual one of EN 10255, 1/8" = DN 6 to 6" = DN 150) joins inches to DN:
  for the orange diamond, and for a new pipe, which takes the DN of its
  threaded nozzle (a pipe is never sized in inches).
- The folio shows the size only (`DN 100`, `Ø 110`, `G 1"1/2`); the
  connection is named in the tooltip of the nozzle.

**Symbols**
- **Change type**: right-click a placed symbol, *Change type* lists the
  symbols of the same collection folder that have the same size, hotspot
  and terminals (name, place and medium). The symbol is swapped in place,
  keeping its position, information and connections; undoable. A generic
  symbol (a valve, a pump) can be placed first and made precise later. A
  P&ID symbol keeps its texts (its tag) and takes the identifier of the new
  symbol.
- **Tags made of fields**: a P&ID symbol may give the identifier of its
  type (`prefix` attribute of `<definition>`, e.g. `PC`). A placed
  equipment or valve has a *Tag* tab: functional unit (picked in the list
  of the project, or typed), train and order, saved as information of the
  element. The model of its family in the project, for instance
  `{unit} {id} {train}{order:2}`, makes its label (`U01 PC 105`); `\n`
  starts a new line, for the three lines of an instrument bubble. The label
  follows a change of model or of type, and a label held twice in the
  project is told. Without a model, the label is typed by hand as before.
  The functional units and the models come with the codification of the
  project (CSV lines `unit;U01;Lifting` and `tag_model;equipment;...`).
- **Tags numbered automatically**: the order is one more than the highest
  held by the same functional unit, identifier and train in the project
  (a number freed below it is not given again; no counter is saved, the
  project is read each time). The *Tag* tab fills an empty order as soon
  as the unit is there, and *Next number* gives it on demand; an order
  typed is never changed. When the model shows the train, a numbered tag
  always has one (0 for none). A pasted copy whose tag is taken gets the
  next order (a cut and paste keeps its own).
- **Create similar**: right-click a placed symbol, *Create similar* starts
  the usual placement mode with the same symbol, options, status of works
  and rotation. Device information is not copied; a P&ID tag takes the
  same functional unit and train, and the next order at each drop.
  Automatic connections apply as usual.
- **Ends and corners of lines**: the element editor can draw the ends of a
  line round or flat and its corners round, mitred or bevelled (`line-cap`
  and `line-join` in the style of a part; nothing written for the default).
- **Symbols with options**: a symbol may declare options (an `<options>`
  block, each option with named values and a default) and draw some parts
  only for some values (`show-if` attribute). Example: one pump symbol
  with a *motor* option (none, fixed speed, variable speed) instead of
  three symbols. A placed symbol saves its choices in its `<element>`
  block, only when it has some; they are chosen in an *Options* tab of its
  properties panel (undoable) and kept by *Change type*. A `.qetdb` file
  does not keep them yet: saving such a project as `.qetdb` is refused
  with a message.

**Scale and paper sizes**
- P&ID drawings use 4 drawing units per millimetre, close to the 3.78 of
  the PDF export. Choose the paper size in the *Format* list of the folio
  properties (A0 to A3 suit a P&ID): widths and symbols come out at about
  106 % of their nominal size, and a P&ID is not drawn to scale anyway. The
  default of a new folio is set in the project properties, *New sheet*.
- The *P&ID* page of the project properties sets the size of the tags of
  equipment, valves and instruments and of the names and diameters of the
  nozzles (none set: the size of the symbol), and whether those of the
  nozzles are printed and exported.

**Checks**: every step has its tests, and the example projects of
QElectroTech are saved byte for byte as by master.

## What is missing

- The line designation shown along a pipe.
- Placing a tee by dropping a pipe on another, flow direction, crossings
  as ISO 10628-1 draws them.
- Signal lines, drive links, equipment data tables.
- A way to find the pipes without a diameter, piping classes (material,
  pressure, wall thickness), the pressure rating (PN) of the nozzles.
- Keeping the tags of an issued drawing set: once issued, a deleted tag
  should never be given again.
- Declaring options and conditions in the element editor (they are written
  by hand or by script for now, and kept by the editor).
- A library of P&ID symbols (symbols are drawn *inspired by* ISO 10628-2,
  never copied from it; none is published here).

## Read this before using it on real work

- The file format of these additions may change.
- The official QElectroTech opens a project of this fork, but shows a pipe
  as a wire and drops what it does not know when it saves it.

## Trying it

Build the branch as QElectroTech itself, following [INSTALL.md](INSTALL.md).
On Debian with Qt 6 and the KDE Frameworks of the system:

```bash
git clone https://github.com/Opus-id/qelectrotech-source-mirror.git
cd qelectrotech-source-mirror
git checkout pipe-medium
git submodule update --init --recursive
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_WITH_KF=ON -DBUILD_KF=OFF
cmake --build . --parallel
./qelectrotech
```

Then make a test symbol with two nozzles: in the element editor, add two
terminals and set their *Medium* to *Pipe (nozzle)*. Place two of them on a
folio and drag from a nozzle to the other.

## Licence

Same as QElectroTech: GNU GPL, version 2 or later (see `LICENSE`).

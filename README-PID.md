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
  finish, saved as `pid_…` attributes of the conductor, only those set.
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
  *Save as default codification* (codes of the fluids, materials and
  functional units, sizes of the materials, equal DN) and *Save as default
  presentation* (colors, texts, connections drawn, models of tags), saved
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

**Sizes: the size lives on the pipe**
- The way of working: draw, connect, place the fittings, **size at the
  end**. The size belongs to the pipe; the valves and fittings on it follow.
- The **DN is the size of the line**, on every pipe (`pid_dn`). A material
  named by its outside diameter, as a plastic, also gives the pipe its
  outside diameter to the tenth (`pid_od`, Ø 110, Ø 88,9); the material
  chooses what is written on the drawing. Files always write a point; the
  project chooses the decimal separator shown (*P&ID* page, *Texts* tab),
  whatever the computer.
- Each material of the codification has its **list of sizes** (*Materials*
  tab, CSV line `size;M04;125;100`): an outside diameter and the DN of the
  flange it fits for a plastic, a DN (and an outside diameter if wanted)
  for a metal. When a DN has several outside diameters, one may be the
  default. **Equal DN** are set for the project (`dn_equal;60;65`): a cast
  iron DN 60 and a steel DN 65 are the same size; without a material, such
  a size is written `DN 60/65`.
- The page of a pipe has one size field: a DN or an outside diameter is
  typed, the other is found in the list of its material, with a question
  when there are several (its answer can be kept as the default). When the
  material changes, the DN is kept and the outside diameter found again. A
  size out of the list of its material is marked by an orange band under
  the pipe, on screen only.
- **Inline symbols have no size of their own**: the nozzles of a valve
  (`pid_inline`, its two nozzles) or of a tee (`pid_run="N1 N2 N3"`, the
  nozzles of one size) follow their pipe; their size field is greyed, in
  the *Nozzles* tab as in the element editor. A change made on the page of
  a pipe goes through them, and through nozzles that touch with no pipe
  between them, in one undo step, each material taking it as it has it
  (DN 60 for a cast iron). It stops at equipment, at reducers and at a
  group of nozzles of different sizes.
- Equipment, instruments and **reducers** have sizes of their own: given in
  the symbol (the element editor chooses DN or Ø beside the field) and
  changed on a placed symbol in its *Nozzles* tab (`<pid_nozzles>` block of
  `<element>`, written only when set; undoable). A reducer (`pid_role` =
  `reducer`) is where the size of the line changes: a size typed on one of
  its sides resizes that line in the same undo step. A `.qetdb` file
  refuses a size given on a placed symbol, as it refuses the options.
- A size fills what is empty and never replaces a value: a new pipe
  without a size takes that of its nozzle, once.
- A difference is marked without being blocked: an orange diamond in front
  of the pipe, on screen only, and a tooltip (a reducer with the same size
  on both sides too). No reducer is ever inserted.
- The size of a nozzle is shown under its name, when terminal names are
  shown and the project shows the sizes; the word `DN` can be left out
  (*Texts* tab).

**Connections of the nozzles**
- Each nozzle has a kind of connection: flange (the default, not written),
  plain end, socket (to glue, to weld or with a gasket: the material of the
  pipe tells them apart), pipe thread, butt weld, grooved or quick coupling
  (`pid_connection` attribute of `<terminal>` in the
  `.elmt`, changed on a placed symbol in its *Nozzles* tab, in the same
  undo step as its size). The list is fixed in the code.
- The connection limits the kind of size: a flange or a quick coupling
  takes a DN, a pipe thread inches, the others a DN or an outside diameter.
  When the connection changes, a size it allows is kept, else it is
  converted when there is one answer, else it is removed and said.
- A pipe thread is sized in inches (`pid_thread="1 1/2"`), shown `G 1"1/2`,
  and typed as `G 1"1/2`, `1"1/2`, `3/4"` or `1-1/2`. A fixed table (the
  usual one of EN 10255, 1/8" = DN 6 to 6" = DN 150, shown in the
  *Connections* tab of the *P&ID* page) joins inches to DN:
  for the orange diamond, and for a new pipe, which takes the DN of its
  threaded nozzle (a pipe is never sized in inches).
- The folio shows the size (`DN 100`, `Ø 110`, `G 1"1/2`) and draws the
  connection at the end of the nozzle (flange, socket…), for the kinds of
  connections and the families of symbols the project chooses
  (*Connections* tab); the connection is also named in the tooltip of the
  nozzle.

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
  The functional units come with the codification of the project (CSV line
  `unit;U01;Lifting`), the models with its presentation (CSV line
  `tag_model;equipment;...`).
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
  only for some values (`show_if` attribute). Example: one pump symbol
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

## What the files carry

Every P&ID addition is written only when it is set, so a file without one
is saved as by master. A name added for the P&ID starts with `pid_`; what
is inside a `pid_` block keeps short names. `medium` keeps its name, and
the additions that are not P&ID-only keep plain names (`prefix`,
`<options>`, `show_if`, `line-cap` / `line-join`). Booleans are `true` /
`false`.

| Where | Name | Values | Default | Written when |
|---|---|---|---|---|
| `.elmt`, `<definition>` | `pid_family` | `equipment`, `valve`, `instrument`, `pipe` | none: not a P&ID symbol | the symbol has a family |
| `.elmt`, `<definition>` | `pid_inline` | `true` | a line does not run through it | a line runs through its two nozzles (valve) |
| `.elmt`, `<definition>` | `pid_run` | names of nozzles, space separated (`N1 N2 N3`) | the two nozzles of a `pid_inline` symbol | a tee or a three-way valve |
| `.elmt`, `<definition>` | `pid_role` | `reducer` | none | the symbol plays that role for the lines |
| `.elmt`, `<definition>` | `prefix` | identifier of the tag (`PC`) | that of the collection folder | the symbol gives one |
| `.elmt`, `<definition>` | `<options>` block | options, each with named values and a default | no option | the symbol declares options |
| `.elmt`, a part | `show_if` | `option=value,value`, conditions separated by `;` | always drawn | the part belongs to some values |
| `.elmt`, a part | `line-cap`, `line-join` in `style` | `round`, `flat` / `miter`, `round`, `bevel` | square ends, default corners | not the default |
| `.elmt`, `<terminal>` | `medium` | `pipe`, `signal`, `drive` | `electrical` | not electrical |
| `.elmt`, `<terminal>` | `pid_dn`, `pid_od`, `pid_thread` | DN; outside diameter to the tenth; inches (`1 1/2`) | the size of its pipe | a nozzle with a size of its own |
| `.elmt`, `<terminal>` | `pid_connection` | `plain_end`, `socket`, `thread`, `butt_weld`, `grooved`, `quick_coupling` | `flange` | not a flange |
| `.qet`, `<element>` | `<options>` block | `<option name="…" value="…"/>`, among them `pid_status` (`existing`, `demolished`) | the default of each option; to be built | a value is chosen |
| `.qet`, `<element>` | `<pid_nozzles>` block | `<nozzle name dn od thread connection mark/>`, `mark` = `always`, `never` | those of the symbol and the project | a nozzle of the placed symbol is changed |
| `.qet`, `<element>` | information `pid_unit`, `pid_train`, `pid_order` | text | empty | the *Tag* tab is filled |
| `.qet`, `<conductor>` | `medium` | `pipe`, `signal`, `drive` | `electrical` | not electrical |
| `.qet`, `<conductor>` | `pid_dn`, `pid_od` | DN; outside diameter to the tenth | none | set |
| `.qet`, `<conductor>` | `pid_fluid`, `pid_material`, `pid_flow`, `pid_line_number`, `pid_piping_class`, `pid_nominal_pressure`, `pid_spool`, `pid_external_finish` | text (a code of the codification, or free) | empty | set |
| `.qet`, `<conductor>` | `pid_line_kind` | `secondary`, `auxiliary` | `main` | not main |
| `.qet`, `<conductor>` | `pid_color` | `#rrggbb` | the color of the codification | set by hand |
| `.qet`, `<conductor>` | `pid_status` | `existing`, `demolished` | to be built | not to be built |
| `.qet`, `<project>` | `<pid_codification>` block | fluids, materials and their sizes, functional units, equal DN, tag models, colors, text sizes, text options | none | the project has a codification |

A `medium` this version does not know is read as electrical and said in the
log.

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

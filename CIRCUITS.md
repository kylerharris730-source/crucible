# Circuit networks

Circuit networks carry information. They are deliberately separate from the
physical spark system:

- Copper/iron/graphene carry sparks, generate heat, melt, and can fail.
- Purple circuit wires connect whole devices and carry named integer signals.
- Circuit wires have no cell footprint and cost no material. They are a visible
  wiring overlay, not an alternative kind of copper.

## Using a wire

Turn on **Circuit Wire** in the left panel (or press `X`), then left-click one
device and left-click another. The purple line is the connection. Repeating the
same two clicks removes that wire. Clicking empty space cancels a half-finished
link.

Circuit Wire mode uses a violet cable-end cursor. After the first endpoint is
selected, a dotted preview cable follows the cursor until the second endpoint
is chosen or the link is cancelled. The preview is only guidance: it does not
place a physical wire or cost an item.

Arithmetic and Decider Combinators have two terminals: violet on the **left**
is input; gold on the **right** is output. Click the matching half of the device
when wiring. Their sides are separate networks, so output only feeds input when
you deliberately wire it back; that is how counters and clocks become possible.

All linked devices form one network. Signals from every producer on that network
are added by name. Circuit evaluation has a one-frame boundary: devices read the
previous frame's network and publish this frame's values. That makes feedback
loops predictable instead of depending on device-list order.

## Signals

Every material is its own signal. A chest holding 40 Copper publishes
`Copper = 40`; pipes, drains, and spouts do the same with their buffer. A Block
Watcher publishes the material it currently sees as `material = 1`.

The generic channels `1` through `9` are for values that are not items:

- A Thermocouple publishes its temperature in Celsius on `1` by default. Its
  right-click panel's **signal** button opens the searchable signal picker.
- Clock and Pulse Button publish `1 = 1` for their firing frame.
- A Constant Combinator continuously publishes a configured value on one of the
  numbered channels.

## Combinators

Three circuit devices appear in the catalog and creative inventory:

- **Constant Combinator** — select output channel and tune its value.
- **Arithmetic Combinator** — `A op B -> output`, with `+`, `-`, `*`, `/`, and
  `%` (division/modulo by zero output 0).
- **Decider Combinator** — outputs A when a comparison is true: `>`, `<`, `=`,
  `>=`, `<=`, or `!=`.

Their right-click panel provides buttons for A, B, output, and operator.
Clicking any signal button opens the searchable signal picker, shared with
filters: virtual `1` through `9` appear as violet numbered chips and every
material appears with its regular item sprite. The panel also shows up to three
live signals on its input and output networks, so you can tell whether the
problem is the reader, the condition, or the cable on the wrong side. They
start on generic channels `1`, `2`, and `3`; material signals are automatically
available from connected inventory readers and are already used directly by the
drain filter.

## Circuit-controlled drain filter

A Drain still supports its manual material filter. If its circuit network has a
positive material signal, the strongest material signal temporarily overrides
that manual filter. For example, connecting a chest holding Copper makes a
drain collect Copper; disconnecting it returns to the manual choice. This keeps
physical spark toggles responsible for enable/disable while circuit signals
carry the richer filtering information.

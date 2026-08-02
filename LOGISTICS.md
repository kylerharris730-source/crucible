# Item logistics

Item logistics moves material stacks between discrete devices. It is separate
from both pixel materials in the world and circuit signals: a pipe carries real
items; a circuit wire carries integer information about items.

## Devices

- **Chest** stores one material type and exposes its inventory through the
  familiar player/container inventory screen.
- **Item Pipe** moves one item at a time between adjacent logistics devices.
  Its sprite joins neighboring pipes, including elbows and T junctions.
- **Pipe Crossover** carries two independent lanes so horizontal and vertical
  pipe runs can cross without joining.
- **Spout** places material cells into the world from its item buffer. It needs
  an item supply and can be enabled or disabled by a physical electrical pulse.
- **Drain** collects neighboring material cells into its buffer. It can use a
  manual material filter, a circuit-provided material filter, and physical
  electrical enable/disable.
- **Block Watcher** watches in its facing direction. When the configured
  material touches it, it produces a physical electrical signal and publishes
  the observed material as a circuit signal.

## Connecting a system

Place pipes adjacent to compatible logistics devices. A chest can feed a pipe,
then a Spout; a Drain can feed a pipe, then a chest. Pipes move a single item at
a time, so long systems are intentionally simple and readable before they become
fast.

Use a Pipe Crossover where two routes must cross without exchanging items.
Rotate aimed devices in their right-click panel so the Spout, Drain, or Watcher
faces the desired cell.

## Filters and signals

A Drain's manual filter opens a searchable material picker. Its circuit network
can temporarily override that choice: a positive material signal selects the
strongest material signal on the network. See [CIRCUITS.md](CIRCUITS.md) for
the circuit side of that behavior.

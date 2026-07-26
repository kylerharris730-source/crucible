# Powder

A falling-sand sandbox. Pour materials into a box and watch them interact —
sand piles up, water soaks into dirt, fire spreads through wood, lava melts
stone, liquid nitrogen freezes mercury solid.

Everything runs on a real temperature simulation. Every cell in the world has a
temperature, heat conducts between neighbours, and almost every interesting
behaviour falls out of that rather than being scripted: water boils *because* it
reached 100 °C, wood catches *because* the flame beside it pushed it past its
ignition point, and a copper bar melts if you leave it in lava too long.

C++11, Win32, no dependencies. About 2,800 lines.

![Powder running: three vessels side by side, one being boiled by fire, one
chilled by cold fire, one frozen solid, with a water reservoir draining down a
channel on the left](powder.png)

*Three vessels, same setup, different fuel underneath: fire boiling the first
into steam, cold fire freezing the second, a heater glowing at the far right.
The reservoir on the left is draining through a channel onto a dirt pile. 36,000
live cells at 60 fps, 2.28 ms of simulation per frame — and only 139 of the 192
chunks are being simulated at all, because the settled parts cost nothing.*

## Try it

**[Download powder.exe from the latest release](https://github.com/kylerharris730-source/powder/releases/latest)**
— one file, nothing to install, no runtime needed. Windows only.

> Windows SmartScreen will warn about an unsigned executable downloaded from the
> internet. That is expected for a hobby build with no code-signing certificate.
> If you would rather not take a stranger's word for it, building from source
> takes about ten seconds.

To build it yourself you need MinGW-w64 (g++) on your PATH:

```bash
build.bat
```

Then run `build\powder.exe`. `mingw32-make` and `mingw32-make run` also work.

## Controls

Pick a material from the panel on the left and draw with the mouse. That is
genuinely all you need to start.

| Input | Action |
|---|---|
| **Left mouse** | Draw the selected material |
| **Right mouse** | Erase |
| **Mouse wheel** | Brush size |
| **Space** | Pause |
| **V** | Cycle view: Glow → Material → Heat |
| **C** | Clear everything |
| **Esc** | Quit |

Everything else is on the panel: brush size, simulation speed, and four toggles.

- **Overwrite** — off means the brush only fills empty space, so you can pour
  something into a scene without carving through what is already there.
- **View** — *Glow* is the normal one. *Material* hides heat entirely, for when
  the glow is in the way. **Heat** is the one worth knowing about: it paints
  temperature as false colour, blue through red to white, so you can see where
  heat is actually going. A lot of what makes this fun is invisible without it.
- **Speed** — 1×/2×/4× simulation steps per frame, for when you are waiting on
  something slow like a lava puddle setting.

The foot of the panel names whatever is under the cursor, with its temperature.
Pointing at apparently empty air to find out how hot it is turns out to be
useful more often than you would think.

## Things worth trying

Roughly in order of how much setup they need.

**Pour water onto dirt.** Wetness spreads as a band rather than a single wet
pixel, and it stops at a finite depth — thick dirt stays dry underneath. Wet
dirt also piles at a steeper angle than dry.

**Set fire to a wooden wall** and watch the burn front travel along it. Then try
putting it out with water.

**Melt stone into lava** with the Heat tool, then watch the lava cool and set
back into stone. It takes a while, deliberately: lava holds eight times the heat
an ordinary material does, which is why a puddle glows for ages.

**Boil a pot dry.** Build a container out of graphene, fill it with water, put
fire underneath. Fire struggles to boil it all off. Plasma does it about six
times faster — not because it is hotter, but because it does not exhaust itself.

**Tap a lava flow.** Feed lava with a Clone, drain it with a Void, and run a
graphene bar out of the flow into whatever you want to heat. This is how you
reach temperatures a flame alone cannot — and a fire driven all the way to the
top of the scale turns into plasma.

**Freeze mercury.** Pour liquid nitrogen over a mercury pool. Mercury freezes at
−30 °C, which nothing but LN2 or a Cooler can reach.

**Build a still.** The good one. Water boils at 100 °C and mercury at 150 °C, so
a mixture held between those two gives off steam while the mercury stays put.
Mercury vapour is heavier than steam, so the two even separate by weight on
their own.

**Find the limits of your materials.** Copper carries heat furthest but melts
first — lava melts copper and leaves iron alone. Iron survives lava but not a
Heater. Graphene has no melting point at all, so anything genuinely hot has to
be built from it. Rubber is the best insulator in the game right up until it
touches something over 100 °C, at which point it melts and is worse than
nothing.

## The materials

27 of them.

| | |
|---|---|
| **Building** | Wall, Stone, Wood, Rubber, Iron, Copper, Graphene |
| **Loose** | Sand, Dirt |
| **Liquids** | Water, Mercury, Liquid N2, Lava |
| **Gases** | Steam, Fire, Plasma, Cold Fire |
| **Cold** | Ice, Liquid N2, Cold Fire, Cooler |
| **Hot** | Fire, Plasma, Lava, Heater |
| **Machines** | Clone (copies whatever it first touches, forever), Void (destroys whatever touches it), Heater, Cooler |

A few materials exist only as states of others and are not in the picker.
Molten iron, molten copper, molten rubber, mercury vapour and frozen mercury are
all fully simulated — you just have to make them rather than place them.

For every number — melting points, densities, conductivities — and the
measurements behind each, see **[INTERNALS.md](INTERNALS.md)**.

## How it's built

The short version: the grid is split into 32×32 chunks that each track whether
anything happened inside them, and only active chunks get simulated. A world
that has finished settling costs **0.00 ms** — 91,000 cells at rest are free.
Cells are 4 bytes, so sixteen fit in a cache line, and drawing one is a single
lookup into a precomputed palette.

A busy screen runs 2–3 ms per frame — the screenshot above is 2.28 ms with
36,000 live cells, and it is only simulating 139 of its 192 chunks. The worst
case in the benchmark suite is a full grid of sand with heat spreading through
every part of it, at about 9 ms.

The long version — including why several obvious-looking optimisations are
actively wrong — is in [INTERNALS.md](INTERNALS.md).

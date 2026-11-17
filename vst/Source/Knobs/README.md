# Nam Forge knob collection

Vector JUCE LookAndFeel models. No images or fonts, and every dimension scales
from the slider bounds.

| Model | Class | Description |
|---|---|---|
| Boss Skirted | `BossSkirtedLookAndFeel` | Fluted body on a wide gloss-black skirt. Axial (`topDown`) or three-quarter (`perspective`) view. |
| Boss | `BossLookAndFeel` | The same body and spun-aluminium cap without the skirt. Axial view only. |
| Neve-Basic | `NeveBasicLookAndFeel` | Tall straight-sided knob with twenty fine flutes, a raised rim, a recessed top and a radial pointer. Straight-on view. |
| Neve-Marconi | `NeveMarconiLookAndFeel` | Dark Marconi cap with a recessed centre and a wide pointer bar. No clear skirt or outer base. Straight-on view. |
| Chicken-Head | `ChickenHeadLookAndFeel` | Asymmetric pointer knob with a wide tail, tapered pointer and raised hub. Straight-on view. |
| Davies | `DaviesLookAndFeel` | Circular base with a raised rectangular pointer bar, a longitudinal white pointer stripe at its outer end, and a set-screw hole. Straight-on view. |
| Fender | `FenderLookAndFeel` | Two-tier skirt knob with large, full-height numbers descending 10→1 left to right, with 5 at top centre. No pointer; text labels omitted. Numbers are white except on White and Cream. Straight-on view. |
| Marshall | `MarshallLookAndFeel` | Fluted black body with a spun-metal cap; pointer black on silver/gold, white on brushed black. Straight-on view. |
| Rectifier | `RectifierLookAndFeel` | Skirtless brushed-metal body with a large dot indicator; metal palette only (silver, gold, brushed black). Straight-on view. |
| SSL 4000 | `SSLLookAndFeel` | Thin segmented grey ring with a coloured insert and white pointer. Straight-on view. |
| SSL Crenulated | `SSLCrenulatedLookAndFeel` | SSL body with five large raised grip sections separated by narrow recessed indents. Straight-on view. |
| API 500 | `APILookAndFeel` | Black four-lobed body, one lobe larger, with a coloured insert and a Boss-style white position dot. Straight-on view. |

The two Boss models derive from `BossKnobLookAndFeel`, which owns the shared
renderer. A model only chooses the `skirted` flag; shading, fluting, cap
brushing and the position dot stay consistent by construction.

## Neve-Basic

`NeveBasicLookAndFeel` is intentionally axial only: the straight-on view shows
the scalloped fluted edge, the recessed top and the pointer. It has no metal cap.
It supports the same plastic palette:

```cpp
#include "Knobs/NeveBasicLookAndFeel.h"

NeveBasicLookAndFeel laf;
laf.setPlasticFinish (KnobFinish::vintageOxblood);   // default is Neve oxblood
slider.setLookAndFeel (&laf);
```

## Neve-Marconi

`NeveMarconiLookAndFeel` is the straight-on Marconi knob: a dark cap with a
recessed centre and a wide pointer bar with a white index inlay. There is no outer
skirt or base — the bar wings are the widest part of the control.

```cpp
#include "Knobs/NeveMarconiLookAndFeel.h"

NeveMarconiLookAndFeel laf;                    // black by default
laf.setPlasticFinish (KnobFinish::modernRed);  // optional palette colour
slider.setLookAndFeel (&laf);
```

## Classic amp knobs

The reference-derived set uses the same construction rules: straight-on, vector
only, no baked assets.

- `ChickenHeadLookAndFeel` — asymmetric pointer and wide tail.
- `DaviesLookAndFeel` — circular base, raised rectangular pointer bar,
  longitudinal white pointer stripe, set screw.
- `FenderLookAndFeel` — numbered skirt; no VOLUME/TONE text and no pointer.
  Large full-height numbers run 10→1 left to right with 5 at top centre,
  white on every finish except White and Cream, which use charcoal.
- `MarshallLookAndFeel` — fluted body and spun-metal cap; black pointer on
  silver/gold, white on brushed black.
- `RectifierLookAndFeel` — skirtless brushed-metal body with a large dot
  indicator; black on silver/gold, white on brushed black; metal palette only.

Marshall also accepts the brushed-metal finishes:

```cpp
MarshallLookAndFeel marshall;
marshall.setPlasticFinish (KnobFinish::black);
marshall.setMetalFinish (KnobMetalFinish::gold);
```

Rectifier is metal-only:

```cpp
RectifierLookAndFeel rectifier;
rectifier.setMetalFinish (KnobMetalFinish::aluminium); // or gold / black
```

## Console knobs

- `SSLLookAndFeel` — thin segmented grey grip ring with a raised coloured insert.
  The palette colour applies to the insert.
- `SSLCrenulatedLookAndFeel` — the same SSL insert with a five-tooth crenulated
  skirt: large raised sections separated by the same narrow indents as before.
- `APILookAndFeel` — black four-lobed body with one slightly larger lobe, a
  coloured insert and a Boss-style white position dot aligned with the knob
  angle. The palette colour applies to the insert; there is no pointer line.

SSL uses white pointer lines except on White and Cream, which use charcoal.

## Pointer rules

- Every pointer/dot/stripe rotates with the knob and reads the current position:
  12 o'clock at centre, 7:30/4:30 at the extremes.
- Pointer colour follows the body/plinth: warm white `#FFF8E8`, charcoal
  `#1B1B1B` only for White and Cream. Inserts do not change it.
- Metal-surface pointers are black `#111111` on silver/aluminium and gold, and
  warm white `#FFF8E8` on brushed black. Use a contrasting edge.
- Fender has no pointer. Chicken-Head's body is the pointer; its index line
  follows the same colour rule.

## Colours

`KnobColours.h` defines two curated plastic palettes, plus Black and White:

- Modern: red, blue, green, purple, yellow, orange
- Vintage: oxblood, royal blue, sage green, grey, cream

Use a named finish or any `juce::Colour`:

```cpp
laf.setPlasticFinish (KnobFinish::modernBlue);
laf.setPlasticColour (juce::Colour (0xff5b262b));   // custom oxblood
```

The renderer derives highlight, shadow and indicator contrast from the plastic
colour. Light finishes get a charcoal indicator; dark finishes keep the off-white
dot.

The metal cap has a separate finish set:

```cpp
laf.setMetalFinish (KnobMetalFinish::gold);   // aluminium, gold or black
laf.setMetalColour (juce::Colour (0xffd3a64c));
```

| Finish | Base colour |
|---|---|
| Brushed Aluminium | `#C8C3B7` |
| Brushed Gold | `#D3A64C` |
| Brushed Black | `#383838` |

## Palette preview requirement

Every new knob model must ship with a full contact sheet covering all thirteen
plastic finishes:

- Black, White
- Modern: red, blue, green, purple, yellow, orange
- Vintage: oxblood, royal blue, sage green, grey, cream

Use one large render per finish, label each swatch, and rely on the automatic
indicator contrast. Save the sheet as `docs/<model>-palette.png` and link it from
the model's documentation.

## Use

```cpp
#include "Knobs/BossSkirtedLookAndFeel.h"   // or BossLookAndFeel.h / BossKnobs.h

BossSkirtedLookAndFeel laf;
laf.setStyle (BossKnobLookAndFeel::Style::topDown);   // rack-panel view

slider.setLookAndFeel (&laf);
```

Or use the family factory:

```cpp
#include "Knobs/BossKnobs.h"

auto laf = makeBossLookAndFeel (BossModel::boss);          // no skirt
auto skirted = makeBossLookAndFeel (BossModel::bossSkirted,
                                    KnobFinish::vintageCream,
                                    KnobMetalFinish::gold);
```

Set the LAF on the editor so every child slider inherits it, keep the member
before the sliders, and call `setLookAndFeel (nullptr)` in the editor destructor.

## Preview

`NamForgeKnobShot` renders min / centre / max at 560 px:

```powershell
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\boss.png' boss topdown black aluminium
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\boss-gold.png' boss-skirted topdown black gold
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\neve.png' neve-basic topdown vintage-oxblood
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\marconi.png' neve-marconi topdown black
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\fender.png' fender topdown black
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\rectifier-silver.png' rectifier topdown none silver
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\ssl.png' ssl topdown modern-blue
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\ssl-cren.png' ssl-crenulated topdown modern-blue
& build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe 'build-vst\api.png' api topdown black
```

## Add a model

Create a thin wrapper rather than copying the renderer:

```cpp
class MyBossLookAndFeel : public BossKnobLookAndFeel
{
public:
    MyBossLookAndFeel() : BossKnobLookAndFeel (true) {}   // true = skirted
};
```

If the new model needs a different proportion, add it to the parameterised
geometry in `BossKnobLookAndFeel::drawTopDown` instead of forking the drawing
code. The named model should remain a thin constructor.

#pragma once

#include "BossKnobLookAndFeel.h"

/** Boss Skirted: the fluted black knob on a wide gloss-black flange.

    This is the model recreated from the reference photograph. It supports both
    the axial (rack-panel) and three-quarter product-photo presentations.
*/
class BossSkirtedLookAndFeel : public BossKnobLookAndFeel
{
public:
    BossSkirtedLookAndFeel() : BossKnobLookAndFeel (true) {}
};

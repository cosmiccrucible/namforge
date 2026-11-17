#pragma once

#include "BossKnobLookAndFeel.h"

/** Boss: the same fluted body and spun-aluminium cap without the outer skirt.

    The fluted body is the largest diameter, so the knob reads as a compact
    panel-mounted boss. The white position dot moves onto the black body ring
    between the cap and the scalloped edge.
*/
class BossLookAndFeel : public BossKnobLookAndFeel
{
public:
    BossLookAndFeel() : BossKnobLookAndFeel (false) {}
};

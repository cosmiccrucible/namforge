#pragma once

/** The Boss knob family.

    BossSkirtedLookAndFeel  - fluted body on a wide gloss-black skirt
    BossLookAndFeel         - the same body and cap without the skirt
*/

#include <memory>

#include "BossLookAndFeel.h"
#include "BossSkirtedLookAndFeel.h"

enum class BossModel
{
    boss,
    bossSkirted
};

/** Creates one of the named library models in a curated finish. */
inline std::unique_ptr<BossKnobLookAndFeel> makeBossLookAndFeel (
    BossModel model,
    KnobFinish finish = KnobFinish::black,
    KnobMetalFinish metal = KnobMetalFinish::aluminium)
{
    std::unique_ptr<BossKnobLookAndFeel> result;

    if (model == BossModel::boss)
        result = std::make_unique<BossLookAndFeel>();
    else
        result = std::make_unique<BossSkirtedLookAndFeel>();

    result->setPlasticFinish (finish);
    result->setMetalFinish (metal);
    return result;
}


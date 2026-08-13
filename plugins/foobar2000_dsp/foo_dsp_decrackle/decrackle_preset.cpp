/* ========================================
 *  foo_dsp_decrackle - preset (de)serialisation
 * ======================================== */

#include "stdafx.h"
#include "decrackle_preset.h"

using airwindows::DeCrackleParams;

namespace decrackle_preset {

GUID guid() {
    // {90680877-D520-4721-BA24-7B3C8E19269E}
    static const GUID g =
        { 0x90680877, 0xd520, 0x4721, { 0xba, 0x24, 0x7b, 0x3c, 0x8e, 0x19, 0x26, 0x9e } };
    return g;
}

void make(const DeCrackleParams & params, dsp_preset & out) {
    DeCrackleParams p = params;
    p.sanitize();

    dsp_preset_builder builder;
    builder << (t_uint32)version;
    builder << p.filter << p.window << p.threshold << p.surface << p.dryWet;
    builder.finish(guid(), out);
}

DeCrackleParams parse(const dsp_preset & in) {
    DeCrackleParams p = DeCrackleParams::defaults();
    if (in.get_owner() == guid()) {
        try {
            dsp_preset_parser parser(in);
            t_uint32 ver = 0;
            parser >> ver;
            if (ver == (t_uint32)version) {
                DeCrackleParams tmp;
                parser >> tmp.filter >> tmp.window >> tmp.threshold
                       >> tmp.surface >> tmp.dryWet;
                p = tmp;
            }
            // Unknown revision: keep the defaults rather than guessing.
        } catch (...) {
            // Truncated or otherwise unusable - defaults it is.
            p = DeCrackleParams::defaults();
        }
    }
    p.sanitize();
    return p;
}

} // namespace decrackle_preset

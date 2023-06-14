#include "lib/effect_runner.h"
#include "spokes.h"

int main(int argc, char **argv)
{
    SpokesEffect e;

    // Global brightness control
    Brightness br(e);
    br.set(0.2);

    EffectRunner r;
    r.setEffect(&br);
    r.setLayout("../layouts/grid64x32.json");
    return r.main(argc, argv);
}


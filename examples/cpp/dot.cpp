#include "lib/effect_runner.h"
#include "dot.h"

int main(int argc, char **argv)
{
    EffectRunner r;

    DotEffect e("data/dot.png");
    r.setEffect(&e);

    // Defaults, overridable with command line options
    r.setLayout("../layouts/grid64x32.json");

    return r.main(argc, argv);
}

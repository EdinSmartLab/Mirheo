#pragma once

#include <core/datatypes.h>

#include <core/utils/cpu_gpu_defines.h>
#include <core/utils/helper_math.h>

class ParticleVector;

/**
 * Applies constant force #extraForce to every particle
 */
class Forcing_ConstDP
{
public:
    Forcing_ConstDP(real3 extraForce) : extraForce(extraForce) {}
    void setup(__UNUSED ParticleVector* pv, __UNUSED real t) {}

    __D__ inline real3 operator()(real3 original, __UNUSED Particle p) const
    {
        return extraForce + original;
    }

private:
    real3 extraForce;
};

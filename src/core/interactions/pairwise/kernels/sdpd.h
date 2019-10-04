#pragma once

#include "accumulators/force.h"
#include "density_kernels.h"
#include "fetchers.h"
#include "interface.h"
#include "pressure_EOS.h"

#include <core/interactions/utils/step_random_gen.h>
#include <core/utils/restart_helpers.h>
#include <core/mirheo_state.h>

#include <fstream>
#include <random>

class CellList;
class LocalParticleVector;

template <typename PressureEOS, typename DensityKernel>
class PairwiseSDPDHandler : public ParticleFetcherWithVelocityDensityAndMass
{
public:
    
    using ViewType     = PVviewWithDensities;
    using ParticleType = ParticleWithDensityAndMass;
    
    PairwiseSDPDHandler(float rc, PressureEOS pressure, DensityKernel densityKernel, float viscosity, float kBT, float dt) :
        ParticleFetcherWithVelocityDensityAndMass(rc),
        pressure(pressure),
        densityKernel(densityKernel),
        fRfact(sqrt(2 * zeta * viscosity * kBT / dt)),
        fDfact(viscosity * zeta)
    {
        inv_rc = 1.0 / rc;
    }

    __D__ inline float3 operator()(const ParticleType dst, int dstId, const ParticleType src, int srcId) const
    {        
        float3 dr = dst.p.r - src.p.r;
        float rij2 = dot(dr, dr);
        if (rij2 > rc2) return make_float3(0.0f);
        
        float di = dst.d;
        float dj = src.d;
        
        float pi = pressure(di * dst.m);
        float pj = pressure(dj * src.m);

        float inv_disq = 1.f / (di * di);
        float inv_djsq = 1.f / (dj * dj);

        float inv_rij = rsqrtf(rij2);
        float rij = rij2 * inv_rij;
        float dWdr = densityKernel.derivative(rij, inv_rc);

        float3 er = dr * inv_rij;
        float3 du = dst.p.u - src.p.u;
        float erdotdu = dot(er, du);

        float myrandnr = Logistic::mean0var1(seed, min(src.p.i1, dst.p.i1), max(src.p.i1, dst.p.i1));

        float Aij = (inv_disq + inv_djsq) * dWdr;
        float fC = - (inv_disq * pi + inv_djsq * pj) * dWdr;
        float fD = fDfact *        Aij * inv_rij  * erdotdu;
        float fR = fRfact * sqrtf(-Aij * inv_rij) * myrandnr;
        
        return (fC + fD + fR) * er;
    }

    __D__ inline ForceAccumulator getZeroedAccumulator() const {return ForceAccumulator();}

protected:

    static constexpr float zeta = 3 + 2;

    float inv_rc;
    float seed;
    PressureEOS pressure;
    DensityKernel densityKernel;
    float fDfact, fRfact;
};

template <typename PressureEOS, typename DensityKernel>
class PairwiseSDPD : public PairwiseKernel, public PairwiseSDPDHandler<PressureEOS, DensityKernel>
{
public:

    using HandlerType = PairwiseSDPDHandler<PressureEOS, DensityKernel>;
    
    PairwiseSDPD(float rc, PressureEOS pressure, DensityKernel densityKernel, float viscosity, float kBT, float dt, long seed = 42424242) :
        PairwiseSDPDHandler<PressureEOS, DensityKernel>(rc, pressure, densityKernel, viscosity, kBT, dt),
        stepGen(seed)
    {}

    const HandlerType& handler() const
    {
        return (const HandlerType&) (*this);
    }
    
    void setup(__UNUSED LocalParticleVector *lpv1,
               __UNUSED LocalParticleVector *lpv2,
               __UNUSED CellList *cl1,
               __UNUSED CellList *cl2, const MirState *state) override
    {
        this->seed = stepGen.generate(state);
    }

    void writeState(std::ofstream& fout) override
    {
        TextIO::writeToStream(fout, stepGen);
    }

    bool readState(std::ifstream& fin) override
    {
        return TextIO::readFromStream(fin, stepGen);
    }
    
    
protected:

    StepRandomGen stepGen;    
};
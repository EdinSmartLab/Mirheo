#pragma once

#include "interface.h"
#include <memory>

class BasicInteractionDensity : public Interaction
{
public:
    ~BasicInteractionDensity();
    
    void setPrerequisites(ParticleVector *pv1, ParticleVector *pv2, CellList *cl1, CellList *cl2) override;
    
    std::vector<InteractionChannel> getOutputChannels() const override;
    
    void local (ParticleVector *pv1, ParticleVector *pv2, CellList *cl1, CellList *cl2, cudaStream_t stream) override;
    void halo  (ParticleVector *pv1, ParticleVector *pv2, CellList *cl1, CellList *cl2, cudaStream_t stream) override;

    Stage getStage() const override {return Stage::Intermediate;}
    
protected:
    BasicInteractionDensity(const MirState *state, std::string name, float rc);
};


template <class DensityKernel>
class InteractionDensity : public BasicInteractionDensity
{
public:
    InteractionDensity(const MirState *state, std::string name, float rc, DensityKernel densityKernel);
    ~InteractionDensity();

protected:

    DensityKernel densityKernel;
};

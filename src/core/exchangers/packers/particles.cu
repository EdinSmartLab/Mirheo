#include "particles.h"
#include "common.h"
#include "shifter.h"

#include "../exchange_helpers.h"

#include <core/pvs/particle_vector.h>
#include <core/utils/cuda_common.h>
#include <core/utils/kernel_launch.h>

#include <type_traits>

namespace ParticlePackerKernels
{
template <typename T>
__global__ void packToBuffer(int n, const MapEntry *map, const size_t *offsetsBytes, const int *offsets,
                             const T *srcData, Shifter shift, char *buffer)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    if (i > n) return;

    auto m = map[i];
    int buffId = m.getBufId();
    int  srcId = m.getId();

    T *dstData = (T*) (buffer + offsetsBytes[buffId]);
    int dstId = i - offsets[buffId];

    dstData[dstId] = shift(srcData[srcId], buffId);
}

template <typename T>
__global__ void unpackFromBuffer(int nBuffers, const int *offsets, int n, const char *buffer,
                                 const size_t *offsetsBytes, T *dstData)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;

    extern __shared__ int sharedOffsets[];

    for (int i = threadIdx.x; i < nBuffers; i += blockDim.x)
        sharedOffsets[i] = offsets[i];
    __syncthreads();

    if (i > n) return;
    
    int buffId = dispatchThreadsPerBuffer(nBuffers, sharedOffsets, i);
    int pid = i - sharedOffsets[buffId];
    
    const T *srcData = (const T*) (buffer + offsetsBytes[buffId]);

    dstData[pid] = srcData[pid];
}

} // namespace ParticlePackerKernels

ParticlesPacker::ParticlesPacker(ParticleVector *pv, PackPredicate predicate) :
    Packer(pv, predicate)
{}

size_t ParticlesPacker::getPackedSizeBytes(int n) const
{
    return _getPackedSizeBytes(pv->local()->dataPerParticle, n);
}

void ParticlesPacker::packToBuffer(const LocalParticleVector *lpv, ExchangeHelper *helper, const std::vector<size_t>& alreadyPacked, cudaStream_t stream)
{
    auto& manager = lpv->dataPerParticle;

    int nBuffers = helper->send.sizes.size();
    
    offsetsBytes.copyFromDevice(helper->send.offsetsBytes, stream);

    for (auto sz : alreadyPacked) // advance offsets to skip the already packed data
        updateOffsets(nBuffers, sz, helper->send.sizes.devPtr(), offsetsBytes.devPtr(), stream);
    
    for (const auto& name_desc : manager.getSortedChannels())
    {
        if (!predicate(name_desc)) continue;
        auto& desc = name_desc.second;

        Shifter shift(desc->shiftTypeSize > 0, pv->state->domain);

        auto packChannel = [&](auto pinnedBuffPtr)
        {
            using T = typename std::remove_pointer<decltype(pinnedBuffPtr)>::type::value_type;

            int n = helper->map.size();
            const int nthreads = 128;

            SAFE_KERNEL_LAUNCH(
                ParticlePackerKernels::packToBuffer,
                getNblocks(n, nthreads), nthreads, 0, stream,
                n, helper->map.devPtr(), offsetsBytes.devPtr(), helper->send.offsets.devPtr(),
                pinnedBuffPtr->devPtr(), shift, helper->send.buffer.devPtr());

            updateOffsets<T>(nBuffers, helper->send.sizes.devPtr(), offsetsBytes.devPtr(), stream);
        };
        
        mpark::visit(packChannel, desc->varDataPtr);
    }
}

void ParticlesPacker::unpackFromBuffer(LocalParticleVector *lpv, const ExchangeHelper *helper, int oldSize, cudaStream_t stream)
{
    auto& manager = lpv->dataPerParticle;

    offsetsBytes.copyFromDevice(helper->recv.offsetsBytes, stream);

    int nBuffers  = helper->recv.sizes.size();
    int nIncoming = helper->recv.offsets[nBuffers];
    
    for (const auto& name_desc : manager.getSortedChannels())
    {
        if (!predicate(name_desc)) continue;
        auto& desc = name_desc.second;

        auto unpackChannel = [&](auto pinnedBuffPtr)
        {
            using T = typename std::remove_pointer<decltype(pinnedBuffPtr)>::type::value_type;

            const int nthreads = 128;
            const size_t sharedMem = nBuffers * sizeof(int);

            SAFE_KERNEL_LAUNCH(
                ParticlePackerKernels::unpackFromBuffer,
                getNblocks(nIncoming, nthreads), nthreads, sharedMem, stream,
                nBuffers, helper->recv.offsets.devPtr(), nIncoming, helper->recv.buffer.devPtr(),
                offsetsBytes.devPtr(), pinnedBuffPtr->devPtr() + oldSize);

            updateOffsets<T>(nBuffers, helper->recv.sizes.devPtr(), offsetsBytes.devPtr(), stream);
        };
        
        mpark::visit(unpackChannel, desc->varDataPtr);
    }
}

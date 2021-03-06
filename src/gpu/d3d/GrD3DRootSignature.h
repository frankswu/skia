/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrD3DRootSignature_DEFINED
#define GrD3DRootSignature_DEFINED

#include "include/gpu/d3d/GrD3DTypes.h"
#include "src/gpu/GrManagedResource.h"

class GrD3DGpu;

class GrD3DRootSignature : public GrManagedResource {
public:
    static sk_sp<GrD3DRootSignature> Make(GrD3DGpu* gpu, int numTextureSamplers);

    enum class ParamIndex {
        kConstantBufferView = 0,
        kSamplerDescriptorTable = 1,
        kTextureDescriptorTable = 2,

        kLast = kTextureDescriptorTable
    };
    static constexpr unsigned int kParamIndexCount = (unsigned int)(ParamIndex::kLast) + 1;

    bool isCompatible(int numTextureSamplers) const;

    ID3D12RootSignature* rootSignature() const { return fRootSignature.Get(); }

#ifdef SK_TRACE_MANAGED_RESOURCES
    /** Output a human-readable dump of this resource's information
    */
    void dumpInfo() const override {
        SkDebugf("GrD3DRootSignature: %p, numTextures: %d (%d refs)\n",
                 fRootSignature.Get(), fNumTextureSamplers, this->getRefCnt());
    }
#endif

private:
    GrD3DRootSignature(ComPtr<ID3D12RootSignature> rootSig, int numTextureSamplers);

    // This will be called right before this class is destroyed and there is no reason to explicitly
    // release the fRootSignature cause the ComPtr will handle that in the dtor.
    void freeGPUData() const override {}

    ComPtr<ID3D12RootSignature> fRootSignature;
    int fNumTextureSamplers;
};

#endif

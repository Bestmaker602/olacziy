/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gltfio/ResourceLoader.h>

#include "FFilamentAsset.h"
#include "upcast.h"

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>

#include <math/quat.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/Log.h>

#include <cgltf.h>

#include <tsl/robin_map.h>

#include <string>

using namespace filament;
using namespace filament::math;
using namespace utils;

namespace {
    using UrlMap = tsl::robin_map<std::string, const uint8_t*>;
}

namespace gltfio {

using namespace details;

class BlobCache {
public:
    BlobCache()  {}

    ~BlobCache() {
        for (auto asset : mAssets) {
            asset->releaseSourceAsset();
        }
    }

    void addAsset(FFilamentAsset* asset) {
        mAssets.push_back(asset);
        asset->acquireSourceAsset();
    }

    void addPendingUpload() {
        ++mPendingUploads;
    }

    // Destroy the URL cache only after the pending upload count is zero and the client has
    // destroyed the ResourceLoader object.
    static void onLoadedResource(void* buffer, size_t size, void* user) {
        auto cache = (BlobCache*) user;
        if (--cache->mPendingUploads == 0 && cache->mLoaderDestroyed) {
            delete cache;
        }
    }

    void onLoaderDestroyed() {
        if (mPendingUploads == 0) {
            delete this;
        } else {
            mLoaderDestroyed = true;
        }
    }

private:
    std::vector<FFilamentAsset*> mAssets;
    bool mLoaderDestroyed = false;
    int mPendingUploads = 0;
};

ResourceLoader::ResourceLoader(Engine* engine, const char* basePath) : mEngine(engine),
        mBasePath(basePath), mCache(new BlobCache) {}

ResourceLoader::~ResourceLoader() {
    mCache->onLoaderDestroyed();
}

bool ResourceLoader::loadResources(FilamentAsset* asset) {
    FFilamentAsset* fasset = upcast(asset);
    mCache->addAsset(fasset);

    // Read data from the file system and base64 URLs.
    cgltf_options options {};
    auto gltf = (cgltf_data*) fasset->mSourceAsset;
    cgltf_result result = cgltf_load_buffers(&options, gltf, mBasePath.c_str());
    if (result != cgltf_result_success) {
        slog.e << "Unable to load resources." << io::endl;
        return false;
    }

    // Upload data to the GPU, or do a memcpy for animation / orientation data.
    const BufferBinding* bindings = asset->getBufferBindings();
    for (size_t i = 0, n = asset->getBufferBindingCount(); i < n; ++i) {
        auto bb = bindings[i];
        const uint8_t* ucdata = bb.offset + (const uint8_t*) *bb.data;
        if (bb.vertexBuffer) {
            mCache->addPendingUpload();
            VertexBuffer::BufferDescriptor bd(ucdata, bb.size, BlobCache::onLoadedResource, mCache);
            bb.vertexBuffer->setBufferAt(*mEngine, bb.bufferIndex, std::move(bd));
        } else if (bb.indexBuffer) {
            mCache->addPendingUpload();
            VertexBuffer::BufferDescriptor bd(ucdata, bb.size, BlobCache::onLoadedResource, mCache);
            bb.indexBuffer->setBuffer(*mEngine, std::move(bd));
        } else if (bb.animationBuffer) {
            memcpy(bb.animationBuffer, ucdata, bb.size);
        } else if (bb.orientationBuffer) {
            memcpy(bb.orientationBuffer, ucdata, bb.size);
        } else {
            slog.e << "Malformed binding: " << bb.uri << io::endl;
            return false;
        }
    }

    // Compute surface orientation quaternions if necessary.
    if (fasset->mOrientationBuffer.size() > 0) {
        computeTangents(fasset);
    }
    return true;
}

void ResourceLoader::computeTangents(const FFilamentAsset* asset) {
    // Build a map of URI strings to blob pointers. TODO: can the key be const char* ?
    UrlMap blobs;
    const BufferBinding* bindings = asset->getBufferBindings();
    for (size_t i = 0, n = asset->getBufferBindingCount(); i < n; ++i) {
        auto bb = bindings[i];
        if (bb.orientationBuffer) {
            blobs[bb.uri] = bb.orientationBuffer;
        }
    }

    // Declare a vector of quats (which we will populate via populateTangentQuaternions)
    // as well as vectors of normals and tangents (which we'll extract & convert from the source)
    std::vector<float3> fp32Normals;
    std::vector<float4> fp32Tangents;

    auto computeQuats = [&](const cgltf_primitive& prim) {

        // Iterate through the attributes and find the normals and tangents (if any).
        cgltf_size normalsSlot = 0;
        cgltf_size vertexCount = 0;
        const uint8_t* normalsBlob = nullptr;
        const cgltf_accessor* normalsInfo = nullptr;
        const uint8_t* tangentsBlob = nullptr;
        const cgltf_accessor* tangentsInfo = nullptr;
        for (cgltf_size slot = 0; slot < prim.attributes_count; slot++) {
            const cgltf_attribute& attr = prim.attributes[slot];
            vertexCount = attr.data->count;
            const char* uri = attr.data->buffer_view->buffer->uri;
            if (attr.type == cgltf_attribute_type_normal) {
                normalsSlot = slot;
                normalsBlob = blobs[uri];
                normalsInfo = attr.data;
                continue;
            }
            if (attr.type == cgltf_attribute_type_tangent) {
                tangentsBlob = blobs[uri];
                tangentsInfo = attr.data;
                continue;
            }
        }
        if (normalsBlob == nullptr || vertexCount == 0) {
            return;
        }

        // Allocate space for the input and output of the tangent computation.
        fp32Normals.resize(vertexCount);
        fp32Tangents.resize(tangentsBlob ? vertexCount : 0);
        quath* fp16Quats = (quath*) malloc(sizeof(quath) * vertexCount);

        // Convert normals (and possibly tangents) into floating point.
        assert(normalsInfo->count == vertexCount);
        assert(normalsInfo->type == cgltf_type_vec3);
        cgltf_accessor_convert_buffer_data(normalsInfo, normalsBlob, &fp32Normals.data()->x);
        if (tangentsInfo) {
            assert(tangentsInfo->count == vertexCount);
            assert(tangentsInfo->type == cgltf_type_vec4);
            cgltf_accessor_convert_buffer_data(tangentsInfo, tangentsBlob, &fp32Tangents.data()->x);
        }

        // Compute surface orientation quaternions.
        VertexBuffer::QuatTangentContext ctx {
            .quatType = VertexBuffer::HALF4,
            .quatCount = vertexCount,
            .outBuffer = fp16Quats,
            .normals = fp32Normals.data(),
            .tangents = fp32Tangents.empty() ? nullptr : fp32Tangents.data()
        };
        VertexBuffer::populateTangentQuaternions(ctx);

        // Upload quaternions to the GPU.
        auto callback = (VertexBuffer::BufferDescriptor::Callback) free;
        VertexBuffer::BufferDescriptor bd(fp16Quats, vertexCount, callback);
        VertexBuffer* vb = asset->mPrimMap.at(&prim);
        vb->setBufferAt(*mEngine, normalsSlot, std::move(bd));
    };

    for (auto iter : asset->mNodeMap) {
        const cgltf_mesh* mesh = iter.first->mesh;
        if (mesh) {
            cgltf_size nprims = mesh->primitives_count;
            for (cgltf_size index = 0; index < nprims; ++index) {
                computeQuats(mesh->primitives[index]);
            }
        }
    }
}

} // namespace gltfio

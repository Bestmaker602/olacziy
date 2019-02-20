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
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>

#include <math/quat.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/Log.h>

#include <cgltf.h>

#include <stb_image.h>

#include <tsl/robin_map.h>

#include <string>

using namespace filament;
using namespace filament::math;
using namespace utils;

namespace gltfio {
namespace details {

// The AssetPool tracks references to raw source data (cgltf hierarchies) and frees them
// appropriately. It releases all source assets only after the pending upload count is zero and the
// client has destroyed the ResourceLoader object. If the ResourceLoader is destroyed while uploads
// are still pending, then the AssetPool will stay alive until all uploads are complete.
class AssetPool {
public:
    AssetPool()  {}
    ~AssetPool() {
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
    static void onLoadedResource(void* buffer, size_t size, void* user) {
        auto pool = (AssetPool*) user;
        if (--pool->mPendingUploads == 0 && pool->mLoaderDestroyed) {
            delete pool;
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

} // namespace details

using namespace details;

ResourceLoader::ResourceLoader(Engine* engine, const char* basePath) : mEngine(engine),
        mBasePath(basePath), mPool(new AssetPool) {}

ResourceLoader::~ResourceLoader() {
    mPool->onLoaderDestroyed();
}

bool ResourceLoader::loadResources(FilamentAsset* asset) {
    FFilamentAsset* fasset = upcast(asset);
    mPool->addAsset(fasset);
    auto gltf = (cgltf_data*) fasset->mSourceAsset;

    // Read data from the file system and base64 URLs.
    cgltf_options options {};
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
            mPool->addPendingUpload();
            VertexBuffer::BufferDescriptor bd(ucdata, bb.size, AssetPool::onLoadedResource, mPool);
            bb.vertexBuffer->setBufferAt(*mEngine, bb.bufferIndex, std::move(bd));
        } else if (bb.indexBuffer) {
            mPool->addPendingUpload();
            VertexBuffer::BufferDescriptor bd(ucdata, bb.size, AssetPool::onLoadedResource, mPool);
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

    // Finally, load image files and create Filament Textures.
    return createTextures(fasset);
}

bool ResourceLoader::createTextures(const details::FFilamentAsset* asset) {
    // Define a simple functor that creates a Filament Texture from a blob of texels.
    // TODO: this could be optimized, e.g. do not generate mips if never mipmap-sampled, and use a
    // more compact format when possible.
    auto createTexture = [this](stbi_uc* texels, uint32_t w, uint32_t h, bool srgb) {
        Texture *tex = Texture::Builder()
                .width(w)
                .height(h)
                .levels(0xff)
                .format(srgb ? driver::TextureFormat::SRGB8_A8 : driver::TextureFormat::RGBA8)
                .build(*mEngine);

        Texture::PixelBufferDescriptor pbd(texels,
                size_t(w * h * 4),
                Texture::Format::RGBA,
                Texture::Type::UBYTE,
                (driver::BufferDescriptor::Callback) &free);

        tex->setImage(*mEngine, 0, std::move(pbd));
        tex->generateMipmaps(*mEngine);
        return tex;
    };

    // Decode textures and associate them with material instance parameters.
    // To prevent needless re-decoding, we create a small map of Filament Texture objects where the
    // map key is (usually) a pointer to a URI string. In the case of buffer view textures, the
    // cache key is a data pointer.
    stbi_uc* texels;
    int width, height, comp;
    Texture* tex;
    tsl::robin_map<void*, Texture*> textures;
    const TextureBinding* texbindings = asset->getTextureBindings();
    for (size_t i = 0, n = asset->getTextureBindingCount(); i < n; ++i) {
        auto tb = texbindings[i];
        if (tb.data) {
            tex = textures[tb.data];
            if (!tex) {
                texels = stbi_load_from_memory((const stbi_uc*) *tb.data, tb.totalSize,
                        &width, &height, &comp, 4);
                if (texels == nullptr) {
                    slog.e << "Unable to decode texture. " << io::endl;
                    return false;
                }
                textures[tb.data] = tex = createTexture(texels, width, height, tb.srgb);
            }
        } else {
            tex = textures[(void*) tb.uri];
            if (!tex) {
                utils::Path fullpath = this->mBasePath + tb.uri;
                texels = stbi_load(fullpath.c_str(), &width, &height, &comp, 4);
                if (texels == nullptr) {
                    slog.e << "Unable to decode texture: " << tb.uri << io::endl;
                    return false;
                }
                textures[(void*) tb.uri] = tex = createTexture(texels, width, height, tb.srgb);
            }
        }
        tb.materialInstance->setParameter(tb.materialParameter, tex, tb.sampler);
    }
    return true;
}

void ResourceLoader::computeTangents(const FFilamentAsset* asset) {
    // Declare vectors of normals and tangents, which we'll extract & convert from the source.
    std::vector<float3> fp32Normals;
    std::vector<float4> fp32Tangents;

    auto computeQuats = [&](const cgltf_primitive& prim) {

        // Iterate through the attributes and find the normals and tangents (if any).
        cgltf_size normalsSlot = 0;
        cgltf_size vertexCount = 0;
        const cgltf_accessor* normalsInfo = nullptr;
        const cgltf_accessor* tangentsInfo = nullptr;
        for (cgltf_size slot = 0; slot < prim.attributes_count; slot++) {
            const cgltf_attribute& attr = prim.attributes[slot];
            vertexCount = attr.data->count;
            const char* uri = attr.data->buffer_view->buffer->uri;
            if (attr.type == cgltf_attribute_type_normal) {
                normalsSlot = slot;
                normalsInfo = attr.data;
                continue;
            }
            if (attr.type == cgltf_attribute_type_tangent) {
                tangentsInfo = attr.data;
                continue;
            }
        }
        if (normalsInfo == nullptr || vertexCount == 0) {
            return;
        }

        // Allocate space for the input and output of the tangent computation.
        fp32Normals.resize(vertexCount);
        fp32Tangents.resize(tangentsInfo ? vertexCount : 0);
        ushort4* fp16Quats = (ushort4*) malloc(sizeof(ushort4) * vertexCount);

        // Convert normals (and possibly tangents) into floating point.
        assert(normalsInfo->count == vertexCount);
        assert(normalsInfo->type == cgltf_type_vec3);
        for (cgltf_size i = 0; i < vertexCount; ++i) {
            fp32Normals[i].x = cgltf_accessor_read_float(normalsInfo, i, 0);
            fp32Normals[i].y = cgltf_accessor_read_float(normalsInfo, i, 1);
            fp32Normals[i].z = cgltf_accessor_read_float(normalsInfo, i, 2);
        }
        if (tangentsInfo) {
            assert(tangentsInfo->count == vertexCount);
            assert(tangentsInfo->type == cgltf_type_vec4);
            for (cgltf_size i = 0; i < vertexCount; ++i) {
                fp32Tangents[i].x = cgltf_accessor_read_float(tangentsInfo, i, 0);
                fp32Tangents[i].y = cgltf_accessor_read_float(tangentsInfo, i, 1);
                fp32Tangents[i].z = cgltf_accessor_read_float(tangentsInfo, i, 2);
                fp32Tangents[i].w = cgltf_accessor_read_float(tangentsInfo, i, 3);
            }
        }

        // Compute surface orientation quaternions.
        VertexBuffer::QuatTangentContext ctx {
            .quatType = VertexBuffer::SHORT4,
            .quatCount = vertexCount,
            .outBuffer = fp16Quats,
            .normals = fp32Normals.data(),
            .tangents = fp32Tangents.empty() ? nullptr : fp32Tangents.data()
        };
        VertexBuffer::populateTangentQuaternions(ctx);

        // Upload quaternions to the GPU.
        auto callback = (VertexBuffer::BufferDescriptor::Callback) free;
        VertexBuffer::BufferDescriptor bd(fp16Quats, vertexCount * sizeof(ushort4), callback);
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

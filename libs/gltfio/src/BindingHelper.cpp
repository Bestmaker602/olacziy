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

#include <gltfio/BindingHelper.h>

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>

#include <utils/Log.h>

#include <cgltf.h>

#include <tsl/robin_map.h>

#include <string>

// TODO: to simplify the implementation of BindingHelper, we are using cgltf_load_buffer_base64 and
// cgltf_load_buffer_file, which are normally private to the library. We should consider
// substituting these functions with our own implementation since they are fairly simple.

using namespace filament;
using namespace utils;

namespace gltfio {

class UrlCache {
public:
    void* getResource(const char* uri) {
        auto iter = mBlobs.find(uri);
        return (iter == mBlobs.end()) ? nullptr : iter->second;
    }

    void addResource(const char* uri, void* blob) {
        mBlobs[uri] = blob;
    }

    void addPendingUpload() {
        ++mPendingUploads;
    }

    UrlCache() {}

    ~UrlCache() {
        // TODO: free all mBlobs
    }

    // Destroy the URL cache only after the pending upload count is zero and the client has
    // destroyed the BindingHelper object.
    static void onLoadedResource(void* buffer, size_t size, void* user) {
        auto cache = (UrlCache*) user;
        if (--cache->mPendingUploads == 0 && cache->mOwnerDestroyed) {
            delete cache;
        }
    }

    void onOwnerDestroyed() {
        if (mPendingUploads == 0) {
            delete this;
        } else {
            mOwnerDestroyed = true;
        }
    }

private:
    bool mOwnerDestroyed = false;
    int mPendingUploads = 0;
    tsl::robin_map<std::string, void*> mBlobs; // TODO: can we simply use const char* for the key?
};

BindingHelper::BindingHelper(Engine* engine, const char* basePath) : mEngine(engine),
        mBasePath(basePath), mCache(new UrlCache) {}

BindingHelper::~BindingHelper() {
    mCache->onOwnerDestroyed();
}

bool BindingHelper::loadResources(FilamentAsset* asset) {
    const BufferBinding* bindings = asset->getBufferBindings();
    for (size_t i = 0, n = asset->getBufferBindingCount(); i < n; ++i) {
        auto bb = bindings[i];
        void* data = mCache->getResource(bb.uri);
        if (data) {
            // Do nothing.
        } else if (isBase64(bb)) {
            data = loadBase64(bb);
            mCache->addResource(bb.uri, data);
        } else if (isFile(bb)) {
            data = loadFile(bb);
            mCache->addResource(bb.uri, data);
        } else {
            slog.e << "Unable to obtain resource: " << bb.uri << io::endl;
            return false;
        }
        uint8_t* ucdata = bb.offset + (uint8_t*) data;
        if (bb.vertexBuffer) {
            mCache->addPendingUpload();
            VertexBuffer::BufferDescriptor bd(ucdata, bb.size, UrlCache::onLoadedResource, mCache);
            bb.vertexBuffer->setBufferAt(*mEngine, bb.bufferIndex, std::move(bd));
        } else if (bb.indexBuffer) {
            mCache->addPendingUpload();
            VertexBuffer::BufferDescriptor bd(ucdata, bb.size, UrlCache::onLoadedResource, mCache);
            bb.indexBuffer->setBuffer(*mEngine, std::move(bd));
        } else if (bb.animationBuffer) {
            memcpy(bb.animationBuffer, ucdata, bb.size);
        } else {
            slog.e << "Malformed binding: " << bb.uri << io::endl;
            return false;
        }
    }
    return true;
}

bool BindingHelper::isBase64(const BufferBinding& bb) {
   if (bb.uri && strncmp(bb.uri, "data:", 5) == 0) {
        const char* comma = strchr(bb.uri, ',');
        if (comma && comma - bb.uri >= 7 && strncmp(comma - 7, ";base64", 7) == 0) {
            return true;
        }
    }
    return false;
}

void* BindingHelper::loadBase64(const BufferBinding& bb) {
    if (!bb.uri || strncmp(bb.uri, "data:", 5)) {
        return nullptr;
    }
    const char* comma = strchr(bb.uri, ',');
    if (comma && comma - bb.uri >= 7 && strncmp(comma - 7, ";base64", 7) == 0) {
        cgltf_options options {};
        void* data = nullptr;
        cgltf_result result = cgltf_load_buffer_base64(
                &options, bb.totalSize, comma + 1, &data);
        if (result != cgltf_result_success) {
            slog.e << "Unable to parse base64 URL." << io::endl;
            return nullptr;
        }
        return data;
    }
    return nullptr;
}

bool BindingHelper::isFile(const BufferBinding& bb) {
    return strstr(bb.uri, "://") == nullptr;
}

void* BindingHelper::loadFile(const BufferBinding& bb) {
    cgltf_options options {};
    void* data = nullptr;
    cgltf_result result = cgltf_load_buffer_file(
            &options, bb.totalSize, bb.uri, mBasePath.c_str(), &data);
    if (result != cgltf_result_success) {
        slog.e << "Unable to consume " << bb.uri << io::endl;
        return nullptr;
    }
    return data;
}

} // namespace gltfio

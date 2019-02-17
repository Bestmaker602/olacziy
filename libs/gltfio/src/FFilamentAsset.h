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

#ifndef GLTFIO_FFILAMENTASSET_H
#define GLTFIO_FFILAMENTASSET_H

#include <gltfio/FilamentAsset.h>
#include <gltfio/Animator.h>

#include <filament/Engine.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>

#include <math/mat4.h>

#include <utils/Entity.h>

#include <cgltf.h>

#include "upcast.h"

#include <tsl/robin_map.h>

#include <set>
#include <string>
#include <vector>

namespace gltfio {
namespace details {

struct Skin {
    std::string name;
    filament::TransformManager::Instance skeleton;
    std::vector<filament::math::mat4f> inverseBindMatrices;
    std::vector<filament::TransformManager::Instance> joints;
    std::vector<filament::RenderableManager::Instance> targets;
};

struct FFilamentAsset : public FilamentAsset {
    FFilamentAsset(filament::Engine* engine) : mEngine(engine) {}

    ~FFilamentAsset() {
        releaseSourceData();
        delete mAnimator;
        mEngine->destroy(mRoot);
        for (auto entity : mEntities) {
            mEngine->destroy(entity);
        }
        for (auto mi : mMaterialInstances) {
            mEngine->destroy(mi);
        }
    }

    size_t getEntityCount() const noexcept {
        return mEntities.size();
    }

    const utils::Entity* getEntities() const noexcept {
        return mEntities.data();
    }

    utils::Entity getRoot() const noexcept {
        return mRoot;
    }

    size_t getMaterialInstanceCount() const noexcept {
        return mMaterialInstances.size();
    }

    const filament::MaterialInstance* const* getMaterialInstances() const noexcept {
        return mMaterialInstances.data();
    }

    size_t getBufferBindingCount() const noexcept {
        return mBufferBindings.size();
    }

    const BufferBinding* getBufferBindings() const noexcept {
        return mBufferBindings.data();
    }

    size_t getTextureBindingCount() const noexcept {
        return mTextureBindings.size();
    }

    const TextureBinding* getTextureBindings() const noexcept {
        return mTextureBindings.data();
    }

    filament::Aabb getBoundingBox() const noexcept {
        return mBoundingBox;
    }

    Animator* createAnimator() noexcept {
        if (!mAnimator) {
            mAnimator = new Animator(this);
        }
        return mAnimator;
    }

    void releaseSourceData() noexcept {
        mBufferBindings.clear();
        mBufferBindings.shrink_to_fit();
        mTextureBindings.clear();
        mTextureBindings.shrink_to_fit();
        mAnimationBuffer.clear();
        mAnimationBuffer.shrink_to_fit();
        mOrientationBuffer.clear();
        mOrientationBuffer.shrink_to_fit();
        cgltf_free((cgltf_data*) mSourceAsset);
        mSourceAsset = nullptr;
        mNodeMap.clear();
        mPrimMap.clear();
    }

    filament::Engine* mEngine;
    std::vector<utils::Entity> mEntities;
    std::vector<filament::MaterialInstance*> mMaterialInstances;
    filament::Aabb mBoundingBox;
    utils::Entity mRoot;
    std::vector<Skin> mSkins;
    Animator* mAnimator = nullptr;

    /** @{
     * Transient source data that can freed via releaseSourceData(). */
    std::vector<BufferBinding> mBufferBindings;
    std::vector<TextureBinding> mTextureBindings;
    std::vector<uint8_t> mAnimationBuffer;
    std::vector<uint8_t> mOrientationBuffer;
    const cgltf_data* mSourceAsset = nullptr;
    tsl::robin_map<const cgltf_node*, utils::Entity> mNodeMap;
    tsl::robin_map<const cgltf_primitive*, filament::VertexBuffer*> mPrimMap;
    /** @} */
};

FILAMENT_UPCAST(FilamentAsset)

} // namespace details
} // namespace gltfio

#endif // GLTFIO_FFILAMENTASSET_H

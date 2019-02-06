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

#ifndef GLTFIO_MATERIALGENERATOR_H
#define GLTFIO_MATERIALGENERATOR_H

#include <filament/Engine.h>
#include <filament/Material.h>

#include <utils/Hash.h>

#include <tsl/robin_map.h>

namespace gltfio {
namespace details {

enum class AlphaMode : uint8_t {
    OPAQUE,
    MASKED,
    TRANSPARENT
};

struct MaterialKey {
    bool doubleSided : 1;
    bool unlit : 1;
    bool hasVertexColors : 1;
    bool hasBaseColorTexture : 1;
    bool hasMetallicRoughnessTexture : 1;
    bool hasNormalTexture : 1;
    bool hasOcclusionTexture : 1;
    bool hasEmissiveTexture : 1;
    AlphaMode alphaMode;
    uint8_t baseColorUV;
    uint8_t metallicRoughnessUV;
    uint8_t emissiveUV;
    uint8_t aoUV;
    uint8_t normalUV;
    float alphaMaskThreshold;
};

// The MaterialGenerator uses filamat to generate properly configured Filament materials that each
// make the required number of texture lookups. This complexity could be avoided if we were to use
// an ubershader approach, but this allows us to generate efficient and streamlined shaders that
// have no branching.
class MaterialGenerator final {
public:
    MaterialGenerator(filament::Engine* engine);

    // The passed-in cache key might be mutated by the implementation due to resource constraints.
    // For example, Filament only allows two sets of UV coordinates but glTF allows five.
    filament::Material* getOrCreateMaterial(MaterialKey* config);

    size_t getMaterialsCount() const noexcept;
    const filament::Material* const* getMaterials() const noexcept;
    void destroyMaterials();
private:
    using HashFn = utils::hash::MurmurHashFn<MaterialKey>;
    struct EqualFn { bool operator()(const MaterialKey& k1, const MaterialKey& k2) const; };
    tsl::robin_map<MaterialKey, filament::Material*, HashFn, EqualFn> mCache;
    std::vector<filament::Material*> mMaterials;
    filament::Engine* mEngine;
};

} // namespace details
} // namespace gltfio

#endif // GLTFIO_MATERIALGENERATOR_H

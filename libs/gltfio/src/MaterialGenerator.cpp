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

#include "MaterialGenerator.h"

#include <filamat/MaterialBuilder.h>

#include <utils/Log.h>

#include <string>

using namespace filamat;
using namespace filament;
using namespace utils;

namespace gltfio {
namespace details {

bool MaterialGenerator::EqualFn::operator()(const MaterialKey& k1, const MaterialKey& k2) const {
    return
        (k1.doubleSided == k2.doubleSided) &&
        (k1.unlit == k2.unlit) &&
        (k1.hasVertexColors == k2.hasVertexColors) &&
        (k1.hasBaseColorTexture == k2.hasBaseColorTexture) &&
        (k1.hasMetallicRoughnessTexture == k2.hasMetallicRoughnessTexture) &&
        (k1.hasNormalTexture == k2.hasNormalTexture) &&
        (k1.hasOcclusionTexture == k2.hasOcclusionTexture) &&
        (k1.hasEmissiveTexture == k2.hasEmissiveTexture) &&
        (k1.alphaMode == k2.alphaMode) &&
        (k1.baseColorUV == k2.baseColorUV) &&
        (k1.metallicRoughnessUV == k2.metallicRoughnessUV) &&
        (k1.emissiveUV == k2.emissiveUV) &&
        (k1.aoUV == k2.aoUV) &&
        (k1.normalUV == k2.normalUV) &&
        (k1.alphaMaskThreshold == k2.alphaMaskThreshold);
}

MaterialGenerator::MaterialGenerator(Engine* engine) : mEngine(engine) {}

size_t MaterialGenerator::getMaterialsCount() const noexcept {
    return mMaterials.size();
}

const Material* const* MaterialGenerator::getMaterials() const noexcept {
    return mMaterials.data();
}

void MaterialGenerator::destroyMaterials() {
    for (auto& iter : mCache) {
        mEngine->destroy(iter.second);
    }
    mMaterials.clear();
    mCache.clear();
}

static std::string shaderFromKey(MaterialKey config) {
    const auto normalUV = std::to_string(config.normalUV);
    const auto baseColorUV = std::to_string(config.baseColorUV);
    const auto metallicRoughnessUV = std::to_string(config.metallicRoughnessUV);
    const auto emissiveUV = std::to_string(config.emissiveUV);
    const auto aoUV = std::to_string(config.aoUV);

    std::string shader = R"SHADER(
        void material(inout MaterialInputs material) {
    )SHADER";

    // TODO: apply materialParams_normalScale and materialParams_aoStrength

    if (config.hasNormalTexture && !config.unlit) {
        shader += "float2 normalUV = getUV" + normalUV + "();\n";
        shader += R"SHADER(
            material.normal = texture(materialParams_normalMap, normalUV).xyz * 2.0 - 1.0;
            material.normal.y = -material.normal.y;
        )SHADER";
    }

    shader += R"SHADER(
        prepareMaterial(material);
        material.baseColor = materialParams.baseColorFactor;
    )SHADER";

    if (config.hasBaseColorTexture) {
        shader += "float2 baseColorUV = getUV" + baseColorUV + "();\n";
        shader += R"SHADER(
            material.baseColor *= texture(materialParams_baseColorMap, baseColorUV);
        )SHADER";
    }

    if (config.alphaMode == AlphaMode::TRANSPARENT) {
        shader += R"SHADER(
            material.baseColor.rgb *= material.baseColor.a;
        )SHADER";
    }

    if (!config.unlit) {
        shader += R"SHADER(
            material.roughness = materialParams.roughnessFactor;
            material.metallic = materialParams.metallicFactor;
            material.emissive.rgb = materialParams.emissiveFactor.rgb;
        )SHADER";
        if (config.hasMetallicRoughnessTexture) {
            shader += "float2 metallicRoughnessUV = getUV" + metallicRoughnessUV + "();\n";
            shader += R"SHADER(
                vec4 roughness = texture(materialParams_metallicRoughnessMap, metallicRoughnessUV);
                material.roughness *= roughness.g;
                material.metallic *= roughness.b;
            )SHADER";
        }
        if (config.hasOcclusionTexture) {
            shader += "float2 aoUV = getUV" + aoUV + "();\n";
            shader += R"SHADER(
                material.ambientOcclusion = texture(materialParams_occlusionMap, aoUV).r;
            )SHADER";
        }
        if (config.hasEmissiveTexture) {
            shader += "float2 emissiveUV = getUV" + emissiveUV + "();\n";
            shader += R"SHADER(
                material.emissive *= texture(materialParams_emissiveMap, emissiveUV);
            )SHADER";
        }
    }

    shader += "}\n";
    return shader;
}

static Material* createMaterial(Engine* engine, MaterialKey& config) {

    // Filament only supports 2 UV sets while glTF supports 5. In practice this usually doesn't
    // matter, just print a warning and drop textures gracefully.
    uint8_t maxUVIndex = std::max({config.baseColorUV, config.metallicRoughnessUV,
            config.emissiveUV, config.aoUV, config.normalUV});
    if (maxUVIndex > 1) {
        slog.w << "More than two UV sets are not supported.\n";
        if (config.baseColorUV > 1) {
            config.hasBaseColorTexture = false;
        }
        if (config.metallicRoughnessUV > 1) {
            config.hasMetallicRoughnessTexture = false;
        }
        if (config.normalUV > 1) {
            config.hasNormalTexture = false;
        }
        if (config.aoUV > 1) {
            config.hasOcclusionTexture = false;
        }
        if (config.emissiveUV > 1) {
            config.hasEmissiveTexture = false;
        }
    }

    int numTextures = 0;
    if (config.hasBaseColorTexture) ++numTextures;
    if (config.hasMetallicRoughnessTexture) ++numTextures;
    if (config.hasNormalTexture) ++numTextures;
    if (config.hasOcclusionTexture) ++numTextures;
    if (config.hasEmissiveTexture) ++numTextures;

    std::string shader = shaderFromKey(config);
    MaterialBuilder builder = MaterialBuilder()
            .name("material")
            .material(shader.c_str())
            .culling(MaterialBuilder::CullingMode::NONE) // TODO depend on doubleSided
            .doubleSided(config.doubleSided);

    if (numTextures > 0) {
        builder.require(VertexAttribute::UV0);
    }

    if (maxUVIndex > 0) {
        builder.require(VertexAttribute::UV1);
    }

    // BASE COLOR
    builder.parameter(MaterialBuilder::UniformType::FLOAT4, "baseColorFactor");
    if (config.hasBaseColorTexture) {
        builder.parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "baseColorMap");
    }

    // METALLIC-ROUGHNESS
    builder.parameter(MaterialBuilder::UniformType::FLOAT, "metallicFactor");
    builder.parameter(MaterialBuilder::UniformType::FLOAT, "roughnessFactor");
    if (config.hasMetallicRoughnessTexture) {
        builder.parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "metallicRoughnessMap");
    }

    // NORMAL MAP
    // In the glTF spec normalScale is in normalTextureInfo; in cgltf it is part of texture_view.
    builder.parameter(MaterialBuilder::UniformType::FLOAT, "normalScale");
    if (config.hasNormalTexture) {
        builder.parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "normalMap");
    }

    // AMBIENT OCCLUSION
    // In the glTF spec aoStrength is in occlusionTextureInfo; in cgltf it is part of texture_view.
    builder.parameter(MaterialBuilder::UniformType::FLOAT, "aoStrength");
    if (config.hasOcclusionTexture) {
        builder.parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "occlusionMap");
    }

    // EMISSIVE
    builder.parameter(MaterialBuilder::UniformType::FLOAT3, "emissiveFactor");
    if (config.hasEmissiveTexture) {
        builder.parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "emissiveMap");
    }

    switch(config.alphaMode) {
        case AlphaMode::MASKED:
            builder.blending(MaterialBuilder::BlendingMode::MASKED);
            builder.maskThreshold(config.alphaMaskThreshold);
            break;
        case AlphaMode::TRANSPARENT:
            builder.blending(MaterialBuilder::BlendingMode::TRANSPARENT);
            break;
        default:
            builder.blending(MaterialBuilder::BlendingMode::OPAQUE);
    }

    builder.shading(config.unlit ? Shading::UNLIT : Shading::LIT);

    Package pkg = builder.build();
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(*engine);
}

Material* MaterialGenerator::getOrCreateMaterial(MaterialKey* config) {
    auto iter = mCache.find(*config);
    if (iter == mCache.end()) {
        Material* mat = createMaterial(mEngine, *config);
        mCache.emplace(std::make_pair(*config, mat));
        mMaterials.push_back(mat);
        return mat;
    }
    return iter->second;
}

} // namespace details
} // namespace gltfio

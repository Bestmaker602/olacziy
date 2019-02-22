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

#include <geometry/SurfaceOrientation.h>

#include <utils/Panic.h>

#include <math/mat3.h>
#include <math/norm.h>

#include <vector>

namespace geometry {

using namespace filament::math;
using std::vector;
using Builder = SurfaceOrientation::Builder;

struct OrientationBuilderImpl {
    size_t vertexCount = 0;
    const float3* normals = 0;
    const float4* tangents = 0;
    const float2* uvs = 0;
    const float3* positions = 0;
    const uint3* triangles32 = 0;
    const ushort3* triangles16 = 0;
    size_t normalStride = 0;
    size_t tangentStride = 0;
    size_t uvStride = 0;
    size_t positionStride = 0;
    SurfaceOrientation* buildWithNormalsOnly();
    SurfaceOrientation* buildWithSuppliedTangents();
    SurfaceOrientation* buildWithUvs();
};

struct OrientationImpl {
    vector<quatf> quaternions;
};

Builder::Builder() : mImpl(new OrientationBuilderImpl) {}

Builder::~Builder() noexcept { delete mImpl; }

Builder& Builder::vertexCount(uint32_t vertexCount) noexcept {
    mImpl->vertexCount = vertexCount;
    return *this;
}

Builder& Builder::normals(const float3* normals) noexcept {
    mImpl->normals = normals;
    return *this;
}

Builder& Builder::tangents(const float4* tangents) noexcept {
    mImpl->tangents = tangents;
    return *this;
}

Builder& Builder::uvs(const float2* uvs) noexcept {
    mImpl->uvs = uvs;
    return *this;
}

Builder& Builder::positions(const float3* positions) noexcept {
    mImpl->positions = positions;
    return *this;
}

Builder& Builder::triangles(const uint3* triangles) noexcept {
    ASSERT_PRECONDITION(mImpl->triangles16 == nullptr, "Triangles already supplied.");
    mImpl->triangles32 = triangles;
    return *this;
}

Builder& Builder::triangles(const ushort3* triangles) noexcept {
    ASSERT_PRECONDITION(mImpl->triangles32 == nullptr, "Triangles already supplied.");
    mImpl->triangles16 = triangles;
    return *this;
}

Builder& Builder::normalStride(size_t numBytes) noexcept {
    mImpl->normalStride = numBytes;
    return *this;
}

Builder& Builder::tangentStride(size_t numBytes) noexcept {
    mImpl->tangentStride = numBytes;
    return *this;
}

Builder& Builder::uvStride(size_t numBytes) noexcept {
    mImpl->uvStride = numBytes;
    return *this;
}

Builder& Builder::positionStride(size_t numBytes) noexcept {
    mImpl->positionStride = numBytes;
    return *this;
}

SurfaceOrientation* Builder::build() {
    ASSERT_PRECONDITION(mImpl->normals != nullptr, "Normals are required.");
    ASSERT_PRECONDITION(mImpl->vertexCount > 0, "Vertex count must be non-zero.");
    if (mImpl->tangents != nullptr) {
        return mImpl->buildWithSuppliedTangents();
    }
    if (mImpl->uvs == nullptr) {
        return mImpl->buildWithNormalsOnly();
    }
    bool hasTriangles = mImpl->triangles16 || mImpl->triangles32;
    ASSERT_PRECONDITION(hasTriangles && mImpl->positions,
            "When using UVs, positions and triangles are required.");
    return mImpl->buildWithUvs();
}

SurfaceOrientation* OrientationBuilderImpl::buildWithNormalsOnly() {
    vector<quatf> quats(vertexCount);

    const float3* normal = this->normals;
    size_t nstride = this->normalStride ? this->normalStride : sizeof(float3);

    for (size_t qindex = 0; qindex < vertexCount; ++qindex) {
        float3 n = *normal;
        float3 b = normalize(cross(n, float3{1, 0, 0}));
        float3 t = cross(n, b);
        quats[qindex] = mat3f::packTangentFrame({t, b, n});
        normal = (const float3*) (((const uint8_t*) normal) + nstride);
    }

    return new SurfaceOrientation(new OrientationImpl( { std::move(quats) } ));
}

SurfaceOrientation* OrientationBuilderImpl::buildWithSuppliedTangents() {
    vector<quatf> quats(vertexCount);
 
    const float3* normal = this->normals;
    size_t nstride = this->normalStride ? this->normalStride : sizeof(float3);
 
    const float3* tanvec = (const float3*) this->tangents;
    const float* tandir = &this->tangents->w;
    size_t tstride = this->tangentStride ? this->tangentStride : sizeof(float4);

    for (size_t qindex = 0; qindex < vertexCount; ++qindex) {
        float3 n = *normal;
        float3 t = *tanvec;
        float3 b = *tandir < 0 ? cross(t, n) : cross(n, t);
        quats[qindex] = mat3f::packTangentFrame({t, b, n});
        normal = (const float3*) (((const uint8_t*) normal) + nstride);
        tanvec = (const float3*) (((const uint8_t*) tanvec) + tstride);
        tandir = (const float*) (((const uint8_t*) tandir) + tstride);
    }
 
    return new SurfaceOrientation(new OrientationImpl( { std::move(quats) } ));
}

SurfaceOrientation* OrientationBuilderImpl::buildWithUvs() {
    vector<quatf> quats(vertexCount);
    // TODO
    return new SurfaceOrientation(new OrientationImpl( { std::move(quats) } ));
}

void SurfaceOrientation::destroy(SurfaceOrientation* so) { delete so; }

SurfaceOrientation::SurfaceOrientation(OrientationImpl* impl) : mImpl(impl) {}

SurfaceOrientation::~SurfaceOrientation() { delete mImpl; }

size_t SurfaceOrientation::getVertexCount() const noexcept {
    return mImpl->quaternions.size();
}

void SurfaceOrientation::getQuats(quatf* out, size_t nquats, size_t stride) const noexcept {
    const vector<quatf>& in = mImpl->quaternions;
    nquats = std::min(nquats, in.size());
    stride = stride ? stride : sizeof(decltype(*out));
    for (size_t i = 0; i < nquats; ++i) {
        *out = in[i];
        out = (decltype(out)) (((uint8_t*) out) + stride);
    }
}

void SurfaceOrientation::getQuats(short4* out, size_t nquats, size_t stride) const noexcept {
    const vector<quatf>& in = mImpl->quaternions;
    nquats = std::min(nquats, in.size());
    stride = stride ? stride : sizeof(decltype(*out));
    for (size_t i = 0; i < nquats; ++i) {
        *out = packSnorm16(in[i].xyzw);
        out = (decltype(out)) (((uint8_t*) out) + stride);
    }
}

void SurfaceOrientation::getQuats(quath* out, size_t nquats, size_t stride) const noexcept {
    const vector<quatf>& in = mImpl->quaternions;
    nquats = std::min(nquats, in.size());
    stride = stride ? stride : sizeof(decltype(*out));
    for (size_t i = 0; i < nquats; ++i) {
        *out = quath(in[i]);
        out = (decltype(out)) (((uint8_t*) out) + stride);
    }
}

} // namespace geometry

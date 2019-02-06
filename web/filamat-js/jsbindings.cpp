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

#include <filamat/MaterialBuilder.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <math/mat4.h>

#include <emscripten.h>
#include <emscripten/bind.h>

using namespace emscripten;
using namespace filamat;

// Many methods require a thin layer of C++ glue which is elegantly expressed with a lambda.
// However, passing a bare lambda into embind's daisy chain requires a cast to a function pointer.
#define EMBIND_LAMBDA(retval, arglist, impl) (retval (*) arglist) [] arglist impl

// Builder functions that return "this" have verbose binding declarations, this macro reduces
// the amount of boilerplate.
#define BUILDER_FUNCTION(name, btype, arglist, impl) \
        function(name, EMBIND_LAMBDA(btype*, arglist, impl), allow_raw_pointers())

namespace {

// For convenience, declare terse private aliases to nested types. This lets us avoid extremely
// verbose binding declarations.
using Parameter = MaterialBuilder::Parameter;

} // anonymous namespace

EMSCRIPTEN_BINDINGS(jsbindings_filamat) {

// MATH TYPES
// ----------
// Individual JavaScript objects for math types would be too heavy, so instead we simply accept
// array-like data using embind's "value_array" feature. We do not expose all our math functions
// under the assumption that JS clients will use glMatrix or something similar for math.

value_array<math::float2>("float2")
    .element(&math::float2::x)
    .element(&math::float2::y);

value_array<math::float3>("float3")
    .element(&math::float3::x)
    .element(&math::float3::y)
    .element(&math::float3::z);

value_array<math::float4>("float4")
    .element(&math::float4::x)
    .element(&math::float4::y)
    .element(&math::float4::z)
    .element(&math::float4::w);

// In JavaScript, a flat contiguous representation is best for matrices (see gl-matrix) so we
// need to define a small wrapper here.

struct flatmat4 {
    math::mat4f m;
    float& operator[](int i) { return m[i / 4][i % 4]; }
};

value_array<flatmat4>("mat4")
    .element(index< 0>()).element(index< 1>()).element(index< 2>()).element(index< 3>())
    .element(index< 4>()).element(index< 5>()).element(index< 6>()).element(index< 7>())
    .element(index< 8>()).element(index< 9>()).element(index<10>()).element(index<11>())
    .element(index<12>()).element(index<13>()).element(index<14>()).element(index<15>());

struct flatmat3 {
    math::mat3f m;
    float& operator[](int i) { return m[i / 3][i % 3]; }
};

value_array<flatmat3>("mat3")
    .element(index<0>()).element(index<1>()).element(index<2>())
    .element(index<3>()).element(index<4>()).element(index<5>())
    .element(index<6>()).element(index<7>()).element(index<8>());

class_<Package>("Package");

class_<MaterialBuilder>("MaterialBuilder")
    .function("name", &MaterialBuilder::name, allow_raw_pointers());
//    .function("build", &MaterialBuilder::build);

} // EMSCRIPTEN_BINDINGS

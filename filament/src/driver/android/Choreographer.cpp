/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "Choreographer.h"

#include <utils/compiler.h>
#include <utils/Log.h>

#include <dlfcn.h>

using namespace utils;

namespace filament {

template <typename T>
static void loadSymbol(T*& pfn, const char *symbol) noexcept {
    pfn = (T*)dlsym(RTLD_DEFAULT, symbol);
}

Choreographer::Choreographer() {
#if __ANDROID_API__ < 24
    loadSymbol(AChoreographer_getInstance,        "AChoreographer_getInstance");
    loadSymbol(AChoreographer_postFrameCallback,  "AChoreographer_postFrameCallback");
#endif
}

Choreographer::~Choreographer() = default;

bool Choreographer::init() {
#if __ANDROID_API__ < 24
    if (AChoreographer_getInstance)
#endif
    {
        mChoreographer = AChoreographer_getInstance();
        slog.d << "mChoreographer: " << mChoreographer << io::endl;
    }

#if __ANDROID_API__ < 24
    if (AChoreographer_postFrameCallback)
#endif
    {
        AChoreographer_postFrameCallback(mChoreographer, &cbVsync, this);
    }

    return mChoreographer != nullptr;
}

void Choreographer::cbVsync(long frameTimeNanos, void* data) noexcept {
    int64_t ns = frameTimeNanos;
    if (sizeof(long) != sizeof(int64_t)) {
        // this is working around an Android bug where frameTimeNanos should have been a int64_t
        ns = VSyncClock::now().time_since_epoch().count();
        ns = (ns & 0xFFFFFFFF00000000) | uint32_t(frameTimeNanos);
    }
    static_cast<Choreographer*>(data)->vsync(VSyncTimePoint(std::chrono::nanoseconds(ns)));
}

void Choreographer::vsync(VSyncTimePoint frameTime) noexcept {
    mLastVSyncTime = frameTime;
    AChoreographer_postFrameCallback(mChoreographer, &cbVsync, this);

    slog.d << frameTime.time_since_epoch().count() << io::endl;
}


} // namespace filament

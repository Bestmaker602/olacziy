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

#ifndef TNT_FILAMENT_DRIVER_ANDROID_CHOREOGRAPHER_H
#define TNT_FILAMENT_DRIVER_ANDROID_CHOREOGRAPHER_H

#include <android/choreographer.h>

#include <stdint.h>

#include <chrono>

namespace filament {

class Choreographer {
public:
    using VSyncClock = std::chrono::steady_clock;
    using VSyncTimePoint = std::chrono::time_point<VSyncClock, std::chrono::nanoseconds>;

    Choreographer();
    ~Choreographer();

    bool init();

    bool isValid() const {
        return (mChoreographer != nullptr);
    }

    VSyncTimePoint getLastVSyncTime() const {
        return mLastVSyncTime;
    }

private:
#if __ANDROID_API__ < 24
    AChoreographer* (*AChoreographer_getInstance)() = nullptr;
    void (*AChoreographer_postFrameCallback)(AChoreographer*, AChoreographer_frameCallback, void*) = nullptr;
#endif

    void vsync(VSyncTimePoint frameTime) noexcept;
    static void cbVsync(long frameTimeNanos, void* data) noexcept;

    AChoreographer* mChoreographer = nullptr;
    VSyncTimePoint mLastVSyncTime{};
};

} // namespace filament


#endif //TNT_FILAMENT_DRIVER_ANDROID_CHOREOGRAPHER_H

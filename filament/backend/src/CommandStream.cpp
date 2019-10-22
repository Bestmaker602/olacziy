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

#include "private/backend/CommandStream.h"

#include <utils/CallStack.h>
#include <utils/Log.h>
#include <utils/Profiler.h>
#include <utils/Systrace.h>

#include <functional>

using namespace utils;

namespace filament {
namespace backend {

// ------------------------------------------------------------------------------------------------
// A few utility functions for debugging...

inline void printParameterPack(io::ostream& out) { }

template<typename LAST>
static void printParameterPack(io::ostream& out, const LAST& t) { out << t; }

template<typename FIRST, typename... REMAINING>
static void printParameterPack(io::ostream& out, const FIRST& first, const REMAINING& ... rest) {
    out << first << ", ";
    printParameterPack(out, rest...);
}

static UTILS_NOINLINE UTILS_UNUSED std::string extractMethodName(std::string& command) noexcept {
    constexpr const char startPattern[] = "::Command<&(filament::backend::Driver::";
    auto pos = command.rfind(startPattern);
    auto end = command.rfind('(');
    pos += sizeof(startPattern) - 1;
    return command.substr(pos, end-pos);
}

// ------------------------------------------------------------------------------------------------

CommandStream::CommandStream(Driver& driver, CircularBuffer& buffer) noexcept
        : mDispatcher(&driver.getDispatcher()),
          mDriver(&driver),
          mCurrentBuffer(&buffer)
#ifndef NDEBUG
          , mThreadId(std::this_thread::get_id())
#endif
{
}

void CommandStream::execute(void* buffer) {
    SYSTRACE_CALL();

    Profiler profiler;

    if (SYSTRACE_TAG) {
        // we want to remove all this when tracing is completely disabled
        profiler.resetEvents(Profiler::EV_CPU_CYCLES | Profiler::EV_L1D_RATES  | Profiler::EV_BPU_RATES);
        profiler.start();
    }

    Driver& UTILS_RESTRICT driver = *mDriver;
    CommandBase* UTILS_RESTRICT base = static_cast<CommandBase*>(buffer);
    while (UTILS_LIKELY(base)) {
        base = base->execute(driver);
    }

    if (SYSTRACE_TAG) {
        // we want to remove all this when tracing is completely disabled
        UTILS_UNUSED Profiler::Counters counters = profiler.readCounters();
        SYSTRACE_VALUE32("GLThread (I)", counters.getInstructions());
        SYSTRACE_VALUE32("GLThread (C)", counters.getCpuCycles());
        SYSTRACE_VALUE32("GLThread (CPI x10)", counters.getCPI() * 10);
        SYSTRACE_VALUE32("GLThread (L1D HR%)", counters.getL1DHitRate() * 100);
        if (profiler.hasBranchRates()) {
            SYSTRACE_VALUE32("GLThread (BHR%)", counters.getBranchHitRate() * 100);
        } else {
            SYSTRACE_VALUE32("GLThread (BPU miss)", counters.getBranchMisses());
        }
    }
}

void CommandStream::queueCommand(std::function<void()> command) {
    new(allocateCommand(CustomCommand::align(sizeof(CustomCommand)))) CustomCommand(std::move(command));
}

template<typename... ARGS>
template<void (Driver::*METHOD)(ARGS...)>
template<std::size_t... I>
void CommandType<void (Driver::*)(ARGS...)>::Command<METHOD>::log(std::index_sequence<I...>) noexcept  {
#if DEBUG_COMMAND_STREAM
    static_assert(UTILS_HAS_RTTI, "DEBUG_COMMAND_STREAM can only be used with RTTI");
    std::string command = utils::CallStack::demangleTypeName(typeid(Command).name()).c_str();
    slog.d << extractMethodName(command) << " : size=" << sizeof(Command) << "\n\t";
    printParameterPack(slog.d, std::get<I>(mArgs)...);
    slog.d << io::endl;
#endif
}

template<typename... ARGS>
template<void (Driver::*METHOD)(ARGS...)>
void CommandType<void (Driver::*)(ARGS...)>::Command<METHOD>::log() noexcept  {
    log(std::make_index_sequence<std::tuple_size<SavedParameters>::value>{});
}

/*
 * When DEBUG_COMMAND_STREAM is activated, we need to explicitly instantiate the log() method
 * (this is because we don't want it in the header file)
 */

#if DEBUG_COMMAND_STREAM
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)
#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    template void CommandType<decltype(&Driver::methodName)>::Command<&Driver::methodName>::log() noexcept;
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    template void CommandType<decltype(&Driver::methodName##R)>::Command<&Driver::methodName##R>::log() noexcept;
#include "private/backend/DriverAPI.inc"
#endif

// ------------------------------------------------------------------------------------------------

void CustomCommand::execute(Driver&, CommandBase* base, intptr_t* next) noexcept {
    *next = CustomCommand::align(sizeof(CustomCommand));
    static_cast<CustomCommand*>(base)->mCommand();
    static_cast<CustomCommand*>(base)->~CustomCommand();
}

} // namespace backend
} // namespace filament


// ------------------------------------------------------------------------------------------------
// Stream operators for all types in DriverEnums.h
// (These must live outside of the filament namespace)
// ------------------------------------------------------------------------------------------------

using namespace filament;
using namespace backend;

#if !defined(NDEBUG) && (DEBUG_COMMAND_STREAM != false)

io::ostream& operator<<(io::ostream& out, const AttributeArray& type) {
    return out << "AttributeArray[" << type.max_size() << "]{}";
}

io::ostream& operator<<(io::ostream& out, const FaceOffsets& type) {
    return out << "FaceOffsets{"
           << type[0] << ", "
           << type[1] << ", "
           << type[2] << ", "
           << type[3] << ", "
           << type[4] << ", "
           << type[5] << "}";
}

io::ostream& operator<<(io::ostream& out, const RasterState& rs) {
    // TODO: implement decoding of enums
    return out << "RasterState{"
           << rs.culling << ", "
           << uint8_t(rs.blendEquationRGB) << ", "
           << uint8_t(rs.blendEquationAlpha) << ", "
           << uint8_t(rs.blendFunctionSrcRGB) << ", "
           << uint8_t(rs.blendFunctionSrcAlpha) << ", "
           << uint8_t(rs.blendFunctionDstRGB) << ", "
           << uint8_t(rs.blendFunctionDstAlpha) << "}";
}

io::ostream& operator<<(io::ostream& out, const TargetBufferInfo& tbi) {
    return out << "TargetBufferInfo{"
           << "h=" << tbi.handle << ", "
           << "level=" << tbi.level << ", "
           << "face=" << tbi.face << "}";
}

io::ostream& operator<<(io::ostream& out, const PolygonOffset& po) {
    return out << "PolygonOffset{"
           << "slope=" << po.slope << ", "
           << "constant=" << po.constant << "}";
}

io::ostream& operator<<(io::ostream& out, const PipelineState& ps) {
    return out << "PipelineState{"
           << "program=" << ps.program << ", "
           << "rasterState=" << ps.rasterState << ", "
           << "polygonOffset=" << ps.polygonOffset << "}";
}

UTILS_PRIVATE
io::ostream& operator<<(io::ostream& out, BufferDescriptor const& b) {
    out << "BufferDescriptor { buffer=" << b.buffer
        << ", size=" << b.size
        << ", callback=" << b.getCallback()
        << ", user=" << b.getUser() << " }";
    return out;
}

UTILS_PRIVATE
io::ostream& operator<<(io::ostream& out, PixelBufferDescriptor const& b) {
    BufferDescriptor const& base = static_cast<BufferDescriptor const&>(b);
    out << "PixelBufferDescriptor { " << base
        << ", left=" << b.left
        << ", top=" << b.top
        << ", stride=" << b.stride
        << ", format=" << b.format
        << ", type=" << b.type
        << ", alignment=" << b.alignment << " }";
    return out;
}

UTILS_PRIVATE
io::ostream& operator<<(io::ostream& out, filament::backend::Viewport const& viewport) {
    out << "Viewport{"
        << ", left=" << viewport.left
        << ", bottom=" << viewport.bottom
        << ", width=" << viewport.width
        << ", height=" << viewport.height << "}";
    return out;
}

UTILS_PRIVATE
io::ostream& operator<<(io::ostream& out, TargetBufferFlags flags) {
    // TODO: implement decoding of enum
    out << uint8_t(flags);
    return out;
}

UTILS_PRIVATE
io::ostream& operator<<(io::ostream& out, RenderPassParams const& params) {
    out << "RenderPassParams{"
        <<   "clear=" << params.flags.clear
        << ", discardStart=" << params.flags.discardStart
        << ", discardEnd=" << params.flags.discardEnd
        << ", left=" << params.viewport.left
        << ", bottom=" << params.viewport.bottom
        << ", width=" << params.viewport.width
        << ", height=" << params.viewport.height
        << ", clearColor=" << params.clearColor
        << ", clearDepth=" << params.clearDepth
        << ", clearStencil=" << params.clearStencil << "}";
    return out;
}

#endif // !NDEBUG

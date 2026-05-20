// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/Viewport.h>

#include <djv/App/App.h>
#include <djv/Models/ColorModel.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/SettingsModel.h>
#include <djv/Models/TimeUnitsModel.h>
#include <djv/Models/ViewportModel.h>

#include <tlRender/Timeline/Util.h>

#include <ftk/UI/ColorSwatch.h>
#include <ftk/UI/GridLayout.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/PushButton.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/Spacer.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/String.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <regex>
#include <string>

namespace djv
{
    namespace app
    {
        namespace
        {
            bool isSplitCompareMode(tl::Compare compare)
            {
                return tl::Compare::Horizontal == compare ||
                    tl::Compare::Vertical == compare ||
                    tl::Compare::Tile == compare;
            }

            const char* getSmokeRoleName(Viewport::Role role)
            {
                switch (role)
                {
                case Viewport::Role::Primary: return "Primary";
                case Viewport::Role::SplitA: return "SplitA";
                case Viewport::Role::SplitB: return "SplitB";
                default: return "Unknown";
                }
            }

            bool isSmokeRoleEnabled(Viewport::Role role, const char* envName)
            {
                const char* roles = std::getenv(envName);
                if (!roles || !roles[0])
                {
                    return true;
                }
                const std::string value(roles);
                return value.find(getSmokeRoleName(role)) != std::string::npos;
            }

            double getSmokeImageByteMean(const std::shared_ptr<ftk::Image>& image)
            {
                if (!image || !image->isValid() || 0 == image->getByteCount())
                {
                    return 0.0;
                }
                const uint8_t* data = image->getData();
                const size_t byteCount = image->getByteCount();
                const size_t sampleCount = std::min<size_t>(4096, byteCount);
                const size_t step = std::max<size_t>(1, byteCount / sampleCount);
                uint64_t sum = 0;
                size_t count = 0;
                for (size_t i = 0; i < byteCount && count < sampleCount; i += step, ++count)
                {
                    sum += data[i];
                }
                return count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
            }

            struct SmokeForwardState
            {
                std::mutex mutex;
                bool started = false;
                bool haveLastTime = false;
                double lastSeconds = 0.0;
                double lastFrameValue = 0.0;
                std::chrono::steady_clock::time_point lastFrameWallTime;
                int movingFrames = 0;
                int emptyFrames = 0;
                int invalidFrames = 0;
                int blackFrames = 0;
                int jumpFrames = 0;
                int stalls = 0;
                bool exitRequested = false;
            };

            SmokeForwardState& getSmokeForwardState()
            {
                static SmokeForwardState state;
                return state;
            }

            struct SmokeReverseState
            {
                std::mutex mutex;
                bool requested = false;
                bool primed = false;
                double lastFrame = 0.0;
                std::chrono::steady_clock::time_point lastFrameWallTime;
                int movingFrames = 0;
                int emptyFrames = 0;
                int invalidFrames = 0;
                bool exitRequested = false;
            };

            SmokeReverseState& getSmokeReverseState()
            {
                static SmokeReverseState state;
                return state;
            }

            struct SmokeSeekState
            {
                std::mutex mutex;
                bool requested = false;
                int targetFrame = 0;
                bool exitRequested = false;
            };

            SmokeSeekState& getSmokeSeekState()
            {
                static SmokeSeekState state;
                return state;
            }

            struct SmokeSpeedState
            {
                std::mutex mutex;
                bool started = false;
                bool exitRequested = false;
                double startFrame = 0.0;
                double lastFrame = 0.0;
                std::chrono::steady_clock::time_point startWallTime;
            };

            SmokeSpeedState& getSmokeSpeedState()
            {
                static SmokeSpeedState state;
                return state;
            }

            struct SmokeDualTotalState
            {
                struct Pane
                {
                    bool seen = false;
                    bool primed = false;
                    bool haveFrame = false;
                    double currentFrame = 0.0;
                    double currentSeconds = 0.0;
                    double lastFrame = 0.0;
                    int movingFrames = 0;
                };
                std::mutex mutex;
                bool started = false;
                bool seeking = false;
                bool playing = false;
                bool exitRequested = false;
                double targetFrame = 120.0;
                std::array<double, 2> targetSeconds = { 0.0, 0.0 };
                double maxSyncDiff = 0.0;
                double maxOffsetErrorSeconds = 0.0;
                int syncSamples = 0;
                int syncBadSamples = 0;
                int offsetSamples = 0;
                int offsetBadSamples = 0;
                std::array<Pane, 2> panes;
            };

            SmokeDualTotalState& getSmokeDualTotalState()
            {
                static SmokeDualTotalState state;
                return state;
            }

            int getSmokeSplitIndex(Viewport::Role role)
            {
                switch (role)
                {
                case Viewport::Role::SplitA: return 0;
                case Viewport::Role::SplitB: return 1;
                default: return -1;
                }
            }

            void smokeFirstFrame(
                Viewport::Role role,
                const ftk::Path& path,
                const OTIO_NS::RationalTime& time,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_FIRST_FRAME_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_FIRST_FRAME_ROLES"))
                {
                    return;
                }

                static std::atomic<bool> primaryReady(false);
                static std::atomic<bool> splitAReady(false);
                static std::atomic<bool> splitBReady(false);
                static std::atomic<bool> exitRequested(false);

                bool wasReady = false;
                bool newlyReady = false;
                if (Viewport::Role::Primary == role)
                {
                    newlyReady = primaryReady.compare_exchange_strong(wasReady, true);
                }
                else if (Viewport::Role::SplitA == role)
                {
                    newlyReady = splitAReady.compare_exchange_strong(wasReady, true);
                }
                else if (Viewport::Role::SplitB == role)
                {
                    newlyReady = splitBReady.compare_exchange_strong(wasReady, true);
                }

                if (!newlyReady)
                {
                    return;
                }

                int expectedCount = 1;
                if (const char* env = std::getenv("DJV_SMOKE_FIRST_FRAME_COUNT"))
                {
                    expectedCount = std::max(1, std::atoi(env));
                }
                const int readyCount =
                    (primaryReady.load() ? 1 : 0) +
                    (splitAReady.load() ? 1 : 0) +
                    (splitBReady.load() ? 1 : 0);
                std::cout << "DJV_SMOKE_FIRST_FRAME role=" << getSmokeRoleName(role) <<
                    " ready=" << readyCount << "/" << expectedCount <<
                    " time=" << time.value() << "/" << time.rate() <<
                    " path=" << path.get() << std::endl;

                if (readyCount >= expectedCount && !exitRequested.exchange(true))
                {
                    std::cout << "DJV_SMOKE_FIRST_FRAME_READY count=" << readyCount <<
                        " expected=" << expectedCount << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->exit();
                    }
                }
            }

            void smokeForwardPlayback(
                Viewport::Role role,
                const ftk::Path& path,
                const std::vector<tl::VideoFrame>& frames,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_FORWARD_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_FORWARD_ROLES"))
                {
                    return;
                }

                auto& state = getSmokeForwardState();
                if (frames.empty())
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (state.started && !state.exitRequested)
                    {
                        ++state.emptyFrames;
                        std::cout << "DJV_SMOKE_FORWARD_EMPTY role=" <<
                            getSmokeRoleName(role) <<
                            " empty=" << state.emptyFrames << std::endl;
                    }
                    return;
                }

                const auto& frame = frames.front();
                const bool invalidFrame = frame.layers.empty() ||
                    !frame.layers.front().image;
                if (invalidFrame)
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (state.started && !state.exitRequested)
                    {
                        ++state.invalidFrames;
                        std::cout << "DJV_SMOKE_FORWARD_INVALID role=" <<
                            getSmokeRoleName(role) <<
                            " invalid=" << state.invalidFrames <<
                            " time=" << frame.time.value() << "/" <<
                            frame.time.rate() << std::endl;
                    }
                    return;
                }

                int expectedFrames = 5;
                if (const char* env = std::getenv("DJV_SMOKE_FORWARD_FRAMES"))
                {
                    expectedFrames = std::max(1, std::atoi(env));
                }
                int maxFrameStep = 4;
                if (const char* env = std::getenv("DJV_SMOKE_FORWARD_MAX_STEP"))
                {
                    maxFrameStep = std::max(1, std::atoi(env));
                }
                double maxFrameInterval = 0.75;
                if (const char* env = std::getenv("DJV_SMOKE_FORWARD_MAX_INTERVAL"))
                {
                    maxFrameInterval = std::max(0.01, std::atof(env));
                }

                const double seconds =
                    frame.time.rate() > 0.0 ?
                    frame.time.value() / frame.time.rate() :
                    frame.time.value();
                const double imageMean = getSmokeImageByteMean(frame.layers.front().image);
                const auto wallNow = std::chrono::steady_clock::now();
                bool shouldStart = false;
                bool shouldExit = false;
                int readyFrames = 0;
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (!state.started)
                    {
                        state.started = true;
                        state.haveLastTime = true;
                        state.lastSeconds = seconds;
                        state.lastFrameValue = frame.time.value();
                        state.lastFrameWallTime = wallNow;
                        shouldStart = true;
                    }
                    else if (!state.exitRequested)
                    {
                        if (!state.haveLastTime || seconds > state.lastSeconds)
                        {
                            const double frameStep = frame.time.value() - state.lastFrameValue;
                            const std::chrono::duration<double> wallDiff =
                                wallNow - state.lastFrameWallTime;
                            ++state.movingFrames;
                            state.haveLastTime = true;
                            state.lastSeconds = seconds;
                            state.lastFrameValue = frame.time.value();
                            state.lastFrameWallTime = wallNow;
                            readyFrames = state.movingFrames;
                            if (state.movingFrames > 1 && imageMean <= 1.0)
                            {
                                ++state.blackFrames;
                                std::cout << "DJV_SMOKE_FORWARD_BLACK role=" <<
                                    getSmokeRoleName(role) <<
                                    " black=" << state.blackFrames <<
                                    " mean=" << imageMean <<
                                    " time=" << frame.time.value() << "/" <<
                                    frame.time.rate() << std::endl;
                            }
                            if (frameStep > maxFrameStep)
                            {
                                ++state.jumpFrames;
                                std::cout << "DJV_SMOKE_FORWARD_JUMP role=" <<
                                    getSmokeRoleName(role) <<
                                    " jumps=" << state.jumpFrames <<
                                    " step=" << frameStep <<
                                    " max=" << maxFrameStep <<
                                    " time=" << frame.time.value() << "/" <<
                                    frame.time.rate() << std::endl;
                            }
                            if (state.movingFrames > 1 &&
                                wallDiff.count() > maxFrameInterval)
                            {
                                ++state.stalls;
                                std::cout << "DJV_SMOKE_FORWARD_STALL role=" <<
                                    getSmokeRoleName(role) <<
                                    " stalls=" << state.stalls <<
                                    " interval=" << wallDiff.count() <<
                                    " max=" << maxFrameInterval <<
                                    " time=" << frame.time.value() << "/" <<
                                    frame.time.rate() << std::endl;
                            }
                            std::cout << "DJV_SMOKE_FORWARD_FRAME role=" <<
                                getSmokeRoleName(role) <<
                                " frames=" << state.movingFrames << "/" << expectedFrames <<
                                " time=" << frame.time.value() << "/" << frame.time.rate() <<
                                " mean=" << imageMean <<
                                " interval=" << wallDiff.count() <<
                                " path=" << path.get() << std::endl;
                        }
                        if (state.movingFrames >= expectedFrames)
                        {
                            state.exitRequested = true;
                            shouldExit = true;
                        }
                    }
                }

                if (shouldStart)
                {
                    std::cout << "DJV_SMOKE_FORWARD_START role=" <<
                        getSmokeRoleName(role) <<
                        " time=" << frame.time.value() << "/" << frame.time.rate() <<
                        " path=" << path.get() << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->transportForward();
                    }
                }
                if (shouldExit)
                {
                    std::cout << "DJV_SMOKE_FORWARD_READY frames=" << readyFrames <<
                        " expected=" << expectedFrames << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->exit();
                    }
                }
            }

            void smokeReversePlayback(
                Viewport::Role role,
                const ftk::Path& path,
                const std::vector<tl::VideoFrame>& frames,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_REVERSE_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_REVERSE_ROLES"))
                {
                    return;
                }

                auto& state = getSmokeReverseState();
                if (frames.empty())
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (state.requested && state.primed && !state.exitRequested)
                    {
                        ++state.emptyFrames;
                        std::cout << "DJV_SMOKE_REVERSE_EMPTY role=" <<
                            getSmokeRoleName(role) <<
                            " empty=" << state.emptyFrames << std::endl;
                    }
                    return;
                }

                const auto& frame = frames.front();
                const bool invalidFrame = frame.layers.empty() ||
                    !frame.layers.front().image;
                if (invalidFrame)
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (state.requested && !state.exitRequested)
                    {
                        ++state.invalidFrames;
                        std::cout << "DJV_SMOKE_REVERSE_INVALID role=" <<
                            getSmokeRoleName(role) <<
                            " invalid=" << state.invalidFrames <<
                            " time=" << frame.time.value() << "/" <<
                            frame.time.rate() << std::endl;
                    }
                    return;
                }

                int expectedFrames = 5;
                if (const char* env = std::getenv("DJV_SMOKE_REVERSE_FRAMES"))
                {
                    expectedFrames = std::max(1, std::atoi(env));
                }
                int startFrame = 60;
                if (const char* env = std::getenv("DJV_SMOKE_REVERSE_START_FRAME"))
                {
                    startFrame = std::max(1, std::atoi(env));
                }

                bool shouldStart = false;
                bool shouldExit = false;
                int readyFrames = 0;
                double frameInterval = 0.0;
                const auto wallNow = std::chrono::steady_clock::now();
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (!state.requested)
                    {
                        state.requested = true;
                        shouldStart = true;
                    }
                    else if (!state.exitRequested)
                    {
                        const double value = frame.time.value();
                        if (!state.primed)
                        {
                            const double startDiff = std::abs(value - startFrame);
                            if (startDiff <= 4.0)
                            {
                                state.primed = true;
                                state.lastFrame = value;
                                state.lastFrameWallTime = wallNow;
                                std::cout << "DJV_SMOKE_REVERSE_PRIMED role=" <<
                                    getSmokeRoleName(role) <<
                                    " time=" << frame.time.value() << "/" <<
                                    frame.time.rate() << std::endl;
                            }
                            else if (value > startFrame + 4)
                            {
                                ++state.invalidFrames;
                                std::cout << "DJV_SMOKE_REVERSE_INVALID role=" <<
                                    getSmokeRoleName(role) <<
                                    " invalid=" << state.invalidFrames <<
                                    " expectedStart=" << startFrame <<
                                    " time=" << frame.time.value() << "/" <<
                                    frame.time.rate() << std::endl;
                            }
                        }
                        else if (value < state.lastFrame)
                        {
                            const std::chrono::duration<double> wallDiff =
                                wallNow - state.lastFrameWallTime;
                            frameInterval = wallDiff.count();
                            ++state.movingFrames;
                            state.lastFrame = value;
                            state.lastFrameWallTime = wallNow;
                            readyFrames = state.movingFrames;
                            std::cout << "DJV_SMOKE_REVERSE_FRAME role=" <<
                                getSmokeRoleName(role) <<
                                " frames=" << state.movingFrames << "/" << expectedFrames <<
                                " time=" << frame.time.value() << "/" << frame.time.rate() <<
                                " interval=" << frameInterval <<
                                " path=" << path.get() << std::endl;
                        }
                        if (state.movingFrames >= expectedFrames)
                        {
                            state.exitRequested = true;
                            shouldExit = true;
                        }
                    }
                }

                if (shouldStart)
                {
                    std::cout << "DJV_SMOKE_REVERSE_START role=" <<
                        getSmokeRoleName(role) <<
                        " startFrame=" << startFrame <<
                        " path=" << path.get() << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->transportSeek(OTIO_NS::RationalTime(
                            startFrame,
                            frame.time.rate()));
                        appLock->transportReverse();
                    }
                }
                if (shouldExit)
                {
                    std::cout << "DJV_SMOKE_REVERSE_READY frames=" << readyFrames <<
                        " expected=" << expectedFrames << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->exit();
                    }
                }
            }

            void smokeReverseStartGuard(
                Viewport::Role role,
                const std::vector<tl::VideoFrame>& frames,
                const std::shared_ptr<tl::Player>& player,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_REVERSE_GUARD_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_REVERSE_GUARD_ROLES") ||
                    frames.empty() ||
                    frames.front().time.strictly_equal(tl::invalidTime))
                {
                    return;
                }

                static std::atomic<bool> tested(false);
                if (tested.exchange(true))
                {
                    return;
                }

                if (auto appLock = app.lock())
                {
                    appLock->transportReverse();
                }
                const bool blocked = !player ||
                    tl::Playback::Reverse != player->getPlayback();
                std::cout << "DJV_SMOKE_REVERSE_GUARD_READY blocked=" <<
                    (blocked ? "true" : "false") <<
                    " playback=" <<
                    (player ? static_cast<int>(player->getPlayback()) : -1) <<
                    " time=" << frames.front().time.value() << "/" <<
                    frames.front().time.rate() << std::endl;
                if (auto appLock = app.lock())
                {
                    appLock->exit();
                }
            }

            void smokeSeek(
                Viewport::Role role,
                const ftk::Path& path,
                const std::vector<tl::VideoFrame>& frames,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_SEEK_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_SEEK_ROLES") ||
                    frames.empty() ||
                    frames.front().time.strictly_equal(tl::invalidTime))
                {
                    return;
                }

                auto& state = getSmokeSeekState();
                bool shouldSeek = false;
                bool shouldExit = false;
                bool passed = false;
                int targetFrame = 45;
                if (const char* env = std::getenv("DJV_SMOKE_SEEK_FRAME"))
                {
                    targetFrame = std::max(1, std::atoi(env));
                }
                int tolerance = 2;
                if (const char* env = std::getenv("DJV_SMOKE_SEEK_TOLERANCE"))
                {
                    tolerance = std::max(0, std::atoi(env));
                }

                const auto& frame = frames.front();
                const bool invalidFrame = frame.layers.empty() ||
                    !frame.layers.front().image;
                const double imageMean = invalidFrame ? 0.0 : getSmokeImageByteMean(frame.layers.front().image);
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    if (!state.requested)
                    {
                        state.requested = true;
                        state.targetFrame = targetFrame;
                        shouldSeek = true;
                    }
                    else if (!state.exitRequested)
                    {
                        const double diff = std::abs(frame.time.value() - state.targetFrame);
                        if (!invalidFrame &&
                            imageMean > 1.0 &&
                            diff <= tolerance)
                        {
                            state.exitRequested = true;
                            shouldExit = true;
                            passed = true;
                        }
                    }
                }

                if (shouldSeek)
                {
                    std::cout << "DJV_SMOKE_SEEK_START role=" <<
                        getSmokeRoleName(role) <<
                        " target=" << targetFrame <<
                        " path=" << path.get() << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->transportSeek(OTIO_NS::RationalTime(
                            targetFrame,
                            frame.time.rate()));
                    }
                }
                if (shouldExit)
                {
                    std::cout << "DJV_SMOKE_SEEK_READY passed=" <<
                        (passed ? "true" : "false") <<
                        " target=" << targetFrame <<
                        " time=" << frame.time.value() << "/" << frame.time.rate() <<
                        " mean=" << imageMean << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->exit();
                    }
                }
            }

            void smokeSpeed(
                Viewport::Role role,
                const ftk::Path& path,
                const std::vector<tl::VideoFrame>& frames,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_SPEED_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_SPEED_ROLES") ||
                    frames.empty() ||
                    frames.front().time.strictly_equal(tl::invalidTime))
                {
                    return;
                }

                double multiplier = 1.0;
                if (const char* env = std::getenv("DJV_SMOKE_SPEED_MULT"))
                {
                    multiplier = std::max(1.0 / 3.0, std::min(3.0, std::atof(env)));
                }
                double duration = 3.0;
                if (const char* env = std::getenv("DJV_SMOKE_SPEED_DURATION"))
                {
                    duration = std::max(0.5, std::atof(env));
                }
                double tolerance = 0.35;
                if (const char* env = std::getenv("DJV_SMOKE_SPEED_TOLERANCE"))
                {
                    tolerance = std::max(0.01, std::atof(env));
                }

                const auto& frame = frames.front();
                const auto wallNow = std::chrono::steady_clock::now();
                bool shouldStart = false;
                bool shouldExit = false;
                double actualMultiplier = 0.0;
                double elapsed = 0.0;
                double frameDelta = 0.0;
                bool passed = false;
                {
                    std::lock_guard<std::mutex> lock(getSmokeSpeedState().mutex);
                    auto& state = getSmokeSpeedState();
                    if (!state.started)
                    {
                        state.started = true;
                        state.startFrame = frame.time.value();
                        state.lastFrame = frame.time.value();
                        state.startWallTime = wallNow;
                        shouldStart = true;
                    }
                    else if (!state.exitRequested)
                    {
                        state.lastFrame = std::max(state.lastFrame, frame.time.value());
                        const std::chrono::duration<double> wallDiff = wallNow - state.startWallTime;
                        elapsed = wallDiff.count();
                        if (elapsed >= duration)
                        {
                            frameDelta = state.lastFrame - state.startFrame;
                            actualMultiplier =
                                frame.time.rate() > 0.0 && elapsed > 0.0 ?
                                (frameDelta / elapsed) / frame.time.rate() :
                                0.0;
                            passed = std::abs(actualMultiplier - multiplier) <= tolerance;
                            state.exitRequested = true;
                            shouldExit = true;
                        }
                    }
                }

                if (shouldStart)
                {
                    std::cout << "DJV_SMOKE_SPEED_START role=" << getSmokeRoleName(role) <<
                        " multiplier=" << multiplier <<
                        " duration=" << duration <<
                        " path=" << path.get() << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->transportSetSpeed(frame.time.rate() * multiplier);
                        appLock->transportForward();
                    }
                }
                if (shouldExit)
                {
                    std::cout << "DJV_SMOKE_SPEED_READY role=" << getSmokeRoleName(role) <<
                        " passed=" << (passed ? "true" : "false") <<
                        " multiplier=" << multiplier <<
                        " actual=" << actualMultiplier <<
                        " elapsed=" << elapsed <<
                        " frameDelta=" << frameDelta << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->exit();
                    }
                }
            }

            void smokeDualTotalControl(
                Viewport::Role role,
                const ftk::Path& path,
                const std::vector<tl::VideoFrame>& frames,
                const std::weak_ptr<App>& app)
            {
                if (!std::getenv("DJV_SMOKE_DUAL_TOTAL_EXIT") ||
                    !isSmokeRoleEnabled(role, "DJV_SMOKE_DUAL_TOTAL_ROLES") ||
                    frames.empty() ||
                    frames.front().time.strictly_equal(tl::invalidTime))
                {
                    return;
                }

                const int splitIndex = getSmokeSplitIndex(role);
                if (splitIndex < 0)
                {
                    return;
                }

                std::string mode = "forward";
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_MODE"))
                {
                    mode = ftk::toLower(env);
                }
                const bool offsetMode =
                    "offset-forward" == mode ||
                    "offset-reverse" == mode;
                const bool reverseMode =
                    "reverse" == mode ||
                    "offset-reverse" == mode;
                int expectedFrames = 12;
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_FRAMES"))
                {
                    expectedFrames = std::max(1, std::atoi(env));
                }
                double speedMult = 1.0;
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_SPEED_MULT"))
                {
                    speedMult = std::max(1.0 / 3.0, std::min(3.0, std::atof(env)));
                }
                double targetFrame = 120.0;
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_START_FRAME"))
                {
                    targetFrame = std::max(1.0, std::atof(env));
                }
                std::array<double, 2> targetSeconds = { 0.0, 0.0 };
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_OFFSET_A_SECONDS"))
                {
                    targetSeconds[0] = std::max(0.0, std::atof(env));
                }
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_OFFSET_B_SECONDS"))
                {
                    targetSeconds[1] = std::max(0.0, std::atof(env));
                }
                double offsetToleranceSeconds = 0.25;
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_OFFSET_TOLERANCE"))
                {
                    offsetToleranceSeconds = std::max(0.0, std::atof(env));
                }
                bool requireSync = false;
                double maxAllowedSyncDiff = 0.0;
                if (const char* env = std::getenv("DJV_SMOKE_DUAL_TOTAL_MAX_DIFF"))
                {
                    requireSync = true;
                    maxAllowedSyncDiff = std::max(0.0, std::atof(env));
                }

                const auto& frame = frames.front();
                bool shouldForward = false;
                bool shouldSeek = false;
                bool shouldReverse = false;
                bool shouldExit = false;
                bool syncPassed = true;
                bool offsetPassed = true;
                double maxSyncDiff = 0.0;
                double maxOffsetErrorSeconds = 0.0;
                int syncSamples = 0;
                int syncBadSamples = 0;
                int offsetSamples = 0;
                int offsetBadSamples = 0;
                int readyPanes = 0;
                int movingFrames = 0;
                const double frameSeconds =
                    frame.time.rate() > 0.0 ?
                    frame.time.value() / frame.time.rate() :
                    frame.time.value();
                {
                    std::lock_guard<std::mutex> lock(getSmokeDualTotalState().mutex);
                    auto& state = getSmokeDualTotalState();
                    auto& pane = state.panes[splitIndex];
                    if (!state.started)
                    {
                        pane.seen = true;
                        pane.lastFrame = frame.time.value();
                        pane.currentFrame = frame.time.value();
                        pane.currentSeconds = frameSeconds;
                        pane.haveFrame = true;
                        state.targetFrame = targetFrame;
                        state.targetSeconds = targetSeconds;
                        const bool bothSeen = state.panes[0].seen && state.panes[1].seen;
                        if (bothSeen)
                        {
                            state.started = true;
                            if (offsetMode)
                            {
                                state.seeking = true;
                                shouldSeek = true;
                                for (size_t i = 0; i < state.panes.size(); ++i)
                                {
                                    if (std::abs(state.panes[i].currentSeconds - state.targetSeconds[i]) <=
                                        offsetToleranceSeconds)
                                    {
                                        state.panes[i].primed = true;
                                    }
                                }
                                if (state.panes[0].primed && state.panes[1].primed)
                                {
                                    state.seeking = false;
                                    state.playing = true;
                                    if (reverseMode)
                                    {
                                        shouldReverse = true;
                                    }
                                    else
                                    {
                                        shouldForward = true;
                                    }
                                }
                            }
                            else if (reverseMode)
                            {
                                state.seeking = true;
                                shouldSeek = true;
                            }
                            else
                            {
                                state.playing = true;
                                shouldForward = true;
                            }
                        }
                    }
                    else if (state.seeking)
                    {
                        const bool primed =
                            offsetMode ?
                            std::abs(frameSeconds - state.targetSeconds[splitIndex]) <= offsetToleranceSeconds :
                            std::abs(frame.time.value() - state.targetFrame) <= 4.0;
                        if (primed)
                        {
                            pane.primed = true;
                            pane.lastFrame = frame.time.value();
                            pane.currentFrame = frame.time.value();
                            pane.currentSeconds = frameSeconds;
                            pane.haveFrame = true;
                        }
                        if (state.panes[0].primed && state.panes[1].primed)
                        {
                            state.seeking = false;
                            state.playing = true;
                            if (reverseMode)
                            {
                                shouldReverse = true;
                            }
                            else
                            {
                                shouldForward = true;
                            }
                        }
                    }
                    else if (state.playing && !state.exitRequested)
                    {
                        const bool moved =
                            reverseMode ?
                            frame.time.value() < pane.lastFrame :
                            frame.time.value() > pane.lastFrame;
                        pane.currentFrame = frame.time.value();
                        pane.currentSeconds = frameSeconds;
                        pane.haveFrame = true;
                        if (requireSync &&
                            state.panes[0].haveFrame &&
                            state.panes[1].haveFrame)
                        {
                            const double diff = std::abs(
                                state.panes[0].currentFrame -
                                state.panes[1].currentFrame);
                            state.maxSyncDiff = std::max(state.maxSyncDiff, diff);
                            ++state.syncSamples;
                            if (diff > maxAllowedSyncDiff)
                            {
                                ++state.syncBadSamples;
                                std::cout << "DJV_SMOKE_DUAL_TOTAL_SYNC_DIFF diff=" <<
                                    diff << " max=" << maxAllowedSyncDiff <<
                                    " a=" << state.panes[0].currentFrame <<
                                    " b=" << state.panes[1].currentFrame <<
                                    std::endl;
                            }
                        }
                        if (offsetMode &&
                            state.panes[0].haveFrame &&
                            state.panes[1].haveFrame)
                        {
                            const double expectedOffset =
                                state.targetSeconds[0] - state.targetSeconds[1];
                            const double actualOffset =
                                state.panes[0].currentSeconds -
                                state.panes[1].currentSeconds;
                            const double error = std::abs(actualOffset - expectedOffset);
                            state.maxOffsetErrorSeconds =
                                std::max(state.maxOffsetErrorSeconds, error);
                            ++state.offsetSamples;
                            if (error > offsetToleranceSeconds)
                            {
                                ++state.offsetBadSamples;
                                std::cout << "DJV_SMOKE_DUAL_TOTAL_OFFSET_DIFF error=" <<
                                    error << " max=" << offsetToleranceSeconds <<
                                    " expected=" << expectedOffset <<
                                    " actual=" << actualOffset <<
                                    " a=" << state.panes[0].currentSeconds <<
                                    " b=" << state.panes[1].currentSeconds <<
                                    std::endl;
                            }
                        }
                        if (moved)
                        {
                            ++pane.movingFrames;
                            pane.lastFrame = frame.time.value();
                            std::cout << "DJV_SMOKE_DUAL_TOTAL_FRAME role=" <<
                                getSmokeRoleName(role) <<
                                " mode=" << mode <<
                                " frames=" << pane.movingFrames << "/" << expectedFrames <<
                                " time=" << frame.time.value() << "/" << frame.time.rate() <<
                                " path=" << path.get() << std::endl;
                        }
                        if (state.panes[0].movingFrames >= expectedFrames &&
                            state.panes[1].movingFrames >= expectedFrames)
                        {
                            state.exitRequested = true;
                            shouldExit = true;
                            readyPanes = 2;
                            movingFrames = std::min(
                                state.panes[0].movingFrames,
                                state.panes[1].movingFrames);
                            maxSyncDiff = state.maxSyncDiff;
                            maxOffsetErrorSeconds = state.maxOffsetErrorSeconds;
                            syncSamples = state.syncSamples;
                            syncBadSamples = state.syncBadSamples;
                            offsetSamples = state.offsetSamples;
                            offsetBadSamples = state.offsetBadSamples;
                            syncPassed = !requireSync || 0 == state.syncBadSamples;
                            offsetPassed = !offsetMode || 0 == state.offsetBadSamples;
                        }
                    }
                }

                if (shouldSeek)
                {
                    std::cout << "DJV_SMOKE_DUAL_TOTAL_SEEK mode=" << mode <<
                        " target=" << targetFrame <<
                        " offsetA=" << targetSeconds[0] <<
                        " offsetB=" << targetSeconds[1] << std::endl;
                    if (auto appLock = app.lock())
                    {
                        if (offsetMode)
                        {
                            appLock->splitSeek(
                                App::SplitPane::A,
                                OTIO_NS::RationalTime(targetSeconds[0], 1.0));
                            appLock->splitSeek(
                                App::SplitPane::B,
                                OTIO_NS::RationalTime(targetSeconds[1], 1.0));
                        }
                        else
                        {
                            appLock->transportSeek(OTIO_NS::RationalTime(targetFrame, frame.time.rate()));
                        }
                    }
                }
                if (shouldForward)
                {
                    std::cout << "DJV_SMOKE_DUAL_TOTAL_START mode=" << mode <<
                        " speedMult=" << speedMult << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->transportSetSpeed(frame.time.rate() * speedMult);
                        appLock->transportForward();
                    }
                }
                if (shouldReverse)
                {
                    std::cout << "DJV_SMOKE_DUAL_TOTAL_START mode=" << mode <<
                        " speedMult=" << speedMult << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->transportSetSpeed(frame.time.rate() * speedMult);
                        appLock->transportReverse();
                    }
                }
                if (shouldExit)
                {
                    std::cout << "DJV_SMOKE_DUAL_TOTAL_READY mode=" << mode <<
                        " readyPanes=" << readyPanes <<
                        " frames=" << movingFrames <<
                        " expected=" << expectedFrames <<
                        " syncPassed=" << (syncPassed ? "true" : "false") <<
                        " offsetPassed=" << (offsetPassed ? "true" : "false") <<
                        " maxSyncDiff=" << maxSyncDiff <<
                        " maxOffsetErrorSeconds=" << maxOffsetErrorSeconds <<
                        " syncSamples=" << syncSamples <<
                        " syncBadSamples=" << syncBadSamples <<
                        " offsetSamples=" << offsetSamples <<
                        " offsetBadSamples=" << offsetBadSamples << std::endl;
                    if (auto appLock = app.lock())
                    {
                        appLock->exit();
                    }
                }
            }
        }

        struct Viewport::Private
        {
            Role role = Role::Primary;
            struct EmptyPane
            {
                std::shared_ptr<ftk::VerticalLayout> layout;
                std::shared_ptr<ftk::Label> titleLabel;
                std::shared_ptr<ftk::Label> dropLabel;
                std::shared_ptr<ftk::PushButton> openButton;
                ftk::Box2I geometry;
            };

            std::weak_ptr<App> app;
            bool hud = false;
            ftk::Path path;
            OTIO_NS::RationalTime currentTime = tl::invalidTime;
            double fps = 0.0;
            size_t droppedFrames = 0;
            size_t videoFramesSize = 0;
            ftk::ImageOptions imageOptions;
            tl::DisplayOptions displayOptions;
            tl::PlayerCacheInfo cacheInfo;
            models::MouseActionBinding pickBinding =
                models::MouseActionBinding(ftk::MouseButton::Left);
            models::MouseActionBinding frameShuttleBinding =
                models::MouseActionBinding(ftk::MouseButton::Left, ftk::KeyModifier::Shift);
            float frameShuttleScale = 1.F;
            std::shared_ptr<ftk::Observable<ftk::V2I> > pick;
            std::shared_ptr<ftk::Observable<ftk::V2I> > samplePos;
            std::shared_ptr<ftk::Observable<ftk::Color4F> > colorSample;

            std::shared_ptr<ftk::Label> fileNameLabel;
            std::shared_ptr<ftk::Label> timeLabel;
            std::shared_ptr<ftk::ColorSwatch> colorPickerSwatch;
            std::shared_ptr<ftk::Label> colorPickerLabel;
            std::shared_ptr<ftk::Label> cacheLabel;
            std::shared_ptr<ftk::GridLayout> hudLayout;
            std::array<EmptyPane, 2> emptyPanes;
            std::shared_ptr<ftk::Label> emptyOverlayLabel;
            std::shared_ptr<ftk::PushButton> closeButton;

            std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > currentTimeObserver;
            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > videoObserver;
            std::shared_ptr<ftk::Observer<tl::PlayerCacheInfo> > cacheObserver;
            std::shared_ptr<ftk::Observer<double> > fpsObserver;
            std::shared_ptr<ftk::Observer<size_t> > droppedFramesObserver;
            std::shared_ptr<ftk::Observer<tl::CompareOptions> > compareOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::OCIOOptions> > ocioOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::LUTOptions> > lutOptionsObserver;
            std::shared_ptr<ftk::Observer<ftk::ImageOptions> > imageOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::DisplayOptions> > displayOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::BackgroundOptions> > bgOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::ForegroundOptions> > fgOptionsObserver;
            std::shared_ptr<ftk::Observer<ftk::gl::TextureType> > colorBufferObserver;
            std::shared_ptr<ftk::Observer<bool> > hudObserver;
            std::shared_ptr<ftk::Observer<tl::TimeUnits> > timeUnitsObserver;
            std::shared_ptr<ftk::Observer<models::MouseSettings> > mouseSettingsObserver;

            enum class MouseMode
            {
                None,
                Shuttle,
                Picker
            };
            struct MouseData
            {
                MouseMode mode = MouseMode::None;
                OTIO_NS::RationalTime shuttleStart = tl::invalidTime;
            };
            MouseData mouse;
        };

        void Viewport::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            Role role,
            const std::shared_ptr<IWidget>& parent)
        {
            tl::ui::Viewport::_init(context, parent);
            FTK_P();

            p.app = app;
            p.role = role;

            p.pick = ftk::Observable<ftk::V2I>::create();
            p.samplePos = ftk::Observable<ftk::V2I>::create();
            p.colorSample = ftk::Observable<ftk::Color4F>::create();

            p.fileNameLabel = ftk::Label::create(context);
            p.fileNameLabel->setFont(ftk::FontType::Mono);
            p.fileNameLabel->setMarginRole(ftk::SizeRole::MarginInside);
            p.fileNameLabel->setBackgroundRole(ftk::ColorRole::Overlay);

            p.timeLabel = ftk::Label::create(context);
            p.timeLabel->setFont(ftk::FontType::Mono);
            p.timeLabel->setMarginRole(ftk::SizeRole::MarginInside);
            p.timeLabel->setBackgroundRole(ftk::ColorRole::Overlay);
            p.timeLabel->setHAlign(ftk::HAlign::Right);

            p.colorPickerSwatch = ftk::ColorSwatch::create(context);
            p.colorPickerSwatch->setSizeRole(ftk::SizeRole::MarginLarge);
            p.colorPickerLabel = ftk::Label::create(context);
            p.colorPickerLabel->setFont(ftk::FontType::Mono);

            p.cacheLabel = ftk::Label::create(context);
            p.cacheLabel->setFont(ftk::FontType::Mono);
            p.cacheLabel->setMarginRole(ftk::SizeRole::MarginInside);
            p.cacheLabel->setBackgroundRole(ftk::ColorRole::Overlay);
            p.cacheLabel->setHAlign(ftk::HAlign::Right);

            p.hudLayout = ftk::GridLayout::create(context, shared_from_this());
            p.hudLayout->setMarginRole(ftk::SizeRole::MarginSmall);
            p.hudLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.fileNameLabel->setParent(p.hudLayout);
            p.hudLayout->setGridPos(p.fileNameLabel, 0, 0);
            p.timeLabel->setParent(p.hudLayout);
            p.hudLayout->setGridPos(p.timeLabel, 0, 2);

            auto spacer = ftk::Spacer::create(context, ftk::Orientation::Vertical, p.hudLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            p.hudLayout->setGridPos(spacer, 1, 1);

            auto hLayout = ftk::HorizontalLayout::create(context, p.hudLayout);
            p.hudLayout->setGridPos(hLayout, 2, 0);
            hLayout->setMarginRole(ftk::SizeRole::MarginInside);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            hLayout->setBackgroundRole(ftk::ColorRole::Overlay);
            p.colorPickerSwatch->setParent(hLayout);
            p.colorPickerLabel->setParent(hLayout);
            p.cacheLabel->setParent(p.hudLayout);
            p.hudLayout->setGridPos(p.cacheLabel, 2, 2);

            for (size_t i = 0; i < p.emptyPanes.size(); ++i)
            {
                auto& pane = p.emptyPanes[i];
                pane.layout = ftk::VerticalLayout::create(context, shared_from_this());
                pane.layout->setSpacingRole(ftk::SizeRole::Spacing);
                pane.layout->setMarginRole(ftk::SizeRole::Margin);
                pane.titleLabel = ftk::Label::create(context, "未加载视频", pane.layout);
                pane.titleLabel->setFont(ftk::FontType::Bold);
                pane.titleLabel->setFontSize(16);
                pane.titleLabel->setHAlign(ftk::HAlign::Center);
                pane.dropLabel = ftk::Label::create(context, "请选择文件或拖拽文件到该区域", pane.layout);
                pane.dropLabel->setHAlign(ftk::HAlign::Center);
                pane.dropLabel->setTextRole(ftk::ColorRole::TextDisabled);
                pane.dropLabel->setFontSize(13);
                pane.openButton = ftk::PushButton::create(context, "打开文件", pane.layout);
                pane.openButton->setIcon(std::string());
                pane.layout->setVisible(false);
            }
            p.emptyOverlayLabel = ftk::Label::create(context, "请选择文件或拖拽文件到该区域", shared_from_this());
            p.emptyOverlayLabel->setHAlign(ftk::HAlign::Center);
            p.emptyOverlayLabel->setBackgroundRole(ftk::ColorRole::Overlay);
            p.emptyOverlayLabel->setMarginRole(ftk::SizeRole::MarginInside);
            p.emptyOverlayLabel->setFontSize(14);
            p.closeButton = ftk::PushButton::create(context, "关闭视频", shared_from_this());
            p.emptyOverlayLabel->setVisible(false);
            p.closeButton->setVisible(false);

            std::weak_ptr<Viewport> weak(std::dynamic_pointer_cast<Viewport>(shared_from_this()));
            auto openDialogForPane = [weak](size_t paneIndex)
            {
                if (auto widget = weak.lock())
                {
                    if (auto app = widget->_p->app.lock())
                    {
                        switch (widget->_p->role)
                        {
                        case Role::SplitA:
                            paneIndex = 0;
                            break;
                        case Role::SplitB:
                            paneIndex = 1;
                            break;
                        case Role::Primary:
                        default:
                            break;
                        }
                        if (paneIndex == 1)
                        {
                            app->openToBDialog();
                        }
                        else
                        {
                            app->openToADialog();
                        }
                    }
                }
            };
            p.emptyPanes[0].openButton->setClickedCallback(
                [openDialogForPane]
                {
                    openDialogForPane(0);
                });
            p.emptyPanes[1].openButton->setClickedCallback(
                [openDialogForPane]
                {
                    openDialogForPane(1);
                });
            p.closeButton->setClickedCallback(
                [weak]
                {
                    if (auto widget = weak.lock())
                    {
                        if (auto app = widget->_p->app.lock())
                        {
                            switch (widget->_p->role)
                            {
                            case Role::SplitB:
                                app->getFilesModel()->clearB();
                                break;
                            case Role::SplitA:
                            case Role::Primary:
                            default:
                                app->getFilesModel()->close();
                                break;
                            }
                        }
                    }
                });

            p.fpsObserver = ftk::Observer<double>::create(
                observeFPS(),
                [this](double value)
                {
                    _p->fps = value;
                    _hudUpdate();
                });

            p.droppedFramesObserver = ftk::Observer<size_t>::create(
                observeDroppedFrames(),
                [this](size_t value)
                {
                    _p->droppedFrames = value;
                    _hudUpdate();
                });

            if (Role::Primary == p.role)
            {
                p.compareOptionsObserver = ftk::Observer<tl::CompareOptions>::create(
                    app->getFilesModel()->observeCompareOptions(),
                    [this](const tl::CompareOptions& value)
                    {
                        setCompareOptions(value);
                        setFrameView(true);
                        _emptyStateUpdate();
                    });
            }
            else
            {
                tl::CompareOptions compareOptions;
                compareOptions.compare = tl::Compare::A;
                setCompareOptions(compareOptions);
            }

            p.ocioOptionsObserver = ftk::Observer<tl::OCIOOptions>::create(
                app->getColorModel()->observeOCIOOptions(),
                [this](const tl::OCIOOptions& value)
                {
                   setOCIOOptions(value);
                });

            p.lutOptionsObserver = ftk::Observer<tl::LUTOptions>::create(
                app->getColorModel()->observeLUTOptions(),
                [this](const tl::LUTOptions& value)
                {
                   setLUTOptions(value);
                });

            p.imageOptionsObserver = ftk::Observer<ftk::ImageOptions>::create(
                app->getViewportModel()->observeImageOptions(),
                [this](const ftk::ImageOptions& value)
                {
                    _p->imageOptions = value;
                    _videoUpdate();
                });

            p.displayOptionsObserver = ftk::Observer<tl::DisplayOptions>::create(
                app->getViewportModel()->observeDisplayOptions(),
                [this](const tl::DisplayOptions& value)
                {
                    _p->displayOptions = value;
                    _videoUpdate();
                });

            p.bgOptionsObserver = ftk::Observer<tl::BackgroundOptions>::create(
                app->getViewportModel()->observeBackgroundOptions(),
                [this](const tl::BackgroundOptions& value)
                {
                    setBackgroundOptions(value);
                });

            p.fgOptionsObserver = ftk::Observer<tl::ForegroundOptions>::create(
                app->getViewportModel()->observeForegroundOptions(),
                [this](const tl::ForegroundOptions& value)
                {
                    setForegroundOptions(value);
                });

            p.colorBufferObserver = ftk::Observer<ftk::gl::TextureType>::create(
                app->getViewportModel()->observeColorBuffer(),
                [this](ftk::gl::TextureType value)
                {
                    setColorBuffer(value);
                    _hudUpdate();
                });

            p.hudObserver = ftk::Observer<bool>::create(
                app->getViewportModel()->observeHUD(),
                [this](bool value)
                {
                    _p->hud = value;
                    _hudUpdate();
                });

            p.timeUnitsObserver = ftk::Observer<tl::TimeUnits>::create(
                app->getTimeUnitsModel()->observeTimeUnits(),
                [this](tl::TimeUnits value)
                {
                    _hudUpdate();
                });

            p.mouseSettingsObserver = ftk::Observer<models::MouseSettings>::create(
                app->getSettingsModel()->observeMouse(),
                [this](const models::MouseSettings& value)
                {
                    FTK_P();
                    auto i = value.bindings.find(models::MouseAction::PanView);
                    setPanBinding(
                        i != value.bindings.end() ? i->second.button : ftk::MouseButton::None,
                        i != value.bindings.end() ? i->second.modifier : ftk::KeyModifier::None);
                    i = value.bindings.find(models::MouseAction::CompareWipe);
                    setWipeBinding(
                        i != value.bindings.end() ? i->second.button : ftk::MouseButton::None,
                        i != value.bindings.end() ? i->second.modifier : ftk::KeyModifier::None);
                    i = value.bindings.find(models::MouseAction::Pick);
                    p.pickBinding = i != value.bindings.end() ? i->second : models::MouseActionBinding();
                    i = value.bindings.find(models::MouseAction::FrameShuttle);
                    p.frameShuttleBinding = i != value.bindings.end() ? i->second : models::MouseActionBinding();
                    p.frameShuttleScale = value.frameShuttleScale;
                });

            setSizeUpdate();
            _emptyStateUpdate();
        }

        Viewport::Viewport() :
            _p(new Private)
        {}

        Viewport::~Viewport()
        {}

        std::shared_ptr<Viewport> Viewport::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            Role role,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<Viewport>(new Viewport);
            out->_init(context, app, role, parent);
            return out;
        }

        std::shared_ptr<ftk::IObservable<ftk::V2I> > Viewport::observePick() const
        {
            return _p->pick;
        }

        std::shared_ptr<ftk::IObservable<ftk::V2I> > Viewport::observeSamplePos() const
        {
            return _p->samplePos;
        }

        std::shared_ptr<ftk::IObservable<ftk::Color4F> > Viewport::observeColorSample() const
        {
            return _p->colorSample;
        }

        void Viewport::setPlayer(const std::shared_ptr<tl::Player>& player)
        {
            FTK_P();
            std::shared_ptr<tl::Player> effectivePlayer = player;
            if (auto app = p.app.lock())
            {
                const auto filesModel = app->getFilesModel();
                if ((Role::SplitA == p.role && !filesModel->getA()) ||
                    (Role::SplitB == p.role && filesModel->getB().empty()))
                {
                    effectivePlayer.reset();
                }
            }

            tl::ui::Viewport::setPlayer(effectivePlayer);
            p.videoFramesSize = 0;
            if (effectivePlayer)
            {
                if (Role::Primary != p.role)
                {
                    tl::CompareOptions compareOptions;
                    bool useCompareB = false;
                    if (auto app = p.app.lock())
                    {
                        useCompareB =
                            Role::SplitB == p.role &&
                            isSplitCompareMode(app->getFilesModel()->getCompareOptions().compare) &&
                            models::DualPlaybackMode::Sync == app->getFilesModel()->getDualPlaybackMode() &&
                            !effectivePlayer->getCompare().empty();
                    }
                    compareOptions.compare = useCompareB ? tl::Compare::B : tl::Compare::A;
                    setCompareOptions(compareOptions);
                }
                p.path = effectivePlayer->getPath();

                p.currentTimeObserver = ftk::Observer<OTIO_NS::RationalTime>::create(
                    effectivePlayer->observeCurrentTime(),
                    [this](const OTIO_NS::RationalTime& value)
                    {
                        if (getPlayer() &&
                            getPlayer()->isStopped() &&
                            getPlayer()->getCurrentVideo().empty())
                        {
                            _p->currentTime = value;
                            _hudUpdate();
                        }
                    });

                p.videoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                    effectivePlayer->observeCurrentVideo(),
                    [this](const std::vector<tl::VideoFrame>& value)
                    {
                        if (!value.empty() &&
                            !value.front().time.strictly_equal(tl::invalidTime))
                        {
                            _p->currentTime = value.front().time;
                            smokeFirstFrame(_p->role, _p->path, value.front().time, _p->app);
                        }
                        else if (getPlayer() && getPlayer()->isStopped())
                        {
                            _p->currentTime = getPlayer()->getCurrentTime();
                        }
                        smokeForwardPlayback(_p->role, _p->path, value, _p->app);
                        smokeReversePlayback(_p->role, _p->path, value, _p->app);
                        smokeReverseStartGuard(_p->role, value, getPlayer(), _p->app);
                        smokeSeek(_p->role, _p->path, value, _p->app);
                        smokeSpeed(_p->role, _p->path, value, _p->app);
                        smokeDualTotalControl(_p->role, _p->path, value, _p->app);
                        const size_t prevSize = _p->videoFramesSize;
                        _p->videoFramesSize = value.size();
                        if (prevSize != value.size())
                        {
                            setFrameView(true);
                        }
                        _hudUpdate();
                        _videoUpdate();
                        _emptyStateUpdate();
                    });

                p.cacheObserver = ftk::Observer<tl::PlayerCacheInfo>::create(
                    effectivePlayer->observeCacheInfo(),
                    [this](const tl::PlayerCacheInfo& value)
                    {
                        _p->cacheInfo = value;
                        _hudUpdate();
                    });
            }
            else
            {
                p.path = ftk::Path();
                p.currentTime = tl::invalidTime;
                p.currentTimeObserver.reset();
                p.videoObserver.reset();
                p.videoFramesSize = 0;
                p.cacheInfo = tl::PlayerCacheInfo();
                p.cacheObserver.reset();
                _hudUpdate();
            }
            p.emptyOverlayLabel->setVisible(false);
            p.closeButton->setVisible(effectivePlayer.get());
            _emptyStateUpdate();
        }

        ftk::Size2I Viewport::getSizeHint() const
        {
            return tl::ui::Viewport::getSizeHint();
        }

        void Viewport::setGeometry(const ftk::Box2I& value)
        {
            FTK_P();
            tl::ui::Viewport::setGeometry(value);
            p.hudLayout->setGeometry(value);
            _layoutEmptyState(value);

            const auto closeSize = p.closeButton->getSizeHint();
            p.closeButton->setGeometry(ftk::Box2I(
                value.max.x - closeSize.w - 8,
                value.min.y + 8,
                closeSize.w,
                closeSize.h));
        }

        void Viewport::drawEvent(const ftk::Box2I& drawRect, const ftk::DrawEvent& event)
        {
            FTK_P();
            const ftk::Box2I& g = getGeometry();
            const bool hasPlayer = static_cast<bool>(getPlayer());
            if (hasPlayer)
            {
                tl::ui::Viewport::drawEvent(drawRect, event);
            }
            else
            {
                event.render->drawRect(g, ftk::Color4F(0.F, 0.F, 0.F));
            }

            const int spacing = event.style->getSizeRole(ftk::SizeRole::Spacing, event.displayScale);

            ftk::Size2I iconSize;
            std::shared_ptr<ftk::Image> icon;
            if (event.iconSystem)
            {
                icon = event.iconSystem->get("Directory", event.displayScale * 4.F);
                if (icon)
                {
                    iconSize = icon->getSize();
                }
            }

            for (size_t i = 0; i < p.emptyPanes.size(); ++i)
            {
                const auto& pane = p.emptyPanes[i];
                if (!pane.layout->isVisible())
                {
                    continue;
                }
                const ftk::Box2I emptyGeometry = pane.layout->getGeometry();
                const int iconY = std::max(
                    pane.geometry.min.y + spacing * 2,
                    emptyGeometry.min.y - iconSize.h - spacing);
                if (icon)
                {
                    const ftk::Box2I iconRect(
                        pane.geometry.min.x + (pane.geometry.w() - iconSize.w) / 2,
                        iconY,
                        iconSize.w,
                        iconSize.h);
                    event.render->drawImage(
                        icon,
                        iconRect,
                        event.style->getColorRole(ftk::ColorRole::Text));
                }
            }

            const ftk::Color4F borderColor = event.style->getColorRole(ftk::ColorRole::Border);
            event.render->drawRect(ftk::Box2I(g.min.x, g.min.y, g.w(), 1), borderColor);
            event.render->drawRect(ftk::Box2I(g.min.x, g.max.y - 1, g.w(), 1), borderColor);
            event.render->drawRect(ftk::Box2I(g.min.x, g.min.y, 1, g.h()), borderColor);
            event.render->drawRect(ftk::Box2I(g.max.x - 1, g.min.y, 1, g.h()), borderColor);

            if (_isDualEmptyState())
            {
                _drawDualDivider(g, event);
            }

            if (!hasPlayer)
            {
                return;
            }
        }

        void Viewport::mouseMoveEvent(ftk::MouseMoveEvent& event)
        {
            tl::ui::Viewport::mouseMoveEvent(event);
            FTK_P();
            switch (p.mouse.mode)
            {
            case Private::MouseMode::Shuttle:
                if (auto player = getPlayer())
                {
                    const OTIO_NS::RationalTime offset = OTIO_NS::RationalTime(
                        (event.pos.x - _getMousePressPos().x) * .05F * p.frameShuttleScale,
                        p.mouse.shuttleStart.rate()).round();
                    const OTIO_NS::TimeRange& timeRange = player->getTimeRange();
                    OTIO_NS::RationalTime t = p.mouse.shuttleStart + offset;
                    if (t < timeRange.start_time())
                    {
                        t = timeRange.end_time_exclusive() - (timeRange.start_time() - t);
                    }
                    else if (t > timeRange.end_time_exclusive())
                    {
                        t = timeRange.start_time() + (t - timeRange.end_time_exclusive());
                    }
                    player->seek(t);
                }
                break;
            case Private::MouseMode::Picker:
                if (auto app = p.app.lock())
                {
                    const ftk::Box2I& g = getGeometry();
                    const ftk::V2I pos = event.pos - g.min;
                    if (p.samplePos->setIfChanged(pos))
                    {
                        p.colorSample->setIfChanged(getColorSample(pos));
                        p.pick->setIfChanged((pos - getViewPos()) / getZoom());
                        _hudUpdate();
                    }
                }
                break;
            default: break;
            }
        }

        void Viewport::mousePressEvent(ftk::MouseClickEvent& event)
        {
            tl::ui::Viewport::mousePressEvent(event);
            FTK_P();
            if (auto app = p.app.lock())
            {
                switch (p.role)
                {
                case Role::SplitA:
                    app->setSplitActivePane(App::SplitPane::A);
                    break;
                case Role::SplitB:
                    app->setSplitActivePane(App::SplitPane::B);
                    break;
                case Role::Primary:
                default:
                    break;
                }
            }
            if (p.pickBinding.button == event.button &&
                ftk::checkKeyModifier(p.pickBinding.modifier, event.modifiers))
            {
                p.mouse.mode = Private::MouseMode::Picker;
                const ftk::Box2I& g = getGeometry();
                const ftk::V2I pos = event.pos - g.min;
                if (p.samplePos->setIfChanged(pos))
                {
                    p.colorSample->setIfChanged(getColorSample(pos));
                    p.pick->setIfChanged((pos - getViewPos()) / getZoom());
                    _hudUpdate();
                }
            }
            else if (p.frameShuttleBinding.button == event.button &&
                ftk::checkKeyModifier(p.frameShuttleBinding.modifier, event.modifiers))
            {
                p.mouse.mode = Private::MouseMode::Shuttle;
                if (auto player = getPlayer())
                {
                    player->stop();
                    p.mouse.shuttleStart = player->getCurrentTime();
                }
            }
        }

        void Viewport::mouseReleaseEvent(ftk::MouseClickEvent& event)
        {
            tl::ui::Viewport::mouseReleaseEvent(event);
            FTK_P();
            p.mouse = Private::MouseData();
        }

        void Viewport::dropEvent(ftk::DragDropEvent& event)
        {
            event.accept = true;
            if (auto textData = std::dynamic_pointer_cast<ftk::DragDropTextData>(event.data))
            {
                if (auto app = _p->app.lock())
                {
                    size_t paneIndex = _getPaneIndex(event.pos);
                    switch (_p->role)
                    {
                    case Role::SplitA: paneIndex = 0; break;
                    case Role::SplitB: paneIndex = 1; break;
                    case Role::Primary:
                    default: break;
                    }
                    for (const auto& i : textData->getText())
                    {
                        if (paneIndex == 1)
                        {
                            app->openToB(ftk::Path(i));
                        }
                        else
                        {
                            app->openToA(ftk::Path(i));
                        }
                    }
                }
            }
        }

        void Viewport::_videoUpdate()
        {
            FTK_P();
            std::vector<ftk::ImageOptions> imageOptions;
            std::vector<tl::DisplayOptions> displayOptions;
            for (size_t i = 0; i < p.videoFramesSize; ++i)
            {
                imageOptions.push_back(p.imageOptions);
                displayOptions.push_back(p.displayOptions);
            }
            setImageOptions(imageOptions);
            setDisplayOptions(displayOptions);
        }

        bool Viewport::_isDualEmptyState() const
        {
            if (Role::Primary != _p->role)
            {
                return false;
            }
            switch (getCompareOptions().compare)
            {
                case tl::Compare::B:
                case tl::Compare::Wipe:
                case tl::Compare::Overlay:
            case tl::Compare::Difference:
            case tl::Compare::Horizontal:
            case tl::Compare::Vertical:
            case tl::Compare::Tile:
                return true;
            default: return false;
            }
        }

        std::array<Viewport::PaneState, 2> Viewport::_getPaneStates() const
        {
            std::array<PaneState, 2> out = { PaneState::Hidden, PaneState::Hidden };
            auto app = _p->app.lock();
            if (!app)
            {
                return out;
            }

            auto filesModel = app->getFilesModel();
            const bool hasA = static_cast<bool>(filesModel->getA());
            const bool hasB = !filesModel->getB().empty();
            const bool aReady = _p->videoFramesSize >= 1;
            const bool bReady = _p->videoFramesSize >= 2;

            if (Role::SplitA == _p->role)
            {
                out[0] = !hasA ? PaneState::Empty : (aReady ? PaneState::Hidden : PaneState::Loading);
                return out;
            }
            if (Role::SplitB == _p->role)
            {
                const bool splitBUsesCompareB = getCompareOptions().compare == tl::Compare::B;
                out[0] = !hasB ? PaneState::Empty : ((splitBUsesCompareB ? bReady : aReady) ? PaneState::Hidden : PaneState::Loading);
                return out;
            }

            if (_isDualEmptyState())
            {
                out[0] = !hasA ? PaneState::Empty : (aReady ? PaneState::Hidden : PaneState::Loading);
                out[1] = !hasB ? PaneState::Empty : (bReady ? PaneState::Hidden : PaneState::Loading);
            }
            else
            {
                out[0] = !hasA ? PaneState::Empty : (aReady ? PaneState::Hidden : PaneState::Loading);
            }
            return out;
        }

        std::vector<ftk::Box2I> Viewport::_getEmptyStateBoxes(const ftk::Box2I& value) const
        {
            std::vector<ftk::Box2I> out;
            if (!_isDualEmptyState())
            {
                out.push_back(value);
                return out;
            }

            switch (getCompareOptions().compare)
            {
            case tl::Compare::Vertical:
            case tl::Compare::Tile:
            {
                const int h0 = value.h() / 2;
                const int h1 = value.h() - h0;
                out.push_back(ftk::Box2I(value.x(), value.y(), value.w(), h0));
                out.push_back(ftk::Box2I(value.x(), value.y() + h0, value.w(), h1));
                break;
            }
            default:
            {
                const int w0 = value.w() / 2;
                const int w1 = value.w() - w0;
                out.push_back(ftk::Box2I(value.x(), value.y(), w0, value.h()));
                out.push_back(ftk::Box2I(value.x() + w0, value.y(), w1, value.h()));
                break;
            }
            }
            return out;
        }

        size_t Viewport::_getPaneIndex(const ftk::V2I& pos) const
        {
            if (!_isDualEmptyState())
            {
                return 0;
            }
            const auto boxes = _getEmptyStateBoxes(getGeometry());
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                const auto& box = boxes[i];
                if (pos.x >= box.min.x &&
                    pos.x < box.max.x &&
                    pos.y >= box.min.y &&
                    pos.y < box.max.y)
                {
                    return i;
                }
            }
            return 0;
        }

        void Viewport::_drawDualDivider(const ftk::Box2I& value, const ftk::DrawEvent& event) const
        {
            const auto boxes = _getEmptyStateBoxes(value);
            if (boxes.size() < 2)
            {
                return;
            }

            const auto& a = boxes[0];
            const auto& b = boxes[1];
            const ftk::Color4F dividerFill = event.style->getColorRole(ftk::ColorRole::Button);
            const ftk::Color4F borderColor = event.style->getColorRole(ftk::ColorRole::Border);
            if (a.min.y == b.min.y)
            {
                const int dividerWidth = 12;
                const int dividerX = a.max.x - dividerWidth / 2;
                event.render->drawRect(
                    ftk::Box2I(dividerX, value.min.y, dividerWidth, value.h()),
                    dividerFill);
                event.render->drawRect(
                    ftk::Box2I(dividerX + dividerWidth / 2, value.min.y, 1, value.h()),
                    borderColor);
            }
            else
            {
                const int dividerHeight = 12;
                const int dividerY = a.max.y - dividerHeight / 2;
                event.render->drawRect(
                    ftk::Box2I(value.min.x, dividerY, value.w(), dividerHeight),
                    dividerFill);
                event.render->drawRect(
                    ftk::Box2I(value.min.x, dividerY + dividerHeight / 2, value.w(), 1),
                    borderColor);
            }
        }

        void Viewport::_layoutEmptyState(const ftk::Box2I& value)
        {
            FTK_P();

            const auto paneStates = _getPaneStates();
            const auto emptyBoxes = _getEmptyStateBoxes(value);
            for (size_t i = 0; i < p.emptyPanes.size(); ++i)
            {
                auto& pane = p.emptyPanes[i];
                const bool visible = paneStates[i] != PaneState::Hidden && i < emptyBoxes.size();
                pane.layout->setVisible(visible);
                pane.geometry = visible ? emptyBoxes[i] : ftk::Box2I();
                if (visible)
                {
                    const auto size = pane.layout->getSizeHint();
                    const int x = pane.geometry.min.x + (pane.geometry.w() - size.w) / 2;
                    const int yOffset = std::min(72, std::max(40, pane.geometry.h() / 4));
                    const int y = pane.geometry.min.y + (pane.geometry.h() - size.h) / 2 + yOffset;
                    pane.layout->setGeometry(ftk::Box2I(x, y, size.w, size.h));
                }
            }
        }

        void Viewport::_emptyStateUpdate()
        {
            FTK_P();
            const auto paneStates = _getPaneStates();

            if (Role::SplitA == _p->role)
            {
                if (paneStates[0] == PaneState::Loading)
                {
                    p.emptyPanes[0].titleLabel->setText("正在加载 A 视频");
                    p.emptyPanes[0].dropLabel->setText("请稍候，正在准备 A 视频首帧");
                    p.emptyPanes[0].openButton->setVisible(false);
                }
                else
                {
                    p.emptyPanes[0].titleLabel->setText("未加载 A 视频");
                    p.emptyPanes[0].dropLabel->setText("请选择 A 视频，或拖拽文件到该区域");
                    p.emptyPanes[0].openButton->setText("打开 A 视频");
                    p.emptyPanes[0].openButton->setVisible(true);
                }
            }
            else if (Role::SplitB == _p->role)
            {
                if (paneStates[0] == PaneState::Loading)
                {
                    p.emptyPanes[0].titleLabel->setText("正在加载 B 视频");
                    p.emptyPanes[0].dropLabel->setText("请稍候，正在准备 B 视频首帧");
                    p.emptyPanes[0].openButton->setVisible(false);
                }
                else
                {
                    p.emptyPanes[0].titleLabel->setText("未加载 B 视频");
                    p.emptyPanes[0].dropLabel->setText("请选择 B 视频，或拖拽文件到该区域");
                    p.emptyPanes[0].openButton->setText("打开 B 视频");
                    p.emptyPanes[0].openButton->setVisible(true);
                }
            }
            else if (_isDualEmptyState())
            {
                if (paneStates[0] == PaneState::Loading)
                {
                    p.emptyPanes[0].titleLabel->setText("正在加载 A 视频");
                    p.emptyPanes[0].dropLabel->setText("请稍候，正在准备 A 视频首帧");
                    p.emptyPanes[0].openButton->setVisible(false);
                }
                else
                {
                    p.emptyPanes[0].titleLabel->setText("未加载 A 视频");
                    p.emptyPanes[0].dropLabel->setText("请选择 A 视频，或拖拽文件到该区域");
                    p.emptyPanes[0].openButton->setText("打开 A 视频");
                    p.emptyPanes[0].openButton->setVisible(true);
                }

                if (paneStates[1] == PaneState::Loading)
                {
                    p.emptyPanes[1].titleLabel->setText("正在加载 B 视频");
                    p.emptyPanes[1].dropLabel->setText("请稍候，正在准备 B 视频首帧");
                    p.emptyPanes[1].openButton->setVisible(false);
                }
                else
                {
                    p.emptyPanes[1].titleLabel->setText("未加载 B 视频");
                    p.emptyPanes[1].dropLabel->setText("请选择 B 视频，或拖拽文件到该区域");
                    p.emptyPanes[1].openButton->setText("打开 B 视频");
                    p.emptyPanes[1].openButton->setVisible(true);
                }
            }
            else
            {
                if (paneStates[0] == PaneState::Loading)
                {
                    p.emptyPanes[0].titleLabel->setText("正在加载视频");
                    p.emptyPanes[0].dropLabel->setText("请稍候，正在准备视频首帧");
                    p.emptyPanes[0].openButton->setVisible(false);
                }
                else
                {
                    p.emptyPanes[0].titleLabel->setText("未加载视频");
                    p.emptyPanes[0].dropLabel->setText("请选择文件或拖拽文件到该区域");
                    p.emptyPanes[0].openButton->setText("打开文件");
                    p.emptyPanes[0].openButton->setVisible(true);
                }
            }

            _layoutEmptyState(getGeometry());
        }

        void Viewport::_hudUpdate()
        {
            FTK_P();

            std::string s = p.path.getFileName();
            p.fileNameLabel->setText(!s.empty() ? s : "(No file)");

            s = std::string();
            if (auto app = p.app.lock())
            {
                auto timeUnitsModel = app->getTimeUnitsModel();
                s = timeUnitsModel->getLabel(p.currentTime);
            }
            p.timeLabel->setText(ftk::Format("Time: {0}, {1} FPS, {2} dropped").
                arg(s).
                arg(p.fps, 2, 5).
                arg(static_cast<int>(p.droppedFrames), 3));

            const auto& colorSample = p.colorSample->get();
            p.colorPickerSwatch->setColor(colorSample);
            p.colorPickerLabel->setText(
                ftk::Format("Color: {0} {1} {2} {3}, Pixel: {4}").
                arg(colorSample.r, 2).
                arg(colorSample.g, 2).
                arg(colorSample.b, 2).
                arg(colorSample.a, 2).
                arg(p.pick->get()));

            p.cacheLabel->setText(
                ftk::Format("Cache: {0}% V, {1}% A").
                arg(static_cast<int>(p.cacheInfo.videoPercentage), 3).
                arg(static_cast<int>(p.cacheInfo.audioPercentage), 3));

            p.hudLayout->setVisible(p.hud);
        }
    }
}

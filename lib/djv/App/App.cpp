// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/App.h>

#include <djv/App/MainWindow.h>
#include <djv/App/SecondaryWindow.h>
#include <djv/App/Viewport.h>
#include <djv/UI/SeparateAudioDialog.h>
#include <djv/Models/AppInfoModel.h>
#include <djv/Models/AudioModel.h>
#include <djv/Models/ColorModel.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/RecentFilesModel.h>
#include <djv/Models/TimeUnitsModel.h>
#include <djv/Models/ToolsModel.h>
#include <djv/Models/ViewportModel.h>
#if defined(TLRENDER_BMD)
#include <djv/Models/BMDDevicesModel.h>
#endif // TLRENDER_BMD

#include <tlRender/UI/ThumbnailSystem.h>
#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/CompareOptions.h>
#include <tlRender/Timeline/Util.h>
#if defined(TLRENDER_BMD)
#include <tlRender/Device/BMDDevicesModel.h>
#include <tlRender/Device/BMDOutputDevice.h>
#endif // TLRENDER_BMD
#include <tlRender/IO/Plugin.h>
#include <tlRender/IO/System.h>
#if defined(TLRENDER_FFMPEG_PLUGIN)
#include <tlRender/IO/FFmpeg.h>
#endif // TLRENDER_FFMPEG_PLUGIN
#if defined(TLRENDER_USD)
#include <tlRender/IO/USD.h>
#endif // TLRENDER_USD

#include <ftk/UI/FileBrowser.h>
#include <ftk/UI/Settings.h>
#include <ftk/UI/SysLogModel.h>
#include <ftk/Core/CmdLine.h>
#include <ftk/Core/FileLogSystem.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/Timer.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <thread>

namespace djv
{
    namespace app
    {
        namespace
        {
            const double minPlaybackSpeedMult = 1.0 / 3.0;
            const double maxPlaybackSpeedMult = 3.0;
            const size_t highMemoryFrameByteCount = 16 * 1024 * 1024;
            const float bytesPerGB = 1024.F * 1024.F * 1024.F;

            size_t getRecommendedVideoRequestMax()
            {
                return 64;
            }

            size_t getRecommendedAudioRequestMax()
            {
                return 8;
            }

            size_t getMaxVideoFrameByteCount(const std::shared_ptr<tl::Timeline>& timeline)
            {
                size_t out = 0;
                if (timeline)
                {
                    for (const auto& video : timeline->getIOInfo().video)
                    {
                        out = std::max(out, video.getByteCount());
                    }
                }
                return out;
            }

            size_t getMaxVideoFrameByteCount(const std::shared_ptr<tl::Player>& player)
            {
                size_t out = 0;
                if (player)
                {
                    for (const auto& video : player->getIOInfo().video)
                    {
                        out = std::max(out, video.getByteCount());
                    }
                }
                return out;
            }

            bool isSplitCompareMode(tl::Compare compare)
            {
                switch (compare)
                {
                case tl::Compare::Horizontal:
                case tl::Compare::Vertical:
                case tl::Compare::Tile:
                    return true;
                default: return false;
                }
            }

            bool isSyncPlaybackMode(models::DualPlaybackMode value)
            {
                return models::DualPlaybackMode::Sync == value;
            }

            bool isDualDisplayMode(models::DisplayMode value)
            {
                return models::DisplayMode::Dual == value;
            }

            bool isHighMemoryFrame(size_t byteCount)
            {
                return byteCount >= highMemoryFrameByteCount;
            }

            float getVideoCacheGBForFrames(size_t frameByteCount, size_t frameCount)
            {
                if (0 == frameByteCount || 0 == frameCount)
                {
                    return 0.F;
                }
                return std::max(
                    0.001F,
                    (static_cast<float>(frameByteCount) * static_cast<float>(frameCount)) / bytesPerGB);
            }

            tl::PlayerCacheOptions getBudgetedCacheOptions(
                tl::PlayerCacheOptions value,
                size_t maxVideoFrameByteCount,
                bool withAudio,
                bool dualBudget)
            {
                const bool highMemoryVideo = isHighMemoryFrame(maxVideoFrameByteCount);
                const bool constrainedVideoPlayback = highMemoryVideo || !withAudio || dualBudget;
                if (constrainedVideoPlayback)
                {
                    const size_t cacheFrameCount = highMemoryVideo ?
                        (dualBudget ? 16 : 128) :
                        (dualBudget ? 4 : 8);
                    const float frameBudgetGB = getVideoCacheGBForFrames(
                        maxVideoFrameByteCount,
                        cacheFrameCount);
                    if (frameBudgetGB > 0.F)
                    {
                        value.videoGB = highMemoryVideo && !dualBudget ?
                            frameBudgetGB :
                            std::min(value.videoGB, frameBudgetGB);
                    }
                    else
                    {
                        value.videoGB = std::min(value.videoGB, dualBudget ? 0.06F : 0.12F);
                    }
                    value.readBehind = 0.F;
                }
                if (!withAudio)
                {
                    value.audioGB = 0.F;
                }
                else if (highMemoryVideo)
                {
                    value.audioGB = std::min(value.audioGB, 0.05F);
                }
                return value;
            }

            void applyCacheBudget(
                const std::shared_ptr<tl::Player>& player,
                const tl::PlayerCacheOptions& value,
                bool withAudio,
                bool dualBudget)
            {
                if (player)
                {
                    player->setCacheOptions(getBudgetedCacheOptions(
                        value,
                        getMaxVideoFrameByteCount(player),
                        withAudio,
                        dualBudget));
                }
            }

            double clampPlaybackSpeed(double value, double defaultSpeed)
            {
                if (defaultSpeed <= 0.0)
                {
                    return value;
                }
                return std::max(
                    defaultSpeed * minPlaybackSpeedMult,
                    std::min(
                        value,
                        defaultSpeed * maxPlaybackSpeedMult));
            }

            double clampPlaybackSpeedMult(
                double value,
                double speed,
                double defaultSpeed)
            {
                if (speed <= 0.0 || defaultSpeed <= 0.0)
                {
                    return value;
                }
                return std::max(
                    defaultSpeed * minPlaybackSpeedMult / speed,
                    std::min(
                        value,
                        defaultSpeed * maxPlaybackSpeedMult / speed));
            }

            void capturePlayerState(
                const std::shared_ptr<tl::Player>& player,
                const std::shared_ptr<models::FilesModelItem>& item)
            {
                if (player && item)
                {
                    item->speed = player->getSpeed();
                    item->currentTime = player->getCurrentTime();
                    item->inOutRange = player->getInOutRange();
                    if (player->getPlayback() != tl::Playback::Stop)
                    {
                        item->playback = player->getPlayback();
                    }
                }
            }

            tl::PlayerOptions createPlayerOptions(
                const std::shared_ptr<tl::Timeline>& timeline,
                const std::shared_ptr<models::SettingsModel>& settingsModel,
                const std::shared_ptr<models::AudioModel>& audioModel,
                bool withAudio,
                bool dualBudget = false)
            {
                tl::PlayerOptions out;
                out.audioDevice = withAudio ? audioModel->getDevice() : tl::AudioDeviceID();
                const models::AdvancedSettings advanced = settingsModel->getAdvanced();
                const size_t maxVideoFrameByteCount = getMaxVideoFrameByteCount(timeline);
                const bool highMemoryVideo = isHighMemoryFrame(maxVideoFrameByteCount);
                out.cache = getBudgetedCacheOptions(
                    settingsModel->getCache(),
                    maxVideoFrameByteCount,
                    withAudio,
                    dualBudget);
                out.videoRequestMax = std::max(
                    size_t(1),
                    std::min(advanced.videoRequestMax, getRecommendedVideoRequestMax()));
                out.audioRequestMax = std::max(
                    size_t(0),
                    std::min(advanced.audioRequestMax, getRecommendedAudioRequestMax()));
                out.audioBufferFrameCount = advanced.audioBufferFrameCount;
                if (highMemoryVideo || !withAudio || dualBudget)
                {
                    const size_t maxVideoRequests =
                        dualBudget && highMemoryVideo ? 8 :
                        (highMemoryVideo ? 64 : 2);
                    if (highMemoryVideo && !dualBudget)
                    {
                        out.videoRequestMax = maxVideoRequests;
                    }
                    else
                    {
                        out.videoRequestMax = std::min<size_t>(
                            out.videoRequestMax,
                            maxVideoRequests);
                    }
                }
                if (!withAudio)
                {
                    out.audioRequestMax = 0;
                }
                else if (highMemoryVideo)
                {
                    out.audioRequestMax = std::min<size_t>(out.audioRequestMax, 4);
                    out.audioBufferFrameCount = std::min<size_t>(out.audioBufferFrameCount, 256);
                }
                return out;
            }

            std::shared_ptr<tl::Player> createPlayer(
                const std::shared_ptr<ftk::Context>& context,
                const std::shared_ptr<tl::Timeline>& timeline,
                const std::shared_ptr<models::SettingsModel>& settingsModel,
                const std::shared_ptr<models::AudioModel>& audioModel,
                bool withAudio,
                bool dualBudget = false)
            {
                if (!timeline)
                {
                    return nullptr;
                }
                return tl::Player::create(
                    context,
                    timeline,
                    createPlayerOptions(
                        timeline,
                        settingsModel,
                        audioModel,
                        withAudio,
                        dualBudget));
            }

            OTIO_NS::RationalTime clampToTimeRange(
                const OTIO_NS::RationalTime& time,
                const OTIO_NS::TimeRange& range)
            {
                if (time.strictly_equal(tl::invalidTime) ||
                    tl::compareExact(range, tl::invalidTimeRange))
                {
                    return tl::invalidTime;
                }

                OTIO_NS::RationalTime out = time.rescaled_to(range.duration()).floor();
                const OTIO_NS::RationalTime start = range.start_time();
                const OTIO_NS::RationalTime end = range.end_time_inclusive();
                if (out < start)
                {
                    out = start;
                }
                else if (out > end)
                {
                    out = end;
                }
                return out;
            }

            OTIO_NS::TimeRange clampToTimeRange(
                const OTIO_NS::TimeRange& range,
                const OTIO_NS::TimeRange& bounds)
            {
                if (tl::compareExact(range, tl::invalidTimeRange) ||
                    tl::compareExact(bounds, tl::invalidTimeRange))
                {
                    return tl::invalidTimeRange;
                }

                OTIO_NS::RationalTime start = clampToTimeRange(range.start_time(), bounds);
                OTIO_NS::RationalTime end = clampToTimeRange(range.end_time_inclusive(), bounds);
                if (start.strictly_equal(tl::invalidTime) ||
                    end.strictly_equal(tl::invalidTime))
                {
                    return tl::invalidTimeRange;
                }
                if (end < start)
                {
                    end = start;
                }
                return OTIO_NS::TimeRange::range_from_start_end_time_inclusive(start, end);
            }

            bool requiresSafeReverse(
                const std::shared_ptr<tl::Player>&,
                bool)
            {
                return false;
            }

            OTIO_NS::RationalTime getDisplayedOrCurrentTime(const std::shared_ptr<tl::Player>& player)
            {
                if (!player)
                {
                    return tl::invalidTime;
                }
                const OTIO_NS::RationalTime currentTime = player->getCurrentTime();
                if (!currentTime.strictly_equal(tl::invalidTime))
                {
                    return currentTime;
                }
                const auto currentVideo = player->getCurrentVideo();
                if (!currentVideo.empty() &&
                    !currentVideo.front().time.strictly_equal(tl::invalidTime))
                {
                    return currentVideo.front().time;
                }
                return tl::invalidTime;
            }

            bool canReverseFromCurrentTime(const std::shared_ptr<tl::Player>& player)
            {
                if (!player)
                {
                    return false;
                }
                const OTIO_NS::TimeRange& range = player->getTimeRange();
                if (tl::compareExact(range, tl::invalidTimeRange))
                {
                    return false;
                }
                const OTIO_NS::RationalTime current = getDisplayedOrCurrentTime(player);
                if (current.strictly_equal(tl::invalidTime))
                {
                    return false;
                }
                const OTIO_NS::RationalTime currentAtRate =
                    current.rescaled_to(range.start_time().rate()).floor();
                return currentAtRate > range.start_time();
            }

            bool reverseOrStepBackSafely(
                const std::shared_ptr<tl::Player>& player,
                bool dualBudget)
            {
                if (!player)
                {
                    return false;
                }
                if (!canReverseFromCurrentTime(player))
                {
                    player->stop();
                    return false;
                }
                if (!requiresSafeReverse(player, dualBudget))
                {
                    player->reverse();
                    return true;
                }

                player->stop();
                const double rate = player->getTimeRange().duration().rate();
                if (rate > 0.0)
                {
                    player->seek(clampToTimeRange(
                        player->getCurrentTime() - OTIO_NS::RationalTime(1.0, rate),
                        player->getTimeRange()));
                }
                return false;
            }

            OTIO_NS::RationalTime addTimeDeltaClamped(
                const OTIO_NS::RationalTime& current,
                const OTIO_NS::RationalTime& from,
                const OTIO_NS::RationalTime& to,
                const OTIO_NS::TimeRange& range)
            {
                if (current.strictly_equal(tl::invalidTime) ||
                    from.strictly_equal(tl::invalidTime) ||
                    to.strictly_equal(tl::invalidTime) ||
                    tl::compareExact(range, tl::invalidTimeRange))
                {
                    return tl::invalidTime;
                }

                const OTIO_NS::RationalTime fromAtRate = from.rescaled_to(current.rate());
                const OTIO_NS::RationalTime toAtRate = to.rescaled_to(current.rate());
                return clampToTimeRange(
                    OTIO_NS::RationalTime(
                        current.value() + (toAtRate.value() - fromAtRate.value()),
                        current.rate()),
                    range);
            }

            double getTimeSeconds(const OTIO_NS::RationalTime& time)
            {
                return time.strictly_equal(tl::invalidTime) ?
                    0.0 :
                    time.rescaled_to(1.0).value();
            }

            OTIO_NS::RationalTime addSecondsDeltaClamped(
                const OTIO_NS::RationalTime& time,
                double secondsDelta,
                const OTIO_NS::TimeRange& range)
            {
                if (time.strictly_equal(tl::invalidTime) ||
                    tl::compareExact(range, tl::invalidTimeRange))
                {
                    return tl::invalidTime;
                }
                return clampToTimeRange(
                    OTIO_NS::RationalTime(
                        time.rescaled_to(1.0).value() + secondsDelta,
                        1.0).rescaled_to(range.duration().rate()).round(),
                    range);
            }

            OTIO_NS::RationalTime mapCompareTime(
                const std::shared_ptr<tl::Player>& source,
                const std::shared_ptr<tl::Player>& target,
                tl::CompareTime compareTime)
            {
                if (!source || !target)
                {
                    return tl::invalidTime;
                }
                return tl::getCompareTime(
                    source->getCurrentTime(),
                    source->getTimeRange(),
                    target->getTimeRange(),
                    compareTime);
            }

            OTIO_NS::RationalTime mapCompareTimeClamped(
                const std::shared_ptr<tl::Player>& source,
                const std::shared_ptr<tl::Player>& target,
                tl::CompareTime compareTime)
            {
                if (!source || !target)
                {
                    return tl::invalidTime;
                }
                return clampToTimeRange(
                    tl::getCompareTime(
                        source->getCurrentTime(),
                        source->getTimeRange(),
                        target->getTimeRange(),
                        compareTime),
                    target->getTimeRange());
            }

            OTIO_NS::TimeRange mapCompareRange(
                const std::shared_ptr<tl::Player>& source,
                const std::shared_ptr<tl::Player>& target,
                const OTIO_NS::TimeRange& sourceRange,
                tl::CompareTime compareTime)
            {
                if (!source || !target || tl::compareExact(sourceRange, tl::invalidTimeRange))
                {
                    return tl::invalidTimeRange;
                }

                const OTIO_NS::RationalTime start = tl::getCompareTime(
                    sourceRange.start_time(),
                    source->getTimeRange(),
                    target->getTimeRange(),
                    compareTime);
                const OTIO_NS::RationalTime end = tl::getCompareTime(
                    sourceRange.end_time_inclusive(),
                    source->getTimeRange(),
                    target->getTimeRange(),
                    compareTime);
                return OTIO_NS::TimeRange::range_from_start_end_time_inclusive(start, end);
            }

            OTIO_NS::TimeRange mapCompareRangeClamped(
                const std::shared_ptr<tl::Player>& source,
                const std::shared_ptr<tl::Player>& target,
                const OTIO_NS::TimeRange& sourceRange,
                tl::CompareTime compareTime)
            {
                if (!source || !target || tl::compareExact(sourceRange, tl::invalidTimeRange))
                {
                    return tl::invalidTimeRange;
                }

                return clampToTimeRange(
                    mapCompareRange(
                        source,
                        target,
                        sourceRange,
                        compareTime),
                    target->getTimeRange());
            }

            double getTimelineDurationSeconds(const std::shared_ptr<tl::Timeline>& timeline)
            {
                return timeline ?
                    timeline->getTimeRange().duration().rescaled_to(1.0).value() :
                    -1.0;
            }

            double getTimeDifferenceInFrames(
                const OTIO_NS::RationalTime& a,
                const OTIO_NS::RationalTime& b)
            {
                if (a.strictly_equal(tl::invalidTime) ||
                    b.strictly_equal(tl::invalidTime))
                {
                    return std::numeric_limits<double>::max();
                }
                const OTIO_NS::RationalTime bAtRate = b.rescaled_to(a.rate());
                return std::abs(a.value() - bAtRate.value());
            }

            double getSignedTimeDifferenceInFrames(
                const OTIO_NS::RationalTime& a,
                const OTIO_NS::RationalTime& b)
            {
                if (a.strictly_equal(tl::invalidTime) ||
                    b.strictly_equal(tl::invalidTime))
                {
                    return std::numeric_limits<double>::max();
                }
                const OTIO_NS::RationalTime bAtRate = b.rescaled_to(a.rate());
                return a.value() - bAtRate.value();
            }

            void syncPlayingPlayerToTime(
                const std::shared_ptr<tl::Player>& player,
                const OTIO_NS::RationalTime& time,
                tl::Playback playback)
            {
                if (!player || time.strictly_equal(tl::invalidTime))
                {
                    return;
                }
                player->stop();
                player->seek(time);
                if (tl::Playback::Forward == playback)
                {
                    player->forward();
                }
                else if (tl::Playback::Reverse == playback)
                {
                    player->reverse();
                }
            }
        }

        struct CmdLine
        {
            std::shared_ptr<ftk::CmdLineListArg<std::string> > inputs;
            std::shared_ptr<ftk::CmdLineOption<std::string> > audioFileName;
            std::shared_ptr<ftk::CmdLineOption<std::string> > compareFileName;
            std::shared_ptr<ftk::CmdLineOption<tl::Compare> > compare;
            std::shared_ptr<ftk::CmdLineOption<ftk::V2F> > wipeCenter;
            std::shared_ptr<ftk::CmdLineOption<float> > wipeRotation;
            std::shared_ptr<ftk::CmdLineOption<double> > speed;
            std::shared_ptr<ftk::CmdLineOption<tl::Playback> > playback;
            std::shared_ptr<ftk::CmdLineOption<tl::Loop> > loop;
            std::shared_ptr<ftk::CmdLineOption<tl::TimeUnits> > timeUnits;
            std::shared_ptr<ftk::CmdLineOption<std::string> > seek;
            std::shared_ptr<ftk::CmdLineOption<std::string> > inPoint;
            std::shared_ptr<ftk::CmdLineOption<std::string> > outPoint;
            std::shared_ptr<ftk::CmdLineOption<std::string> > ocioFileName;
            std::shared_ptr<ftk::CmdLineOption<std::string> > ocioInput;
            std::shared_ptr<ftk::CmdLineOption<std::string> > ocioDisplay;
            std::shared_ptr<ftk::CmdLineOption<std::string> > ocioView;
            std::shared_ptr<ftk::CmdLineOption<std::string> > ocioLook;
            std::shared_ptr<ftk::CmdLineOption<std::string> > lutFileName;
            std::shared_ptr<ftk::CmdLineOption<tl::LUTOrder> > lutOrder;
#if defined(TLRENDER_USD)
            std::shared_ptr<ftk::CmdLineOption<int> > usdRenderWidth;
            std::shared_ptr<ftk::CmdLineOption<float> > usdComplexity;
            std::shared_ptr<ftk::CmdLineOption<tl::usd::DrawMode> > usdDrawMode;
            std::shared_ptr<ftk::CmdLineOption<bool> > usdEnableLighting;
            std::shared_ptr<ftk::CmdLineOption<bool> > usdSRGB;
            std::shared_ptr<ftk::CmdLineOption<int> > usdStageCacheCount;
            std::shared_ptr<ftk::CmdLineOption<int> > usdDiskCacheGB;
#endif // TLRENDER_USD
            std::shared_ptr<ftk::CmdLineOption<std::string> > logFileName;
            std::shared_ptr<ftk::CmdLineFlag> resetSettings;
            std::shared_ptr<ftk::CmdLineOption<std::string> > settingsFileName;
            std::shared_ptr<ftk::CmdLineFlag> version;
            std::shared_ptr<ftk::CmdLineOption<int> > debugLoop;
        };

        struct App::Private
        {
            std::filesystem::path logFile;
            std::filesystem::path settingsFile;
            CmdLine cmdLine;

            std::shared_ptr<models::AppInfoModel> appInfoModel;
            std::shared_ptr<ftk::FileLogSystem> fileLogSystem;
            std::shared_ptr<ftk::Settings> settings;
            std::shared_ptr<models::SettingsModel> settingsModel;
            std::shared_ptr<ftk::SysLogModel> sysLogModel;
            std::shared_ptr<models::TimeUnitsModel> timeUnitsModel;
            std::shared_ptr<models::FilesModel> filesModel;
            std::vector<std::shared_ptr<models::FilesModelItem> > files;
            std::vector<std::shared_ptr<models::FilesModelItem> > activeFiles;
            std::shared_ptr<models::FilesModelItem> splitAItem;
            std::shared_ptr<models::FilesModelItem> splitBItem;
            std::shared_ptr<models::FilesModelItem> splitMasterItem;
            size_t splitMasterIndex = 0;
            App::SplitPane splitActivePane = App::SplitPane::A;
            OTIO_NS::RationalTime splitTransportTime = tl::invalidTime;
            bool splitGlobalPlaybackActive = false;
            bool splitSyncAdjusting = false;
            double splitSyncOffsetSecondsA = 0.0;
            double splitSyncOffsetSecondsB = 0.0;
            OTIO_NS::RationalTime splitDisplayedTimeA = tl::invalidTime;
            OTIO_NS::RationalTime splitDisplayedTimeB = tl::invalidTime;
            std::shared_ptr<models::RecentFilesModel> recentFilesModel;
            std::vector<std::shared_ptr<tl::Timeline> > timelines;
            std::shared_ptr<ftk::Observable<std::shared_ptr<tl::Player> > > player;
            std::shared_ptr<ftk::Observable<std::shared_ptr<tl::Player> > > playerA;
            std::shared_ptr<ftk::Observable<std::shared_ptr<tl::Player> > > playerB;
            std::shared_ptr<models::ColorModel> colorModel;
            std::shared_ptr<models::ViewportModel> viewportModel;
            std::shared_ptr<models::AudioModel> audioModel;
            std::shared_ptr<models::ToolsModel> toolsModel;

            std::shared_ptr<ftk::Observable<bool> > secondaryWindowActive;
            std::shared_ptr<MainWindow> mainWindow;
            std::shared_ptr<SecondaryWindow> secondaryWindow;
            std::shared_ptr<ftk::Timer> startupLayoutTimer;
            std::shared_ptr<ftk::Timer> startupScaleTimer;
            std::shared_ptr<ui::SeparateAudioDialog> separateAudioDialog;

            bool bmdDeviceActive = false;
#if defined(TLRENDER_BMD)
            std::shared_ptr<models::BMDDevicesModel> bmdDevicesModel;
            std::shared_ptr<tl::bmd::OutputDevice> bmdOutputDevice;
#endif // TLRENDER_BMD

            std::shared_ptr<ftk::Observer<tl::PlayerCacheOptions> > cacheObserver;
            std::shared_ptr<ftk::ListObserver<std::shared_ptr<models::FilesModelItem> > > filesObserver;
            std::shared_ptr<ftk::ListObserver<std::shared_ptr<models::FilesModelItem> > > activeObserver;
            std::shared_ptr<ftk::ListObserver<int> > layersObserver;
            std::shared_ptr<ftk::Observer<tl::CompareTime> > compareTimeObserver;
            std::shared_ptr<ftk::Observer<models::DisplayMode> > displayModeObserver;
            std::shared_ptr<ftk::Observer<models::DualPlaybackMode> > dualPlaybackModeObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > masterPlayerObserver;
            std::shared_ptr<ftk::Observer<tl::Playback> > masterPlaybackObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > masterCurrentTimeObserver;
            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > masterCurrentVideoObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::TimeRange> > masterInOutRangeObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > splitAPlayerObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > splitBPlayerObserver;
            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > splitACurrentVideoObserver;
            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > splitBCurrentVideoObserver;
            std::shared_ptr<ftk::Observer<std::pair<ftk::V2I, double> > > viewPosZoomObserver;
            std::shared_ptr<ftk::Observer<bool> > viewFramedObserver;
            std::shared_ptr<ftk::Observer<tl::AudioDeviceID> > audioDeviceObserver;
            std::shared_ptr<ftk::Observer<float> > volumeObserver;
            std::shared_ptr<ftk::Observer<bool> > muteObserver;
            std::shared_ptr<ftk::ListObserver<bool> > channelMuteObserver;
            std::shared_ptr<ftk::Observer<double> > syncOffsetObserver;
            std::shared_ptr<ftk::Observer<models::StyleSettings> > styleSettingsObserver;
            std::shared_ptr<ftk::Observer<models::MiscSettings> > miscSettingsObserver;
#if defined(TLRENDER_BMD)
            std::shared_ptr<ftk::Observer<tl::bmd::DevicesModelData> > bmdDevicesObserver;
            std::shared_ptr<ftk::Observer<bool> > bmdActiveObserver;
            std::shared_ptr<ftk::Observer<ftk::Size2I> > bmdSizeObserver;
            std::shared_ptr<ftk::Observer<tl::bmd::FrameRate> > bmdFrameRateObserver;
            std::shared_ptr<ftk::Observer<tl::OCIOOptions> > ocioOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::LUTOptions> > lutOptionsObserver;
            std::shared_ptr<ftk::Observer<ftk::ImageOptions> > imageOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::DisplayOptions> > displayOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::CompareOptions> > compareOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::BackgroundOptions> > bgOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::ForegroundOptions> > fgOptionsObserver;
#endif // TLRENDER_BMD

            std::shared_ptr<ftk::Timer> debugTimer;
            int debugInput = 0;
        };

        void App::_init(
            const std::shared_ptr<ftk::Context>& context,
            std::vector<std::string>& argv,
            const std::shared_ptr<models::AppInfoModel>& appInfoModel)
        {
            FTK_P();

            p.appInfoModel = appInfoModel ? appInfoModel : models::AppInfoModel::create();
            p.logFile = _getLogFilePath();
            p.settingsFile = _getSettingsPath();

            p.cmdLine.inputs = ftk::CmdLineListArg<std::string>::create(
                "input",
                "One or more timelines, movies, image sequences, or directories.",
                true);
            p.cmdLine.audioFileName = ftk::CmdLineOption<std::string>::create(
                { "-audio", "-a" },
                "Audio file name.",
                "Audio");
            p.cmdLine.compareFileName = ftk::CmdLineOption<std::string>::create(
                { "-compare", "-b" },
                "Compare \"B\" file name.",
                "Compare");
            p.cmdLine.compare = ftk::CmdLineOption<tl::Compare>::create(
                { "-compareMode", "-c" },
                "Compare mode.",
                "Compare",
                std::optional<tl::Compare>(),
                ftk::quotes(tl::getCompareLabels()));
            p.cmdLine.wipeCenter = ftk::CmdLineOption<ftk::V2F>::create(
                { "-wipeCenter", "-wc" },
                "Wipe center.",
                "Compare",
                tl::CompareOptions().wipeCenter);
            p.cmdLine.wipeRotation = ftk::CmdLineOption<float>::create(
                { "-wipeRotation", "-wr" },
                "Wipe rotation.",
                "Compare",
                0.F);
            p.cmdLine.speed = ftk::CmdLineOption<double>::create(
                { "-speed" },
                "Playback speed.",
                "Playback");
            p.cmdLine.playback = ftk::CmdLineOption<tl::Playback>::create(
                { "-playback", "-p" },
                "Playback mode.",
                "Playback",
                std::optional<tl::Playback>(),
                ftk::quotes(tl::getPlaybackLabels()));
            p.cmdLine.loop = ftk::CmdLineOption<tl::Loop>::create(
                { "-loop" },
                "Loop mode.",
                "Playback",
                std::optional<tl::Loop>(),
                ftk::quotes(tl::getLoopLabels()));
            p.cmdLine.timeUnits = ftk::CmdLineOption<tl::TimeUnits>::create(
                { "-timeUnits", "-tu" },
                "Set the time units.",
                "Playback",
                std::optional<tl::TimeUnits>(),
                ftk::quotes(tl::getTimeUnitsLabels()));
            p.cmdLine.seek = ftk::CmdLineOption<std::string>::create(
                { "-seek" },
                "Seek to the given time.",
                "Playback");
            p.cmdLine.inPoint = ftk::CmdLineOption<std::string>::create(
                { "-inPoint", "-in" },
                "Set the in point.",
                "Playback");
            p.cmdLine.outPoint = ftk::CmdLineOption<std::string>::create(
                { "-outPoint", "-out" },
                "Set the out point.",
                "Playback");
            p.cmdLine.ocioFileName = ftk::CmdLineOption<std::string>::create(
                { "-ocio" },
                "OCIO configuration file name (e.g., config.ocio).",
                "Color");
            p.cmdLine.ocioInput = ftk::CmdLineOption<std::string>::create(
                { "-ocioInput" },
                "OCIO input name.",
                "Color");
            p.cmdLine.ocioDisplay = ftk::CmdLineOption<std::string>::create(
                { "-ocioDisplay" },
                "OCIO display name.",
                "Color");
            p.cmdLine.ocioView = ftk::CmdLineOption<std::string>::create(
                { "-ocioView" },
                "OCIO view name.",
                "Color");
            p.cmdLine.ocioLook = ftk::CmdLineOption<std::string>::create(
                { "-ocioLook" },
                "OCIO look name.",
                "Color");
            p.cmdLine.lutFileName = ftk::CmdLineOption<std::string>::create(
                { "-lut" },
                "LUT file name.",
                "Color");
            p.cmdLine.lutOrder = ftk::CmdLineOption<tl::LUTOrder>::create(
                { "-lutOrder" },
                "LUT operation order.",
                "Color",
                std::optional<tl::LUTOrder>(),
                ftk::quotes(tl::getLUTOrderLabels()));
#if defined(TLRENDER_USD)
            p.cmdLine.usdRenderWidth = ftk::CmdLineOption<int>::create(
                { "-usdRenderWidth" },
                "Render width.",
                "USD",
                1920);
            p.cmdLine.usdComplexity = ftk::CmdLineOption<float>::create(
                { "-usdComplexity" },
                "Render complexity setting.",
                "USD",
                1.F);
            p.cmdLine.usdDrawMode = ftk::CmdLineOption<tl::usd::DrawMode>::create(
                { "-usdDrawMode" },
                "Draw mode.",
                "USD",
                tl::usd::DrawMode::ShadedSmooth,
                ftk::quotes(tl::usd::getDrawModeLabels()));
            p.cmdLine.usdEnableLighting = ftk::CmdLineOption<bool>::create(
                { "-usdEnableLighting" },
                "Enable lighting.",
                "USD",
                true);
            p.cmdLine.usdSRGB = ftk::CmdLineOption<bool>::create(
                { "-usdSRGB" },
                "Enable sRGB color space.",
                "USD",
                true);
            p.cmdLine.usdStageCacheCount = ftk::CmdLineOption<int>::create(
                { "-usdStageCache" },
                "Number of USD stages to cache.",
                "USD",
                10);
            p.cmdLine.usdDiskCacheGB = ftk::CmdLineOption<int>::create(
                { "-usdDiskCache" },
                "Disk cache size in gigabytes. A size of zero disables the cache.",
                "USD",
                0);
#endif // TLRENDER_USD
            p.cmdLine.logFileName = ftk::CmdLineOption<std::string>::create(
                { "-logFile" },
                "Log file name.",
                std::string(),
                ftk::Format("{0}").arg(p.logFile.u8string()));
            p.cmdLine.resetSettings = ftk::CmdLineFlag::create(
                { "-resetSettings" },
                "Reset settings to defaults.");
            p.cmdLine.settingsFileName = ftk::CmdLineOption<std::string>::create(
                { "-settingsFile" },
                "Settings file name.",
                std::string(),
                ftk::Format("{0}").arg(p.settingsFile.u8string()));
            p.cmdLine.version = ftk::CmdLineFlag::create(
                { "-version" },
                "Print the version and exit.");
            p.cmdLine.debugLoop = ftk::CmdLineOption<int>::create(
                { "-debugLoop" },
                "Load the command line inputs in a loop. This value is the number of seconds for each cycle.",
                "Testing",
                10);

            ftk::App::_init(
                context,
                argv,
                p.appInfoModel->getShortName(),
                "Media playback and review.",
                { p.cmdLine.inputs },
                {
                    p.cmdLine.audioFileName,
                    p.cmdLine.compareFileName,
                    p.cmdLine.compare,
                    p.cmdLine.wipeCenter,
                    p.cmdLine.wipeRotation,
                    p.cmdLine.speed,
                    p.cmdLine.playback,
                    p.cmdLine.loop,
                    p.cmdLine.timeUnits,
                    p.cmdLine.seek,
                    p.cmdLine.inPoint,
                    p.cmdLine.outPoint,
                    p.cmdLine.ocioFileName,
                    p.cmdLine.ocioInput,
                    p.cmdLine.ocioDisplay,
                    p.cmdLine.ocioView,
                    p.cmdLine.ocioLook,
                    p.cmdLine.lutFileName,
                    p.cmdLine.lutOrder,
#if defined(TLRENDER_USD)
                    p.cmdLine.usdRenderWidth,
                    p.cmdLine.usdComplexity,
                    p.cmdLine.usdDrawMode,
                    p.cmdLine.usdEnableLighting,
                    p.cmdLine.usdSRGB,
                    p.cmdLine.usdStageCacheCount,
                    p.cmdLine.usdDiskCacheGB,
#endif // TLRENDER_USD
                    p.cmdLine.logFileName,
                    p.cmdLine.resetSettings,
                    p.cmdLine.settingsFileName,
                    p.cmdLine.version,
                    p.cmdLine.debugLoop
                });
        }

        App::App() :
            _p(new Private)
        {}

        App::~App()
        {}

        std::shared_ptr<App> App::create(
            const std::shared_ptr<ftk::Context>& context,
            std::vector<std::string>& argv,
            const std::shared_ptr<models::AppInfoModel>& appInfoModel)
        {
            auto out = std::shared_ptr<App>(new App);
            out->_init(context, argv, appInfoModel);
            return out;
        }

        const std::shared_ptr<models::AppInfoModel>& App::getAppInfoModel() const
        {
            return _p->appInfoModel;
        }

        const std::shared_ptr<ftk::Settings>& App::getSettings() const
        {
            return _p->settings;
        }

        const std::shared_ptr<models::SettingsModel>& App::getSettingsModel() const
        {
            return _p->settingsModel;
        }

        const std::shared_ptr<ftk::SysLogModel>& App::getSysLogModel() const
        {
            return _p->sysLogModel;
        }

        const std::shared_ptr<models::TimeUnitsModel>& App::getTimeUnitsModel() const
        {
            return _p->timeUnitsModel;
        }

        const std::shared_ptr<models::FilesModel>& App::getFilesModel() const
        {
            return _p->filesModel;
        }

        const std::shared_ptr<models::RecentFilesModel>& App::getRecentFilesModel() const
        {
            return _p->recentFilesModel;
        }

        const std::shared_ptr<models::ColorModel>& App::getColorModel() const
        {
            return _p->colorModel;
        }

        const std::shared_ptr<models::ViewportModel>& App::getViewportModel() const
        {
            return _p->viewportModel;
        }

        const std::shared_ptr<models::AudioModel>& App::getAudioModel() const
        {
            return _p->audioModel;
        }

        const std::shared_ptr<models::ToolsModel>& App::getToolsModel() const
        {
            return _p->toolsModel;
        }

#if defined(TLRENDER_BMD)
        const std::shared_ptr<models::BMDDevicesModel>& App::getBMDDevicesModel() const
        {
            return _p->bmdDevicesModel;
        }

        const std::shared_ptr<tl::bmd::OutputDevice>& App::getBMDOutputDevice() const
        {
            return _p->bmdOutputDevice;
        }
#endif // TLRENDER_BMD

        void App::openDialog()
        {
            openToADialog();
        }

        void App::openToADialog()
        {
            FTK_P();
            auto fileBrowserSystem = _context->getSystem<ftk::FileBrowserSystem>();
            fileBrowserSystem->open(
                p.mainWindow,
                [this](const ftk::Path& value)
                {
                    openToA(value);
                });
        }

        void App::openCompareDialog()
        {
            openToBDialog();
        }

        void App::openToBDialog()
        {
            FTK_P();
            auto fileBrowserSystem = _context->getSystem<ftk::FileBrowserSystem>();
            fileBrowserSystem->open(
                p.mainWindow,
                [this](const ftk::Path& value)
                {
                    openToB(value);
                });
        }

        void App::openSeparateAudioDialog()
        {
            FTK_P();
            p.separateAudioDialog = ui::SeparateAudioDialog::create(_context);
            p.separateAudioDialog->open(p.mainWindow);
            p.separateAudioDialog->setCallback(
                [this](const ftk::Path& value, const ftk::Path& audio)
                {
                    open(value, audio);
                    _p->separateAudioDialog->close();
                });
            p.separateAudioDialog->setCloseCallback(
                [this]
                {
                    _p->separateAudioDialog.reset();
                });
        }

        void App::open(const ftk::Path& path, const ftk::Path& audioPath)
        {
            openToA(path, audioPath);
        }

        void App::openToA(const ftk::Path& path, const ftk::Path& audioPath)
        {
            FTK_P();
            ftk::DirListOptions dirListOptions;
            dirListOptions.seqExts = tl::getExts(_context, static_cast<int>(tl::FileType::Seq));
            dirListOptions.seqMaxDigits = p.settingsModel->getImageSeq().maxDigits;
            for (const auto& i : tl::getPaths(_context, path, dirListOptions))
            {
                auto item = std::make_shared<models::FilesModelItem>();
                item->path = i;
                item->audioPath = audioPath;
                p.filesModel->add(item, true);
                p.recentFilesModel->addRecent(std::filesystem::u8path(path.get()));
            }
            if (!isDualDisplayMode(p.filesModel->getDisplayMode()))
            {
                p.filesModel->setDisplayMode(models::DisplayMode::Single);
            }
        }

        void App::openCompare(const ftk::Path& path)
        {
            openToB(path);
        }

        void App::openToB(const ftk::Path& path)
        {
            FTK_P();
            if (!p.filesModel->getA())
            {
                if (!isSplitCompareMode(p.filesModel->getCompareOptions().compare))
                {
                    openToA(path);
                    return;
                }
            }

            ftk::DirListOptions dirListOptions;
            dirListOptions.seqExts = tl::getExts(_context, static_cast<int>(tl::FileType::Seq));
            dirListOptions.seqMaxDigits = p.settingsModel->getImageSeq().maxDigits;
            for (const auto& i : tl::getPaths(_context, path, dirListOptions))
            {
                const int bIndex = static_cast<int>(p.filesModel->getFiles().size());
                auto item = std::make_shared<models::FilesModelItem>();
                item->path = i;
                p.filesModel->add(item, false, false);
                p.filesModel->setB(bIndex, true);
                p.recentFilesModel->addRecent(std::filesystem::u8path(path.get()));
            }

            auto compareOptions = p.filesModel->getCompareOptions();
            compareOptions.compare = tl::Compare::Horizontal;
            p.filesModel->setCompareOptions(compareOptions);
            p.filesModel->setDisplayMode(models::DisplayMode::Dual);
        }

        void App::reload()
        {
            FTK_P();
            const auto activeFiles = p.activeFiles;
            const auto files = p.files;
            for (const auto& i : activeFiles)
            {
                const auto j = std::find(p.files.begin(), p.files.end(), i);
                if (j != p.files.end())
                {
                    const size_t index = j - p.files.begin();
                    p.files.erase(j);
                    p.timelines.erase(p.timelines.begin() + index);
                }
            }
            p.activeFiles.clear();
            if (!activeFiles.empty())
            {
                if (isDualDisplayMode(p.filesModel->getDisplayMode()))
                {
                    capturePlayerState(p.playerA->get(), p.splitAItem);
                    capturePlayerState(p.playerB->get(), p.splitBItem);
                }
                else if (auto player = p.player->get())
                {
                    capturePlayerState(player, activeFiles.front());
                }
            }

            auto thumbnailSytem = _context->getSystem<tl::ui::ThumbnailSystem>();
            thumbnailSytem->clearCache();

            _filesUpdate(files);
            _activeUpdate(activeFiles);
        }

        std::shared_ptr<ftk::IObservable<std::shared_ptr<tl::Player> > > App::observePlayer() const
        {
            return _p->player;
        }

        std::shared_ptr<ftk::IObservable<std::shared_ptr<tl::Player> > > App::observePlayerA() const
        {
            return _p->playerA;
        }

        std::shared_ptr<ftk::IObservable<std::shared_ptr<tl::Player> > > App::observePlayerB() const
        {
            return _p->playerB;
        }

        void App::transportStop()
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                p.splitGlobalPlaybackActive = false;
                const auto apply = [&p](auto&& fn)
                {
                    const auto playerA = p.playerA->get();
                    const auto playerB = p.playerB->get();
                    if (playerA)
                    {
                        fn(playerA);
                    }
                    if (playerB && playerB != playerA)
                    {
                        fn(playerB);
                    }
                };
                apply([](const std::shared_ptr<tl::Player>& player) { player->stop(); });
                if (auto player = p.player->get())
                {
                    p.splitTransportTime = player->getCurrentTime();
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->stop();
                p.splitTransportTime = player->getCurrentTime();
            }
        }

        void App::transportForward()
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                const auto master = p.player->get();
                const OTIO_NS::RationalTime masterTime = getDisplayedOrCurrentTime(master);
                if (!masterTime.strictly_equal(tl::invalidTime))
                {
                    if (const auto playerA = p.playerA->get())
                    {
                        p.splitSyncOffsetSecondsA =
                            getTimeSeconds(getDisplayedOrCurrentTime(playerA)) -
                            getTimeSeconds(masterTime);
                    }
                    if (const auto playerB = p.playerB->get())
                    {
                        p.splitSyncOffsetSecondsB =
                            getTimeSeconds(getDisplayedOrCurrentTime(playerB)) -
                            getTimeSeconds(masterTime);
                    }
                    p.splitGlobalPlaybackActive = true;
                }
                const auto apply = [&p](auto&& fn)
                {
                    const auto playerA = p.playerA->get();
                    const auto playerB = p.playerB->get();
                    if (playerA)
                    {
                        fn(playerA);
                    }
                    if (playerB && playerB != playerA)
                    {
                        fn(playerB);
                    }
                };
                if (auto player = p.player->get())
                {
                    p.splitTransportTime = player->getCurrentTime();
                }
                apply([](const std::shared_ptr<tl::Player>& player) { player->forward(); });
                return;
            }
            if (auto player = p.player->get())
            {
                p.splitTransportTime = player->getCurrentTime();
                player->forward();
            }
        }

        void App::transportReverse()
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                const auto master = p.player->get();
                const OTIO_NS::RationalTime masterTime = getDisplayedOrCurrentTime(master);
                if (!masterTime.strictly_equal(tl::invalidTime))
                {
                    if (const auto playerA = p.playerA->get())
                    {
                        p.splitSyncOffsetSecondsA =
                            getTimeSeconds(getDisplayedOrCurrentTime(playerA)) -
                            getTimeSeconds(masterTime);
                    }
                    if (const auto playerB = p.playerB->get())
                    {
                        p.splitSyncOffsetSecondsB =
                            getTimeSeconds(getDisplayedOrCurrentTime(playerB)) -
                            getTimeSeconds(masterTime);
                    }
                    p.splitGlobalPlaybackActive = true;
                }
                const auto apply = [&p](auto&& fn)
                {
                    const auto playerA = p.playerA->get();
                    const auto playerB = p.playerB->get();
                    if (playerA)
                    {
                        fn(playerA);
                    }
                    if (playerB && playerB != playerA)
                    {
                        fn(playerB);
                    }
                };
                if (auto player = p.player->get())
                {
                    p.splitTransportTime = player->getCurrentTime();
                }
                apply([](const std::shared_ptr<tl::Player>& player) { reverseOrStepBackSafely(player, true); });
                return;
            }
            if (auto player = p.player->get())
            {
                p.splitTransportTime = player->getCurrentTime();
                reverseOrStepBackSafely(player, false);
            }
        }

        void App::transportTogglePlayback()
        {
            FTK_P();
            if (isDualDisplayMode(p.filesModel->getDisplayMode()))
            {
                const auto playerA = p.playerA->get();
                const auto playerB = p.playerB->get();
                const bool playing =
                    (playerA && !playerA->isStopped()) ||
                    (playerB && playerB != playerA && !playerB->isStopped());
                if (playing)
                {
                    transportStop();
                }
                else
                {
                    transportForward();
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->togglePlayback();
            }
        }

        void App::transportTimeAction(tl::TimeAction value)
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                transportStop();
                if (auto player = p.player->get())
                {
                    player->timeAction(value);
                    const OTIO_NS::RationalTime syncTime = player->getCurrentTime();
                    const auto playerA = p.playerA->get();
                    if (playerA)
                    {
                        playerA->seek(clampToTimeRange(syncTime, playerA->getTimeRange()));
                    }
                    if (auto playerB = p.playerB->get())
                    {
                        if (playerB != playerA)
                        {
                            playerB->seek(clampToTimeRange(syncTime, playerB->getTimeRange()));
                        }
                    }
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->timeAction(value);
            }
        }

        void App::transportSeek(const OTIO_NS::RationalTime& value)
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                p.splitGlobalPlaybackActive = false;
                transportStop();
                if (auto playerA = p.playerA->get())
                {
                    playerA->seek(clampToTimeRange(value, playerA->getTimeRange()));
                }
                if (auto playerB = p.playerB->get())
                {
                    if (playerB != p.playerA->get())
                    {
                        playerB->seek(clampToTimeRange(value, playerB->getTimeRange()));
                    }
                }
                if (auto player = p.player->get())
                {
                    p.splitTransportTime = player->getCurrentTime();
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->seek(value);
            }
        }

        void App::transportSetLoop(tl::Loop value)
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                const auto playerA = p.playerA->get();
                const auto playerB = p.playerB->get();
                if (playerA)
                {
                    playerA->setLoop(value);
                }
                if (playerB && playerB != playerA)
                {
                    playerB->setLoop(value);
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->setLoop(value);
            }
        }

        void App::transportSetSpeed(double value)
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                const auto playerA = p.playerA->get();
                const auto playerB = p.playerB->get();
                if (playerA)
                {
                    playerA->setSpeed(clampPlaybackSpeed(value, playerA->getDefaultSpeed()));
                }
                if (playerB && playerB != playerA)
                {
                    playerB->setSpeed(clampPlaybackSpeed(value, playerB->getDefaultSpeed()));
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->setSpeed(clampPlaybackSpeed(value, player->getDefaultSpeed()));
            }
        }

        void App::transportSetSpeedMult(double value)
        {
            FTK_P();
            const bool dualMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (dualMode)
            {
                const auto setSpeedMult = [value](const std::shared_ptr<tl::Player>& player)
                {
                    player->setSpeedMult(clampPlaybackSpeedMult(
                        value,
                        player->getSpeed(),
                        player->getDefaultSpeed()));
                };
                const auto playerA = p.playerA->get();
                const auto playerB = p.playerB->get();
                if (playerA)
                {
                    setSpeedMult(playerA);
                }
                if (playerB && playerB != playerA)
                {
                    setSpeedMult(playerB);
                }
                return;
            }
            if (auto player = p.player->get())
            {
                player->setSpeedMult(clampPlaybackSpeedMult(
                    value,
                    player->getSpeed(),
                    player->getDefaultSpeed()));
            }
        }

        void App::setSplitActivePane(SplitPane value)
        {
            FTK_P();
            p.splitActivePane = value;
        }

        void App::splitStop(SplitPane value)
        {
            FTK_P();
            setSplitActivePane(value);
            p.splitGlobalPlaybackActive = false;
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            if (player)
            {
                player->stop();
            }
        }

        void App::splitForward(SplitPane value)
        {
            FTK_P();
            setSplitActivePane(value);
            p.splitGlobalPlaybackActive = false;
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            if (player)
            {
                const auto item = SplitPane::B == value ? p.splitBItem : p.splitAItem;
                if (item && !item->forwardStartTime.strictly_equal(tl::invalidTime))
                {
                    player->seek(clampToTimeRange(item->forwardStartTime, player->getTimeRange()));
                }
                if (item)
                {
                    item->playback = tl::Playback::Forward;
                }
                player->forward();
            }
        }

        void App::splitReverse(SplitPane value)
        {
            FTK_P();
            setSplitActivePane(value);
            p.splitGlobalPlaybackActive = false;
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            if (player)
            {
                const auto item = SplitPane::B == value ? p.splitBItem : p.splitAItem;
                if (item && !item->reverseStartTime.strictly_equal(tl::invalidTime))
                {
                    player->seek(clampToTimeRange(item->reverseStartTime, player->getTimeRange()));
                }
                if (item)
                {
                    item->playback = !canReverseFromCurrentTime(player) ||
                        requiresSafeReverse(player, true) ?
                        tl::Playback::Stop :
                        tl::Playback::Reverse;
                }
                reverseOrStepBackSafely(player, true);
            }
        }

        void App::splitSeek(SplitPane value, const OTIO_NS::RationalTime& time)
        {
            FTK_P();
            setSplitActivePane(value);
            p.splitGlobalPlaybackActive = false;
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            if (player)
            {
                player->stop();
                player->seek(time);
            }
        }

        void App::splitSetSpeed(SplitPane value, double speed)
        {
            FTK_P();
            setSplitActivePane(value);
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            const auto item = SplitPane::B == value ? p.splitBItem : p.splitAItem;
            if (item)
            {
                item->speed = speed;
            }
            if (player)
            {
                player->setSpeed(clampPlaybackSpeed(speed, player->getDefaultSpeed()));
            }
        }

        void App::splitSetForwardStart(SplitPane value)
        {
            FTK_P();
            setSplitActivePane(value);
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            const auto item = SplitPane::B == value ? p.splitBItem : p.splitAItem;
            if (player && item)
            {
                item->forwardStartTime = clampToTimeRange(player->getCurrentTime(), player->getTimeRange());
            }
        }

        void App::splitSetReverseStart(SplitPane value)
        {
            FTK_P();
            setSplitActivePane(value);
            const auto player = SplitPane::B == value ? p.playerB->get() : p.playerA->get();
            const auto item = SplitPane::B == value ? p.splitBItem : p.splitAItem;
            if (player && item)
            {
                item->reverseStartTime = clampToTimeRange(player->getCurrentTime(), player->getTimeRange());
            }
        }

        const std::shared_ptr<MainWindow>& App::getMainWindow() const
        {
            return _p->mainWindow;
        }

        std::shared_ptr<ftk::IObservable<bool> > App::observeSecondaryWindow() const
        {
            return _p->secondaryWindowActive;
        }

        void App::setSecondaryWindow(bool value)
        {
            FTK_P();
            if (p.secondaryWindowActive->setIfChanged(value))
            {
                if (value)
                {
                    p.secondaryWindow = SecondaryWindow::create(
                        _context,
                        std::dynamic_pointer_cast<App>(shared_from_this()));
                    p.secondaryWindow->setCloseCallback(
                        [this]
                        {
                            FTK_P();
                            p.secondaryWindowActive->setIfChanged(false);
                            p.secondaryWindow.reset();
                        });
                    p.secondaryWindow->show();
                }
                else if (p.secondaryWindow)
                {
                    p.secondaryWindow->close();
                    p.secondaryWindow.reset();
                }
            }
        }

        bool App::hasPrintVersion() const
        {
            return _p->cmdLine.version->found();
        }

        void App::run()
        {
            FTK_P();

            p.fileLogSystem = ftk::FileLogSystem::create(_context, p.logFile);

            p.settings = ftk::Settings::create(
                _context,
                p.settingsFile,
                p.cmdLine.resetSettings->found());

            _modelsInit();
            _devicesInit();
            _observersInit();
            _inputFilesInit();
            _windowsInit();

            if (p.cmdLine.debugLoop->found() &&
                !p.cmdLine.inputs->getList().empty())
            {
                p.debugTimer = ftk::Timer::create(_context);
                p.debugTimer->setRepeating(true);
                p.debugTimer->start(
                    std::chrono::seconds(p.cmdLine.debugLoop->getValue()),
                    [this]
                    {
                        FTK_P();
                        if (!p.filesModel->getFiles().empty())
                        {
                            p.filesModel->closeAll();
                        }
                        else
                        {
                            ftk::Path path(p.cmdLine.inputs->getList()[p.debugInput]);
                            if (path.hasSeqWildcard())
                            {
                                path = ftk::expandSeq(path);
                            }
                            open(path);
                            if (auto player = p.player->get())
                            {
                                player->forward();
                            }
                            ++p.debugInput;
                            if (p.debugInput >= p.cmdLine.inputs->getList().size())
                            {
                                p.debugInput = 0;
                            }
                        }
                    });
            }

            ftk::App::run();
        }

        void App::_modelsInit()
        {
            FTK_P();

            p.settingsModel = models::SettingsModel::create(
                _context,
                p.settings,
                getDefaultDisplayScale());
            if (getColorStyleCmdLineOption()->found() ||
                getDisplayScaleCmdLineOption()->found())
            {
                // Override settings with the command line.
                auto style = p.settingsModel->getStyle();
                if (getColorStyleCmdLineOption()->found())
                {
                    style.colorStyle = getColorStyleCmdLineOption()->getValue();
                }
                if (getDisplayScaleCmdLineOption()->found())
                {
                    style.displayScale = getDisplayScaleCmdLineOption()->getValue();
                }
                p.settingsModel->setStyle(style);
            }
#if defined(TLRENDER_USD)
            if (p.cmdLine.usdRenderWidth->found() ||
                p.cmdLine.usdComplexity->found() ||
                p.cmdLine.usdDrawMode->found() ||
                p.cmdLine.usdEnableLighting->found() ||
                p.cmdLine.usdSRGB->found() ||
                p.cmdLine.usdStageCacheCount->found() ||
                p.cmdLine.usdDiskCacheGB->found())
            {
                tl::usd::Options options = p.settingsModel->getUSD();
                if (p.cmdLine.usdRenderWidth->found())
                {
                    options.renderWidth = p.cmdLine.usdRenderWidth->getValue();
                }
                if (p.cmdLine.usdComplexity->found())
                {
                    options.complexity = p.cmdLine.usdComplexity->getValue();
                }
                if (p.cmdLine.usdDrawMode->found())
                {
                    options.drawMode = p.cmdLine.usdDrawMode->getValue();
                }
                if (p.cmdLine.usdEnableLighting->found())
                {
                    options.enableLighting = p.cmdLine.usdEnableLighting->getValue();
                }
                if (p.cmdLine.usdSRGB->found())
                {
                        options.sRGB = p.cmdLine.usdSRGB->getValue();
                }
                if (p.cmdLine.usdStageCacheCount->found())
                {
                    options.stageCacheCount = std::max(0, p.cmdLine.usdStageCacheCount->getValue());
                }
                if (p.cmdLine.usdDiskCacheGB->found())
                {
                    options.diskCacheGB = std::max(0, p.cmdLine.usdDiskCacheGB->getValue());
                }
                p.settingsModel->setUSD(options);
            }
#endif // TLRENDER_USD

            p.sysLogModel = ftk::SysLogModel::create(_context);

            p.timeUnitsModel = models::TimeUnitsModel::create(_context, p.settings);
            
            p.filesModel = models::FilesModel::create(p.settings);

            p.recentFilesModel = models::RecentFilesModel::create(_context, p.settings);
            auto fileBrowserSystem = _context->getSystem<ftk::FileBrowserSystem>();
            fileBrowserSystem->getModel()->setExts(tl::getExts(_context));
            ftk::FileBrowserOptions fileBrowserOptions;
            fileBrowserOptions.dirList.seqExts = tl::getExts(_context, static_cast<int>(tl::FileType::Seq));
            fileBrowserSystem->getModel()->setOptions(fileBrowserOptions);
            fileBrowserSystem->setRecentFilesModel(p.recentFilesModel);

            p.colorModel = models::ColorModel::create(_context, p.settings);
            if (p.cmdLine.ocioFileName->found() ||
                p.cmdLine.ocioInput->found() ||
                p.cmdLine.ocioDisplay->found() ||
                p.cmdLine.ocioView->found() ||
                p.cmdLine.ocioLook->found())
            {
                tl::OCIOOptions options = p.colorModel->getOCIOOptions();
                options.enabled = true;
                if (p.cmdLine.ocioFileName->found())
                {
                    options.fileName = p.cmdLine.ocioFileName->getValue();
                }
                if (p.cmdLine.ocioInput->found())
                {
                    options.input = p.cmdLine.ocioInput->getValue();
                }
                if (p.cmdLine.ocioDisplay->found())
                {
                    options.display = p.cmdLine.ocioDisplay->getValue();
                }
                if (p.cmdLine.ocioView->found())
                {
                    options.view = p.cmdLine.ocioView->getValue();
                }
                if (p.cmdLine.ocioLook->found())
                {
                    options.look = p.cmdLine.ocioLook->getValue();
                }
                p.colorModel->setOCIOOptions(options);
            }
            if (p.cmdLine.lutFileName->found() ||
                p.cmdLine.lutOrder->found())
            {
                tl::LUTOptions options = p.colorModel->getLUTOptions();
                options.enabled = true;
                if (p.cmdLine.lutFileName->found())
                {
                    options.fileName = p.cmdLine.lutFileName->getValue();
                }
                if (p.cmdLine.lutOrder->found())
                {
                    options.order = p.cmdLine.lutOrder->getValue();
                }
                p.colorModel->setLUTOptions(options);
            }

            p.viewportModel = models::ViewportModel::create(_context, p.settings);

            p.audioModel = models::AudioModel::create(_context, p.settings);

            p.toolsModel = models::ToolsModel::create(p.settings);
        }

        void App::_devicesInit()
        {
            FTK_P();
#if defined(TLRENDER_BMD)
            p.bmdOutputDevice = tl::bmd::OutputDevice::create(_context);
            p.bmdDevicesModel = models::BMDDevicesModel::create(_context, p.settings);
#endif // TLRENDER_BMD
        }

        void App::_observersInit()
        {
            FTK_P();

            p.player = ftk::Observable<std::shared_ptr<tl::Player> >::create();
            p.playerA = ftk::Observable<std::shared_ptr<tl::Player> >::create();
            p.playerB = ftk::Observable<std::shared_ptr<tl::Player> >::create();

            p.cacheObserver = ftk::Observer<tl::PlayerCacheOptions>::create(
                p.settingsModel->observeCache(),
                [this](const tl::PlayerCacheOptions& value)
                {
                    const bool dualMode = isDualDisplayMode(_p->filesModel->getDisplayMode());
                    if (dualMode)
                    {
                        applyCacheBudget(_p->playerA->get(), value, false, true);
                        const auto playerA = _p->playerA->get();
                        const auto playerB = _p->playerB->get();
                        if (playerB && playerB != playerA)
                        {
                            applyCacheBudget(playerB, value, false, true);
                        }
                    }
                    else if (auto player = _p->player->get())
                    {
                        applyCacheBudget(player, value, true, false);
                    }
                });

            p.filesObserver = ftk::ListObserver<std::shared_ptr<models::FilesModelItem> >::create(
                p.filesModel->observeFiles(),
                [this](const std::vector<std::shared_ptr<models::FilesModelItem> >& value)
                {
                    _filesUpdate(value);
                });
            p.activeObserver = ftk::ListObserver<std::shared_ptr<models::FilesModelItem> >::create(
                p.filesModel->observeActive(),
                [this](const std::vector<std::shared_ptr<models::FilesModelItem> >& value)
                {
                    _activeUpdate(value);
                });
            p.layersObserver = ftk::ListObserver<int>::create(
                p.filesModel->observeLayers(),
                [this](const std::vector<int>& value)
                {
                    _layersUpdate(value);
                });
            p.compareTimeObserver = ftk::Observer<tl::CompareTime>::create(
                p.filesModel->observeCompareTime(),
                [this](tl::CompareTime value)
                {
                    if (auto player = _p->player->get())
                    {
                        player->setCompareTime(value);
                    }
                });
            p.displayModeObserver = ftk::Observer<models::DisplayMode>::create(
                p.filesModel->observeDisplayMode(),
                [this](models::DisplayMode)
                {
                    _activeUpdate(_p->filesModel->observeActive()->get());
                });
            p.dualPlaybackModeObserver = ftk::Observer<models::DualPlaybackMode>::create(
                p.filesModel->observeDualPlaybackMode(),
                [this](models::DualPlaybackMode)
                {
                    _activeUpdate(_p->filesModel->observeActive()->get());
                });

            p.masterPlayerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                p.player,
                [this](const std::shared_ptr<tl::Player>& player)
                {
                    FTK_P();
                    _p->masterPlaybackObserver.reset();
                    _p->masterCurrentTimeObserver.reset();
                    _p->masterCurrentVideoObserver.reset();
                    _p->masterInOutRangeObserver.reset();
                    if (!player)
                    {
                        return;
                    }

                    _p->masterPlaybackObserver = ftk::Observer<tl::Playback>::create(
                        player->observePlayback(),
                        [this](tl::Playback value)
                        {
                            FTK_P();
                            if (isDualDisplayMode(_p->filesModel->getDisplayMode()) ||
                                !isSplitCompareMode(_p->filesModel->getCompareOptions().compare) ||
                                !isSyncPlaybackMode(_p->filesModel->getDualPlaybackMode()))
                            {
                                return;
                            }
                            const auto source = _p->player->get();
                            const auto syncTargetPlayback = [source, value](const std::shared_ptr<tl::Player>& target)
                            {
                                if (!target || target == source)
                                {
                                    return;
                                }
                                if (tl::Playback::Stop == value)
                                {
                                    if (target->getPlayback() != tl::Playback::Stop)
                                    {
                                        target->stop();
                                    }
                                }
                                else if (target->getPlayback() != tl::Playback::Stop)
                                {
                                    // Followers in split sync mode are driven
                                    // by the master's transport time instead of
                                    // running their own clocks. Keeping them
                                    // stopped avoids audio-clock vs no-audio
                                    // clock drift, which previously made pane B
                                    // look faster than pane A.
                                    target->stop();
                                }
                            };
                            syncTargetPlayback(_p->playerA->get());
                            syncTargetPlayback(_p->playerB->get());
                        });

                    _p->masterCurrentTimeObserver = ftk::Observer<OTIO_NS::RationalTime>::create(
                        player->observeCurrentTime(),
                        [this](const OTIO_NS::RationalTime& value)
                        {
                            FTK_P();
                            const auto source = _p->player->get();
                            if (!source)
                            {
                                _p->splitTransportTime = tl::invalidTime;
                                return;
                            }
                            _p->splitTransportTime = value;
                            if (isDualDisplayMode(_p->filesModel->getDisplayMode()) ||
                                !isSplitCompareMode(_p->filesModel->getCompareOptions().compare) ||
                                !isSyncPlaybackMode(_p->filesModel->getDualPlaybackMode()))
                            {
                                return;
                            }
                            // While stopped, split sync only needs to update
                            // the shared bottom transport state. The actual
                            // follower seeks are driven by explicit global
                            // transport actions such as the bottom progress
                            // bar and frame-step buttons. Avoiding passive
                            // stop-state follower seeks keeps pane B from
                            // repeatedly clearing its first-frame requests
                            // during startup.
                        });

                    _p->masterCurrentVideoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                        player->observeCurrentVideo(),
                        [this](const std::vector<tl::VideoFrame>& value)
                        {
                            FTK_P();
                            const auto source = _p->player->get();
                            if (!source ||
                                value.empty() ||
                                value.front().time.strictly_equal(tl::invalidTime))
                            {
                                return;
                            }
                            const OTIO_NS::RationalTime displayedTime = value.front().time;
                            const tl::Playback sourcePlayback = source->getPlayback();
                            if (isDualDisplayMode(_p->filesModel->getDisplayMode()))
                            {
                                if (!_p->splitGlobalPlaybackActive ||
                                    tl::Playback::Stop == sourcePlayback)
                                {
                                    return;
                                }
                                const double liveSyncToleranceFrames = 30.0;
                                const auto syncDualTarget = [source, displayedTime, sourcePlayback, liveSyncToleranceFrames](
                                    const std::shared_ptr<tl::Player>& target,
                                    double offsetSeconds)
                                {
                                    if (!target || target == source)
                                    {
                                        return;
                                    }
                                    const OTIO_NS::RationalTime targetTime =
                                        addSecondsDeltaClamped(
                                            displayedTime,
                                            offsetSeconds,
                                            target->getTimeRange());
                                    if (targetTime.strictly_equal(tl::invalidTime))
                                    {
                                        return;
                                    }
                                    const OTIO_NS::RationalTime targetCurrentTime =
                                        getDisplayedOrCurrentTime(target);
                                    if (targetCurrentTime.strictly_equal(tl::invalidTime))
                                    {
                                        return;
                                    }
                                    const OTIO_NS::RationalTime targetCurrentAtRate =
                                        targetCurrentTime.rescaled_to(targetTime.rate());
                                    const double signedDiff =
                                        targetTime.value() - targetCurrentAtRate.value();
                                    if (std::abs(signedDiff) > liveSyncToleranceFrames)
                                    {
                                        // Only correct the follower. Pulling the
                                        // visible master back during normal 1x
                                        // playback clears frame requests in both
                                        // panes and presents as flicker.
                                        syncPlayingPlayerToTime(
                                            target,
                                            targetTime,
                                            sourcePlayback);
                                    }
                                };
                                syncDualTarget(
                                    _p->playerA->get(),
                                    _p->splitSyncOffsetSecondsA);
                                syncDualTarget(
                                    _p->playerB->get(),
                                    _p->splitSyncOffsetSecondsB);
                                return;
                            }
                            if (!isSplitCompareMode(_p->filesModel->getCompareOptions().compare) ||
                                !isSyncPlaybackMode(_p->filesModel->getDualPlaybackMode()))
                            {
                                return;
                            }
                            const auto syncTarget = [this, &source, &displayedTime, sourcePlayback](const std::shared_ptr<tl::Player>& target)
                            {
                                if (!target || target == source)
                                {
                                    return;
                                }
                                const OTIO_NS::RationalTime targetTime =
                                    clampToTimeRange(displayedTime, target->getTimeRange());
                                if (targetTime.strictly_equal(tl::invalidTime))
                                {
                                    return;
                                }
                                if (sourcePlayback == tl::Playback::Stop)
                                {
                                    if (getTimeDifferenceInFrames(targetTime, target->getCurrentTime()) > 0.0)
                                    {
                                        target->seek(targetTime);
                                    }
                                }
                                else
                                {
                                    // Drive followers from the master's actual
                                    // displayed frame time instead of every
                                    // transport tick. This avoids flooding the
                                    // follower with sub-frame sync updates,
                                    // which was delaying the first visible
                                    // frame in pane B and making the transport
                                    // buttons feel unresponsive.
                                    target->syncToTime(targetTime, sourcePlayback);
                                }
                            };
                            syncTarget(_p->playerA->get());
                            syncTarget(_p->playerB->get());
                        });

                    _p->masterInOutRangeObserver = ftk::Observer<OTIO_NS::TimeRange>::create(
                        player->observeInOutRange(),
                        [this](const OTIO_NS::TimeRange& value)
                        {
                            FTK_P();
                            if (isDualDisplayMode(_p->filesModel->getDisplayMode()) ||
                                !isSplitCompareMode(_p->filesModel->getCompareOptions().compare) ||
                                !isSyncPlaybackMode(_p->filesModel->getDualPlaybackMode()))
                            {
                                return;
                            }
                            const auto source = _p->player->get();
                            const auto syncTargetRange = [source, &value](const std::shared_ptr<tl::Player>& target)
                            {
                                if (!target || target == source)
                                {
                                    return;
                                }
                                const OTIO_NS::TimeRange targetRange = clampToTimeRange(
                                    value,
                                    target->getTimeRange());
                                if (!tl::compareExact(targetRange, tl::invalidTimeRange) &&
                                    !tl::compareExact(target->getInOutRange(), targetRange))
                                {
                                    target->setInOutRange(targetRange);
                                }
                            };
                            syncTargetRange(_p->playerA->get());
                            syncTargetRange(_p->playerB->get());
                        });
                });

            const auto syncSplitDisplayedTimes = []
            {
                // The global transport starts, stops, seeks, and speeds both
                // panes together. Continuous cross-pane correction during
                // normal playback repeatedly clears decoder requests, which is
                // visible as flashing in dual-video mode.
            };

            p.splitAPlayerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                p.playerA,
                [this, syncSplitDisplayedTimes](const std::shared_ptr<tl::Player>& player)
                {
                    FTK_P();
                    _p->splitACurrentVideoObserver.reset();
                    _p->splitDisplayedTimeA = tl::invalidTime;
                    if (player)
                    {
                        _p->splitACurrentVideoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                            player->observeCurrentVideo(),
                            [this, syncSplitDisplayedTimes](const std::vector<tl::VideoFrame>& value)
                            {
                                FTK_P();
                                if (!value.empty() &&
                                    !value.front().time.strictly_equal(tl::invalidTime))
                                {
                                    _p->splitDisplayedTimeA = value.front().time;
                                    syncSplitDisplayedTimes();
                                }
                            });
                    }
                });

            p.splitBPlayerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                p.playerB,
                [this, syncSplitDisplayedTimes](const std::shared_ptr<tl::Player>& player)
                {
                    FTK_P();
                    _p->splitBCurrentVideoObserver.reset();
                    _p->splitDisplayedTimeB = tl::invalidTime;
                    if (player)
                    {
                        _p->splitBCurrentVideoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                            player->observeCurrentVideo(),
                            [this, syncSplitDisplayedTimes](const std::vector<tl::VideoFrame>& value)
                            {
                                FTK_P();
                                if (!value.empty() &&
                                    !value.front().time.strictly_equal(tl::invalidTime))
                                {
                                    _p->splitDisplayedTimeB = value.front().time;
                                    syncSplitDisplayedTimes();
                                }
                            });
                    }
                });

            p.audioDeviceObserver = ftk::Observer<tl::AudioDeviceID>::create(
                p.audioModel->observeDevice(),
                [this](const tl::AudioDeviceID& value)
                {
                    if (auto player = _p->player->get())
                    {
                        player->setAudioDevice(value);
                    }
                });
            p.volumeObserver = ftk::Observer<float>::create(
                p.audioModel->observeVolume(),
                [this](float)
                {
                    _audioUpdate();
                });
            p.muteObserver = ftk::Observer<bool>::create(
                p.audioModel->observeMute(),
                [this](bool)
                {
                    _audioUpdate();
                });
            p.channelMuteObserver = ftk::ListObserver<bool>::create(
                p.audioModel->observeChannelMute(),
                [this](const std::vector<bool>&)
                {
                    _audioUpdate();
                });
            p.syncOffsetObserver = ftk::Observer<double>::create(
                p.audioModel->observeSyncOffset(),
                [this](double)
                {
                    _audioUpdate();
                });

            p.styleSettingsObserver = ftk::Observer<models::StyleSettings>::create(
                p.settingsModel->observeStyle(),
                [this](const models::StyleSettings& value)
                {
                    auto fontSystem = getFontSystem();
                    const auto& fonts = fontSystem->getFonts();
                    for (const auto& font : value.fontFiles)
                    {
                        if (!font.empty())
                        {
                            ftk::Path path(font);
                            const std::string fontName = path.getBase() + path.getNum();
                            const auto i = std::find(fonts.begin(), fonts.end(), fontName);
                            if (i == fonts.end())
                            {
                                fontSystem->addFont(fontName, font);
                            }
                        }
                    }
                    std::map<ftk::FontType, std::string> fontsMap = value.fonts;
#if defined(__APPLE__)
                    const std::string cjkFontName = "Hiragino Sans GB";
                    const std::string cjkFontFile = "/System/Library/Fonts/Hiragino Sans GB.ttc";
                    if (std::find(fonts.begin(), fonts.end(), cjkFontName) == fonts.end())
                    {
                        fontSystem->addFont(cjkFontName, cjkFontFile);
                    }
                    if (fontsMap[ftk::FontType::Regular] == ftk::getDefaultFont(ftk::FontType::Regular))
                    {
                        fontsMap[ftk::FontType::Regular] = cjkFontName;
                    }
                    if (fontsMap[ftk::FontType::Bold] == ftk::getDefaultFont(ftk::FontType::Bold))
                    {
                        fontsMap[ftk::FontType::Bold] = cjkFontName;
                    }
#endif // __APPLE__
                    auto style = getStyle();
                    style->setColorControls(value.colorControls);
                    style->setFonts(fontsMap);
                    setColorStyle(value.colorStyle);
                    setCustomColorRoles(value.customColorRoles);
                    setDisplayScale(value.displayScale);
                });

            p.miscSettingsObserver = ftk::Observer<models::MiscSettings>::create(
                p.settingsModel->observeMisc(),
                [this](const models::MiscSettings& value)
                {
                    setTooltipsEnabled(value.tooltipsEnabled);
                });

#if defined(TLRENDER_BMD)
            p.bmdDevicesObserver = ftk::Observer<tl::bmd::DevicesModelData>::create(
                p.bmdDevicesModel->observeData(),
                [this](const tl::bmd::DevicesModelData& value)
                {
                    FTK_P();
                    tl::bmd::DeviceConfig config;
                    config.deviceIndex = value.deviceIndex - 1;
                    config.displayModeIndex = value.displayModeIndex - 1;
                    config.pixelType = value.pixelTypeIndex >= 0 &&
                        value.pixelTypeIndex < value.pixelTypes.size() ?
                        value.pixelTypes[value.pixelTypeIndex] :
                        tl::bmd::PixelType::None;
                    config.boolOptions = value.boolOptions;
                    p.bmdOutputDevice->setConfig(config);
                    p.bmdOutputDevice->setEnabled(value.deviceEnabled);
                    tl::DisplayOptions displayOptions = p.viewportModel->getDisplayOptions();
                    p.bmdOutputDevice->setDisplayOptions({ displayOptions });
                    p.bmdOutputDevice->setHDR(value.hdrMode, value.hdrData);
                });
            p.bmdActiveObserver = ftk::Observer<bool>::create(
                p.bmdOutputDevice->observeActive(),
                [this](bool value)
                {
                    _p->bmdDeviceActive = value;
                    _audioUpdate();
                });
            p.bmdSizeObserver = ftk::Observer<ftk::Size2I>::create(
                p.bmdOutputDevice->observeSize(),
                [this](const ftk::Size2I& value)
                {
                    //std::cout << "output device size: " << value << std::endl;
                });
            p.bmdFrameRateObserver = ftk::Observer<tl::bmd::FrameRate>::create(
                p.bmdOutputDevice->observeFrameRate(),
                [this](const tl::bmd::FrameRate& value)
                {
                    //std::cout << "output device frame rate: " <<
                    //    value.num << "/" <<
                    //    value.den <<
                    //    std::endl;
                });

            p.ocioOptionsObserver = ftk::Observer<tl::OCIOOptions>::create(
                p.colorModel->observeOCIOOptions(),
                [this](const tl::OCIOOptions& value)
                {
                    _p->bmdOutputDevice->setOCIOOptions(value);
                });
            p.lutOptionsObserver = ftk::Observer<tl::LUTOptions>::create(
                p.colorModel->observeLUTOptions(),
                [this](const tl::LUTOptions& value)
                {
                    _p->bmdOutputDevice->setLUTOptions(value);
                });
            p.imageOptionsObserver = ftk::Observer<ftk::ImageOptions>::create(
                p.viewportModel->observeImageOptions(),
                [this](const ftk::ImageOptions& value)
                {
                    _p->bmdOutputDevice->setImageOptions({ value });
                });
            p.displayOptionsObserver = ftk::Observer<tl::DisplayOptions>::create(
                p.viewportModel->observeDisplayOptions(),
                [this](const tl::DisplayOptions& value)
                {
                    _p->bmdOutputDevice->setDisplayOptions({ value });
                });

            p.compareOptionsObserver = ftk::Observer<tl::CompareOptions>::create(
                p.filesModel->observeCompareOptions(),
                [this](const tl::CompareOptions& value)
                {
                    _p->bmdOutputDevice->setCompareOptions(value);
                });

            p.bgOptionsObserver = ftk::Observer<tl::BackgroundOptions>::create(
                p.viewportModel->observeBackgroundOptions(),
                [this](const tl::BackgroundOptions& value)
                {
                    _p->bmdOutputDevice->setBackgroundOptions(value);
                });

            p.fgOptionsObserver = ftk::Observer<tl::ForegroundOptions>::create(
                p.viewportModel->observeForegroundOptions(),
                [this](const tl::ForegroundOptions& value)
                {
                    _p->bmdOutputDevice->setForegroundOptions(value);
                });
#endif // TLRENDER_BMD
        }

        void App::_inputFilesInit()
        {
            FTK_P();
            auto applyCompareOptions = [this]
            {
                FTK_P();
                if (p.cmdLine.compare->found() ||
                    p.cmdLine.wipeCenter->found() ||
                    p.cmdLine.wipeRotation->found())
                {
                    auto options = p.filesModel->getCompareOptions();
                    if (p.cmdLine.compare->found())
                    {
                        options.compare = p.cmdLine.compare->getValue();
                    }
                    if (p.cmdLine.wipeCenter->found())
                    {
                        options.wipeCenter = p.cmdLine.wipeCenter->getValue();
                    }
                    if (p.cmdLine.wipeRotation->found())
                    {
                        options.wipeRotation = p.cmdLine.wipeRotation->getValue();
                    }
                    p.filesModel->setCompareOptions(options);
                }
            };

            applyCompareOptions();
            if (!p.cmdLine.inputs->getList().empty())
            {
                ftk::PathOptions pathOptions;
                pathOptions.seqMaxDigits = p.settingsModel->getImageSeq().maxDigits;

                if (p.cmdLine.compareFileName->found())
                {
                    ftk::Path path(p.cmdLine.compareFileName->getValue());
                    if (path.hasSeqWildcard())
                    {
                        path = ftk::expandSeq(path, pathOptions);
                    }
                    open(path);
                    tl::CompareOptions options;
                    if (p.cmdLine.compare->found())
                    {
                        options.compare = p.cmdLine.compare->getValue();
                    }
                    if (p.cmdLine.wipeCenter->found())
                    {
                        options.wipeCenter = p.cmdLine.wipeCenter->getValue();
                    }
                    if (p.cmdLine.wipeRotation->found())
                    {
                        options.wipeRotation = p.cmdLine.wipeRotation->getValue();
                    }
                    p.filesModel->setCompareOptions(options);
                    p.filesModel->setB(0, true);
                }

                std::string audioFileName;
                if (p.cmdLine.audioFileName->found())
                {
                    audioFileName = p.cmdLine.audioFileName->getValue();
                }

                for (const auto& input : p.cmdLine.inputs->getList())
                {
                    ftk::Path path(input);
                    if (path.hasSeqWildcard())
                    {
                        path = ftk::expandSeq(path, pathOptions);
                    }
                    open(path, ftk::Path(audioFileName));

                    if (auto player = p.player->get())
                    {
                        if (p.cmdLine.speed->found())
                        {
                            player->setSpeed(p.cmdLine.speed->getValue());
                        }
                        if (p.cmdLine.timeUnits->found())
                        {
                            p.timeUnitsModel->setTimeUnits(p.cmdLine.timeUnits->getValue());
                        }
                        const double speed = player->getSpeed();
                        const tl::TimeUnits timeUnits = p.timeUnitsModel->getTimeUnits();
                        if (p.cmdLine.inPoint->found())
                        {
                            const auto inOutRange = OTIO_NS::TimeRange::range_from_start_end_time_inclusive(
                                tl::textToTime(
                                    p.cmdLine.inPoint->getValue(),
                                    speed,
                                    timeUnits),
                                player->getInOutRange().end_time_inclusive());
                            player->setInOutRange(inOutRange);
                            player->seek(inOutRange.start_time());
                        }
                        if (p.cmdLine.outPoint->found())
                        {
                            const auto inOutRange = OTIO_NS::TimeRange::range_from_start_end_time_inclusive(
                                player->getInOutRange().start_time(),
                                tl::textToTime(
                                    p.cmdLine.outPoint->getValue(),
                                    speed,
                                    timeUnits));
                            player->setInOutRange(inOutRange);
                            player->seek(inOutRange.start_time());
                        }
                        if (p.cmdLine.seek->found())
                        {
                            player->seek(tl::textToTime(
                                p.cmdLine.seek->getValue(),
                                speed,
                                timeUnits));
                        }
                        if (p.cmdLine.loop->found())
                        {
                            player->setLoop(p.cmdLine.loop->getValue());
                        }
                        if (p.cmdLine.playback->found())
                        {
                            player->setPlayback(p.cmdLine.playback->getValue());
                        }
                    }
                }

                applyCompareOptions();
            }
        }

        void App::_windowsInit()
        {
            FTK_P();

            p.secondaryWindowActive = ftk::Observable<bool>::create(false);

            p.mainWindow = MainWindow::create(
                _context,
                std::dynamic_pointer_cast<App>(shared_from_this()));

            p.viewPosZoomObserver = ftk::Observer<std::pair<ftk::V2I, double> >::create(
                p.mainWindow->getViewport()->observeViewPosAndZoom(),
                [this](const std::pair<ftk::V2I, double>& value)
                {
                    _viewUpdate(
                        value.first,
                        value.second,
                        _p->mainWindow->getViewport()->hasFrameView());
                });
            p.viewFramedObserver = ftk::Observer<bool>::create(
                p.mainWindow->getViewport()->observeFramed(),
                [this](bool value)
                {
                    _viewUpdate(
                        _p->mainWindow->getViewport()->getViewPos(),
                        _p->mainWindow->getViewport()->getZoom(),
                        value);
                });
            p.mainWindow->setCloseCallback(
                [this]
                {
                    FTK_P();
                    if (p.secondaryWindow)
                    {
                        p.secondaryWindow->close();
                        p.secondaryWindow.reset();
                    }
                });

            p.startupScaleTimer = ftk::Timer::create(_context);
            p.startupScaleTimer->setRepeating(true);
            p.startupScaleTimer->start(
                std::chrono::milliseconds(50),
                [this]
                {
                    FTK_P();
                    if (!p.mainWindow)
                    {
                        if (p.startupScaleTimer)
                        {
                            p.startupScaleTimer->stop();
                            p.startupScaleTimer.reset();
                        }
                        return;
                    }
                    const float contentScale = p.mainWindow->getContentScale();
                    const float displayScale = p.mainWindow->getDisplayScale();
                    if (contentScale > 1.F && displayScale + .01F < contentScale)
                    {
                        setDisplayScale(contentScale);
                    }
                    if (contentScale > 0.F &&
                        std::fabs(p.mainWindow->getDisplayScale() - contentScale) <= .01F)
                    {
                        p.startupScaleTimer->stop();
                        p.startupScaleTimer.reset();
                    }
                });

            p.startupLayoutTimer = ftk::Timer::create(_context);
            p.startupLayoutTimer->setRepeating(true);
            p.startupLayoutTimer->start(
                std::chrono::milliseconds(50),
                [this, startupLayoutPasses = size_t(0)]() mutable
                {
                    FTK_P();
                    if (!p.mainWindow)
                    {
                        if (p.startupLayoutTimer)
                        {
                            p.startupLayoutTimer->stop();
                            p.startupLayoutTimer.reset();
                        }
                        return;
                    }

                    const ftk::Size2I bufferSize = p.mainWindow->getBufferSize();
                    if (bufferSize.isValid())
                    {
                        p.mainWindow->setSizeUpdate();
                        p.mainWindow->setGeometry(ftk::Box2I(ftk::V2I(), bufferSize));
                        p.mainWindow->setDrawUpdate();
                        ++startupLayoutPasses;
                    }

                    if (startupLayoutPasses >= 8)
                    {
                        p.startupLayoutTimer->stop();
                        p.startupLayoutTimer.reset();
                    }
                });
        }


        std::filesystem::path App::_appDocsPath()
        {
            FTK_P();
            const std::filesystem::path documentsPath = ftk::getUserPath(ftk::UserPath::Documents);
            if (!std::filesystem::exists(documentsPath))
            {
                std::filesystem::create_directory(documentsPath);
            }
            const std::filesystem::path out = documentsPath / p.appInfoModel->getFullName();
            if (!std::filesystem::exists(out))
            {
                std::filesystem::create_directory(out);
            }
            return out;
        }

        std::filesystem::path App::_getLogFilePath()
        {
            FTK_P();
            return _appDocsPath() / ftk::Format("{0}.{1}.log").
                arg(p.appInfoModel->getShortName()).
                arg(p.appInfoModel->getVersionMajor()).
                str();
        }

        std::filesystem::path App::_getSettingsPath()
        {
            FTK_P();
            return _appDocsPath() / ftk::Format("{0}.{1}.json").
                arg(p.appInfoModel->getShortName()).
                arg(p.appInfoModel->getVersionMajor()).
                str();
        }

        void App::_filesUpdate(const std::vector<std::shared_ptr<models::FilesModelItem> >& files)
        {
            FTK_P();

            std::vector<std::shared_ptr<tl::Timeline> > timelines(files.size());
            for (size_t i = 0; i < files.size(); ++i)
            {
                const auto j = std::find(p.files.begin(), p.files.end(), files[i]);
                if (j != p.files.end())
                {
                    timelines[i] = p.timelines[j - p.files.begin()];
                }
            }

            for (size_t i = 0; i < files.size(); ++i)
            {
                if (!timelines[i])
                {
                    try
                    {
                        tl::Options options;
                        const models::ImageSeqSettings imageSeq = p.settingsModel->getImageSeq();
                        options.imageSeqAudio = imageSeq.audio;
                        options.imageSeqAudioExts = imageSeq.audioExts;
                        options.imageSeqAudioFileName = imageSeq.audioFileName;
                        const models::AdvancedSettings advanced = p.settingsModel->getAdvanced();
                        options.compat = advanced.compat;
                        options.videoRequestMax = getRecommendedVideoRequestMax();
                        options.audioRequestMax = 0;
                        options.ioOptions = p.settingsModel->getIOOptions();
                        options.ioOptions["FFmpeg/HardwareDecode"] = "Auto";
                        options.pathOptions.seqMaxDigits = imageSeq.maxDigits;
                        auto otioTimeline = files[i]->audioPath.isEmpty() ?
                            tl::create(_context, files[i]->path, options) :
                            tl::create(_context, files[i]->path, files[i]->audioPath, options);
                        timelines[i] = tl::Timeline::create(_context, otioTimeline, options);
                        for (const auto& video : timelines[i]->getIOInfo().video)
                        {
                            files[i]->videoLayers.push_back(video.name);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                    }
                }
            }

            p.files = files;
            p.timelines = timelines;
        }

        void App::_activeUpdate(const std::vector<std::shared_ptr<models::FilesModelItem> >& activeFiles)
        {
            FTK_P();

            const bool splitMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (!p.activeFiles.empty())
            {
                if (splitMode)
                {
                    capturePlayerState(p.playerA->get(), p.splitAItem);
                    capturePlayerState(p.playerB->get(), p.splitBItem);
                }
                else if (auto player = p.player->get())
                {
                    capturePlayerState(player, p.activeFiles.front());
                }
            }

            std::shared_ptr<tl::Player> player;
            std::shared_ptr<tl::Player> playerA;
            std::shared_ptr<tl::Player> playerB;
            std::shared_ptr<models::FilesModelItem> splitMasterItem;
            size_t splitMasterIndex = 0;
            if (!activeFiles.empty())
            {
                auto getTimeline = [&p](const std::shared_ptr<models::FilesModelItem>& item)
                {
                    auto i = std::find(p.files.begin(), p.files.end(), item);
                    return i != p.files.end() ? p.timelines[i - p.files.begin()] : nullptr;
                };
                auto applyItemState = [](
                    const std::shared_ptr<tl::Player>& player,
                    const std::shared_ptr<models::FilesModelItem>& item)
                {
                    if (!player || !item)
                    {
                        return;
                    }
                    if (item->speed >= 0.0)
                    {
                        player->setSpeed(clampPlaybackSpeed(item->speed, player->getDefaultSpeed()));
                    }
                    if (!tl::compareExact(item->inOutRange, tl::invalidTimeRange))
                    {
                        player->setInOutRange(item->inOutRange);
                    }
                    if (!item->currentTime.strictly_equal(tl::invalidTime))
                    {
                        player->seek(item->currentTime);
                    }
                };

                const auto splitAItem = splitMode ? p.filesModel->getA() : nullptr;
                const auto& bItems = p.filesModel->getB();
                const auto splitBItem = splitMode && !bItems.empty() ? bItems.front() : nullptr;
                const auto timelineA = getTimeline(splitMode ? splitAItem : activeFiles[0]);
                const auto timelineB = getTimeline(splitBItem);
                if (splitMode)
                {
                    try
                    {
                        // The bottom-most timeline and transport are always the
                        // dual-view global controller. It uses the longer
                        // timeline as the absolute time base; the shorter pane
                        // clamps to its boundary when the global position is
                        // outside its range.
                        if (splitBItem &&
                            (!splitAItem ||
                                getTimelineDurationSeconds(timelineB) > getTimelineDurationSeconds(timelineA)))
                        {
                            splitMasterIndex = 1;
                        }
                        splitMasterItem = splitMasterIndex == 0 ? splitAItem : splitBItem;
                        if (splitAItem)
                        {
                            if (p.splitAItem && splitAItem == p.splitAItem)
                            {
                                playerA = p.playerA->get();
                            }
                            if (!playerA)
                            {
                                playerA = createPlayer(
                                    _context,
                                    timelineA,
                                    p.settingsModel,
                                    p.audioModel,
                                    false,
                                    true);
                            }
                            if (playerA)
                            {
                                playerA->setCompare({});
                                applyItemState(playerA, splitAItem);
                            }
                        }
                        if (splitBItem)
                        {
                            if (p.splitBItem && splitBItem == p.splitBItem)
                            {
                                playerB = p.playerB->get();
                            }
                            if (!playerB)
                            {
                                playerB = createPlayer(
                                    _context,
                                    timelineB,
                                    p.settingsModel,
                                    p.audioModel,
                                    false,
                                    true);
                            }
                            if (playerB)
                            {
                                playerB->setCompare({});
                                applyItemState(playerB, splitBItem);
                            }
                        }
                        player = splitMasterIndex == 0 ? playerA : playerB;
                        if (player)
                        {
                            p.splitTransportTime = player->getCurrentTime();
                        }
                    }
                    catch (const std::exception& e)
                    {
                        _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                    }
                }
                else
                {
                    if (!p.activeFiles.empty() && activeFiles[0] == p.activeFiles[0])
                    {
                        player = p.player->get();
                    }
                    else
                    {
                        try
                        {
                            player = createPlayer(
                                _context,
                                timelineA,
                                p.settingsModel,
                                p.audioModel,
                                false);
                        }
                        catch (const std::exception& e)
                        {
                            _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                        }
                    }
                    if (player)
                    {
                        applyItemState(player, activeFiles.front());
                        std::vector<std::shared_ptr<tl::Timeline> > compare;
                        for (size_t i = 1; i < activeFiles.size(); ++i)
                        {
                            if (auto timeline = getTimeline(activeFiles[i]))
                            {
                                compare.push_back(timeline);
                            }
                        }
                        player->setCompare(compare);
                        player->setCompareTime(p.filesModel->getCompareTime());
                    }
                }
            }

            const bool startPlayback =
                player &&
                p.settingsModel->getPlayback().startPlayback &&
                ((splitMode && splitMasterItem && splitMasterItem->newFile) ||
                    (!splitMode && !activeFiles.empty() && activeFiles.front()->newFile));

            for (auto& file : p.files)
            {
                file->newFile = false;
            }

            p.activeFiles = activeFiles;
            p.splitAItem = splitMode ? p.filesModel->getA() : nullptr;
            p.splitBItem = splitMode && !p.filesModel->getB().empty() ? p.filesModel->getB().front() : nullptr;
            p.splitMasterItem = splitMode && player ? splitMasterItem : nullptr;
            p.splitMasterIndex = splitMode && player ? splitMasterIndex : 0;
            if (splitMode)
            {
                p.splitActivePane = p.splitMasterIndex == 1 ? SplitPane::B : SplitPane::A;
            }
            p.splitTransportTime =
                splitMode && player ?
                player->getCurrentTime() :
                tl::invalidTime;
            p.playerA->setIfChanged(playerA);
            p.playerB->setIfChanged(playerB);
            p.player->setIfChanged(player);
            if (auto thumbnailSystem = _context->getSystem<tl::ui::ThumbnailSystem>())
            {
                // Filmstrip decode uses ThumbnailSystem (software, queued) and does
                // not share the player predecode/cache pool. Keep requests enabled
                // so pane timelines can populate without disabling playback caches.
                thumbnailSystem->setRequestsEnabled(true);
                tl::ui::ThumbnailCacheOptions cacheOptions =
                    p.settingsModel->getThumbnailCache();
                if (splitMode)
                {
                    cacheOptions.thumbnailMB = std::min(
                        std::max(cacheOptions.thumbnailMB, 48.F),
                        64.F);
                    cacheOptions.waveformMB = std::min(cacheOptions.waveformMB, 8.F);
                }
                thumbnailSystem->setCacheOptions(cacheOptions);
            }
#if defined(TLRENDER_BMD)
            p.bmdOutputDevice->setPlayer(player);
#endif // TLRENDER_BMD

            _layersUpdate(p.filesModel->observeLayers()->get());
            _audioUpdate();
            if (startPlayback)
            {
                if (splitMode)
                {
                    transportForward();
                }
                else if (player)
                {
                    player->forward();
                }
            }
        }

        void App::_layersUpdate(const std::vector<int>& value)
        {
            FTK_P();
            const bool splitMode = isDualDisplayMode(p.filesModel->getDisplayMode());
            if (splitMode)
            {
                int videoLayerA = 0;
                int videoLayerB = 0;
                if (!value.empty() && value.size() == p.files.size())
                {
                    if (p.splitAItem)
                    {
                        auto i = std::find(p.files.begin(), p.files.end(), p.splitAItem);
                        if (i != p.files.end())
                        {
                            videoLayerA = value[i - p.files.begin()];
                        }
                    }
                    if (p.splitBItem)
                    {
                        auto i = std::find(p.files.begin(), p.files.end(), p.splitBItem);
                        if (i != p.files.end())
                        {
                            videoLayerB = value[i - p.files.begin()];
                        }
                    }
                }
                if (auto player = p.player->get())
                {
                    player->setVideoLayer(p.splitMasterIndex == 1 ? videoLayerB : videoLayerA);
                    player->setCompareVideoLayers({});
                }
                if (auto player = p.playerA->get())
                {
                    player->setVideoLayer(videoLayerA);
                }
                if (auto player = p.playerB->get())
                {
                    player->setVideoLayer(videoLayerB);
                }
            }
            else if (auto player = p.player->get())
            {
                int videoLayer = 0;
                std::vector<int> compareVideoLayers;
                if (!value.empty() && value.size() == p.files.size() && !p.activeFiles.empty())
                {
                    auto i = std::find(p.files.begin(), p.files.end(), p.activeFiles.front());
                    if (i != p.files.end())
                    {
                        videoLayer = value[i - p.files.begin()];
                    }
                    for (size_t j = 1; j < p.activeFiles.size(); ++j)
                    {
                        i = std::find(p.files.begin(), p.files.end(), p.activeFiles[j]);
                        if (i != p.files.end())
                        {
                            compareVideoLayers.push_back(value[i - p.files.begin()]);
                        }
                    }
                }
                player->setVideoLayer(videoLayer);
                player->setCompareVideoLayers(compareVideoLayers);
            }
        }

        void App::_viewUpdate(const ftk::V2I& pos, double zoom, bool frame)
        {
            FTK_P();
            const ftk::Box2I& g = p.mainWindow->getViewport()->getGeometry();
            float scale = 1.F;
            if (p.secondaryWindow)
            {
                const ftk::Size2I& secondarySize = p.secondaryWindow->getViewport()->getGeometry().size();
                if (g.isValid() && secondarySize.isValid())
                {
                    scale = secondarySize.w / static_cast<float>(g.w());
                }
                p.secondaryWindow->setView(pos * scale, zoom * scale, frame);
            }
#if defined(TLRENDER_BMD)
            scale = 1.F;
            const ftk::Size2I& bmdSize = p.bmdOutputDevice->getSize();
            if (g.isValid() && bmdSize.isValid())
            {
                scale = bmdSize.w / static_cast<float>(g.w());
            }
            p.bmdOutputDevice->setView(pos * scale, zoom * scale, frame);
#endif // TLRENDER_BMD
        }

        void App::_audioUpdate()
        {
            FTK_P();
            const float volume = p.audioModel->getVolume();
            const bool mute = p.audioModel->isMuted();
            const std::vector<bool> channelMute = p.audioModel->getChannelMute();
            const double audioOffset = p.audioModel->getSyncOffset();
            if (auto player = p.player->get())
            {
                player->setAudioDevice(tl::AudioDeviceID());
                player->setVolume(0.F);
                player->setMute(true);
                player->setChannelMute(channelMute);
                player->setAudioOffset(audioOffset);
            }
            if (auto player = p.playerA->get())
            {
                if (player != p.player->get())
                {
                    player->setAudioDevice(tl::AudioDeviceID());
                    player->setVolume(0.F);
                    player->setMute(true);
                    player->setChannelMute(channelMute);
                    player->setAudioOffset(audioOffset);
                }
            }
            if (auto player = p.playerB->get())
            {
                if (player != p.player->get())
                {
                    player->setAudioDevice(tl::AudioDeviceID());
                    player->setVolume(0.F);
                    player->setMute(true);
                    player->setChannelMute(channelMute);
                    player->setAudioOffset(audioOffset);
                }
            }
#if defined(TLRENDER_BMD)
            p.bmdOutputDevice->setVolume(volume);
            p.bmdOutputDevice->setMute(mute);
            p.bmdOutputDevice->setChannelMute(channelMute);
            p.bmdOutputDevice->setAudioOffset(audioOffset);
#endif // TLRENDER_BMD
        }
    }
}

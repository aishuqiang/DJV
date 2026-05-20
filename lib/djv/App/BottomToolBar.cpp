// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/BottomToolBar.h>

#include <djv/App/App.h>
#include <djv/App/AudioActions.h>
#include <djv/App/FrameActions.h>
#include <djv/App/PlaybackActions.h>
#include <djv/UI/AudioPopup.h>
#include <djv/UI/SpeedPopup.h>
#include <djv/Models/AudioModel.h>
#include <djv/Models/TimeUnitsModel.h>

#include <tlRender/UI/PlaybackLoopWidget.h>
#include <tlRender/UI/TimeEdit.h>
#include <tlRender/UI/TimeLabel.h>
#include <tlRender/UI/TimeUnitsWidget.h>
#include <tlRender/Timeline/Player.h>

#include <ftk/UI/DoubleModel.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/ShuttleWidget.h>
#include <ftk/UI/Spacer.h>
#include <ftk/UI/ToolButton.h>
#include <ftk/Core/Format.h>

#include <cmath>

namespace djv
{
    namespace app
    {
        namespace
        {
            const double minPlaybackSpeedMult = 1.0 / 3.0;
            const double maxPlaybackSpeedMult = 3.0;
        }

        struct BottomToolBar::Private
        {
            std::weak_ptr<App> app;
            std::shared_ptr<tl::Player> player;
            std::shared_ptr<ftk::DoubleModel> speedModel;
            OTIO_NS::RationalTime startTime = tl::invalidTime;

            std::map<std::string, std::shared_ptr<ftk::ToolButton> > buttons;
            std::shared_ptr<tl::ui::PlaybackLoopWidget> loopWidget;
            std::shared_ptr<ftk::ShuttleWidget> playbackShuttle;
            std::shared_ptr<ftk::ShuttleWidget> frameShuttle;
            std::shared_ptr<tl::ui::TimeEdit> currentTimeEdit;
            std::shared_ptr<tl::ui::TimeLabel> durationLabel;
            std::shared_ptr<tl::ui::TimeUnitsWidget> timeUnitsWidget;
            std::shared_ptr<ftk::ToolButton> speedButton;
            std::shared_ptr<ui::SpeedPopup> speedPopup;
            std::shared_ptr<ftk::Label> audioLabel;
            std::shared_ptr<ftk::ToolButton> audioButton;
            std::shared_ptr<ui::AudioPopup> audioPopup;
            std::shared_ptr<ftk::ToolButton> muteButton;
            std::shared_ptr<ftk::HorizontalLayout> layout;

            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > playerObserver;
            std::shared_ptr<ftk::Observer<double> > speedObserver;
            std::shared_ptr<ftk::Observer<double> > actualSpeedObserver;
            std::shared_ptr<ftk::Observer<double> > speedObserver2;
            std::shared_ptr<ftk::Observer<tl::Loop> > loopObserver;
            std::shared_ptr<ftk::Observer<tl::Playback> > playbackObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > currentTimeObserver;
            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > currentVideoObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::TimeRange> > inOutRangeObserver;
            std::shared_ptr<ftk::Observer<float> > volumeObserver;
        };

        void BottomToolBar::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<PlaybackActions>& playbackActions,
            const std::shared_ptr<FrameActions>& frameActions,
            const std::shared_ptr<AudioActions>& audioActions,
            const std::shared_ptr<IWidget>& parent)
        {
            IWidget::_init(
                context,
                "djv::app::BottomToolBar",
                parent);
            FTK_P();

            p.app = app;

            p.speedModel = ftk::DoubleModel::create();
            p.speedModel->setRange(ftk::RangeD(minPlaybackSpeedMult, maxPlaybackSpeedMult));
            p.speedModel->setStep(1.F);
            p.speedModel->setLargeStep(1.F);

            auto actions = playbackActions->getActions();
            p.buttons["Stop"] = ftk::ToolButton::create(context, actions["Stop"]);
            p.buttons["Forward"] = ftk::ToolButton::create(context, actions["Forward"]);
            p.buttons["Reverse"] = ftk::ToolButton::create(context, actions["Reverse"]);

            p.loopWidget = tl::ui::PlaybackLoopWidget::create(context);

            p.playbackShuttle = ftk::ShuttleWidget::create(context);
            p.playbackShuttle->setTooltip("播放速率拖动控制。");

            actions = frameActions->getActions();
            p.buttons["Start"] = ftk::ToolButton::create(context, actions["Start"]);
            p.buttons["End"] = ftk::ToolButton::create(context, actions["End"]);
            p.buttons["Prev"] = ftk::ToolButton::create(context, actions["Prev"]);
            p.buttons["Prev"]->setRepeatClick(true);
            p.buttons["Next"] = ftk::ToolButton::create(context, actions["Next"]);
            p.buttons["Next"]->setRepeatClick(true);

            p.frameShuttle = ftk::ShuttleWidget::create(context);
            p.frameShuttle->setTooltip("帧拖动控制。");

            auto timeUnitsModel = app->getTimeUnitsModel();
            p.currentTimeEdit = tl::ui::TimeEdit::create(context, timeUnitsModel);
            p.currentTimeEdit->setTooltip("当前时间。");

            p.durationLabel = tl::ui::TimeLabel::create(context, timeUnitsModel);
            p.durationLabel->setMarginRole(ftk::SizeRole::MarginInside);
            p.durationLabel->setTooltip("时间线或入出点范围时长。");

            p.timeUnitsWidget = tl::ui::TimeUnitsWidget::create(context, timeUnitsModel);
            p.timeUnitsWidget->setTooltip("时间单位。");

            p.speedButton = ftk::ToolButton::create(context);
            p.speedButton->setPopupIcon("MenuArrow");
            p.speedButton->setTooltip("播放速率。");

            p.audioLabel = ftk::Label::create(context);
            p.audioLabel->setFont(ftk::FontType::Mono);
            p.audioLabel->setHMarginRole(ftk::SizeRole::MarginInside);
            p.audioLabel->setTooltip("音量。");
            p.audioButton = ftk::ToolButton::create(context);
            p.audioButton->setIcon("Volume");
            p.audioButton->setPopupIcon(true);
            p.audioButton->setTooltip("音频控制。");
            actions = audioActions->getActions();
            p.muteButton = ftk::ToolButton::create(context, actions["Mute"]);

            p.layout = ftk::HorizontalLayout::create(context, shared_from_this());
            p.layout->setMarginRole(ftk::SizeRole::MarginInside);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            auto hLayout = ftk::HorizontalLayout::create(context, p.layout);
            hLayout->setSpacingRole(ftk::SizeRole::None);
            p.buttons["Reverse"]->setParent(hLayout);
            p.buttons["Stop"]->setParent(hLayout);
            p.buttons["Forward"]->setParent(hLayout);
            p.loopWidget->setParent(hLayout);
            p.playbackShuttle->setParent(hLayout);
            hLayout = ftk::HorizontalLayout::create(context, p.layout);
            hLayout->setSpacingRole(ftk::SizeRole::None);
            p.buttons["Start"]->setParent(hLayout);
            p.buttons["Prev"]->setParent(hLayout);
            p.buttons["Next"]->setParent(hLayout);
            p.buttons["End"]->setParent(hLayout);
            p.frameShuttle->setParent(hLayout);
            p.currentTimeEdit->setParent(p.layout);
            p.durationLabel->setParent(p.layout);
            p.timeUnitsWidget->setParent(p.layout);
            p.speedButton->setParent(p.layout);
            auto spacer = ftk::Spacer::create(context, ftk::Orientation::Horizontal, p.layout);
            spacer->setHStretch(ftk::Stretch::Expanding);
            hLayout = ftk::HorizontalLayout::create(context, p.layout);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingTool);
            p.audioLabel->setParent(hLayout);
            p.audioButton->setParent(hLayout);
            p.muteButton->setParent(hLayout);

            p.loopWidget->setCallback(
                [this](tl::Loop value)
                {
                    FTK_P();
                    if (auto app = p.app.lock())
                    {
                        app->transportSetLoop(value);
                    }
                });

            p.playbackShuttle->setActiveCallback(
                [this](bool value)
                {
                    FTK_P();
                    if (auto app = p.app.lock())
                    {
                        if (value)
                        {
                            if (p.player &&
                                p.player->isStopped())
                            {
                                app->transportForward();
                            }
                        }
                        else
                        {
                            app->transportSetSpeedMult(1.0);
                        }
                    }
                });
            p.playbackShuttle->setCallback(
                [this](int value)
                {
                    FTK_P();
                    if (auto app = p.app.lock())
                    {
                        app->transportSetSpeedMult(1.0 + value / 10.0);
                    }
                });

            p.frameShuttle->setActiveCallback(
                [this](bool)
                {
                    FTK_P();
                    if (auto app = p.app.lock();
                        app && p.player)
                    {
                        app->transportStop();
                        p.startTime = p.player->getCurrentTime();
                    }
                });
            p.frameShuttle->setCallback(
                [this](int value)
                {
                    FTK_P();
                    if (auto app = p.app.lock();
                        app && p.player)
                    {
                        app->transportSeek(OTIO_NS::RationalTime(
                            p.startTime.value() + value,
                            p.startTime.rate()));
                    }
                });

            p.currentTimeEdit->setCallback(
                [this](const OTIO_NS::RationalTime& value)
                {
                    FTK_P();
                    if (auto app = p.app.lock();
                        app && p.player)
                    {
                        app->transportSeek(value);
                        p.currentTimeEdit->setValue(p.player->getCurrentTime());
                    }
                });

            p.speedButton->setPressedCallback(
                [this]
                {
                    _showSpeedPopup();
                });

            p.audioButton->setPressedCallback(
                [this]
                {
                    _showAudioPopup();
                });

            auto appWeak = std::weak_ptr<App>(app);
            p.muteButton->setCheckedCallback(
                [appWeak](bool value)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getAudioModel()->setMute(value);
                    }
                });

            p.playerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                app->observePlayer(),
                [this](const std::shared_ptr<tl::Player>& value)
                {
                    _playerUpdate(value);
                });

            p.speedObserver2 = ftk::Observer<double>::create(
                p.speedModel->observeValue(),
                [this](double value)
                {
                    FTK_P();
                    if (auto app = p.app.lock())
                    {
                        app->transportSetSpeed(value);
                    }
                });

            p.volumeObserver = ftk::Observer<float>::create(
                app->getAudioModel()->observeVolume(),
                [this](float value)
                {
                    FTK_P();
                    p.audioLabel->setText(ftk::Format("{0}%").
                        arg(static_cast<int>(value * 100.F), 3));
                });
        }

        BottomToolBar::BottomToolBar() :
            _p(new Private)
        {}

        BottomToolBar::~BottomToolBar()
        {}

        std::shared_ptr<BottomToolBar> BottomToolBar::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<PlaybackActions>& playbackActions,
            const std::shared_ptr<FrameActions>& frameActions,
            const std::shared_ptr<AudioActions>& audioActions,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<BottomToolBar>(new BottomToolBar);
            out->_init(context, app, playbackActions, frameActions, audioActions, parent);
            return out;
        }

        void BottomToolBar::focusCurrentFrame()
        {
            if (_p->currentTimeEdit->isEnabled())
            {
                _p->currentTimeEdit->takeKeyFocus();
                _p->currentTimeEdit->selectAll();
            }
        }

        ftk::Size2I BottomToolBar::getSizeHint() const
        {
            return _p->layout->getSizeHint();
        }

        void BottomToolBar::setGeometry(const ftk::Box2I& value)
        {
            IWidget::setGeometry(value);
            _p->layout->setGeometry(value);
        }

        void BottomToolBar::_playerUpdate(const std::shared_ptr<tl::Player>& value)
        {
            FTK_P();

            p.player = value;
            const auto updateSpeedLabel = [this]
            {
                FTK_P();
                double speed = 0.0;
                if (p.player)
                {
                    const tl::Playback playback = p.player->getPlayback();
                    speed = tl::Playback::Stop == playback ?
                        p.player->getSpeed() :
                        p.player->getActualSpeed();
                    if (tl::Playback::Reverse == playback)
                    {
                        speed = -std::abs(speed);
                    }
                }
                p.speedButton->setText(ftk::Format("{0}").arg(speed, 2));
            };

            if (p.player)
            {
                const double defaultSpeed = p.player->getDefaultSpeed();
                p.speedModel->setRange(ftk::RangeD(
                    defaultSpeed * minPlaybackSpeedMult,
                    defaultSpeed * maxPlaybackSpeedMult));
                p.speedModel->setStep(defaultSpeed);
                p.speedModel->setLargeStep(defaultSpeed);

                p.speedObserver = ftk::Observer<double>::create(
                    p.player->observeSpeed(),
                    [this, updateSpeedLabel](double value)
                    {
                        _p->speedModel->setValue(value);
                        updateSpeedLabel();
                    });

                p.actualSpeedObserver = ftk::Observer<double>::create(
                    p.player->observeActualSpeed(),
                    [updateSpeedLabel](double)
                    {
                        updateSpeedLabel();
                    });

                p.loopObserver = ftk::Observer<tl::Loop>::create(
                    p.player->observeLoop(),
                    [this](tl::Loop value)
                    {
                        _p->loopWidget->setLoop(value);
                    });

                p.currentTimeObserver = ftk::Observer<OTIO_NS::RationalTime>::create(
                    p.player->observeCurrentTime(),
                    [this](const OTIO_NS::RationalTime& value)
                    {
                        if (_p->player &&
                            _p->player->isStopped() &&
                            _p->player->getCurrentVideo().empty())
                        {
                            _p->currentTimeEdit->setValue(value);
                        }
                    });

                p.currentVideoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                    p.player->observeCurrentVideo(),
                    [this](const std::vector<tl::VideoFrame>& value)
                    {
                        if (!value.empty() &&
                            !value.front().time.strictly_equal(tl::invalidTime))
                        {
                            _p->currentTimeEdit->setValue(value.front().time);
                        }
                        else if (_p->player && _p->player->isStopped())
                        {
                            _p->currentTimeEdit->setValue(_p->player->getCurrentTime());
                        }
                    });

                p.inOutRangeObserver = ftk::Observer<OTIO_NS::TimeRange>::create(
                    p.player->observeInOutRange(),
                    [this](const OTIO_NS::TimeRange& value)
                    {
                        _p->durationLabel->setValue(value.duration());
                    });

                p.playbackObserver = ftk::Observer<tl::Playback>::create(
                    p.player->observePlayback(),
                    [updateSpeedLabel](tl::Playback)
                    {
                        updateSpeedLabel();
                    });

                p.speedModel->setValue(p.player->getSpeed());
                updateSpeedLabel();
            }
            else
            {
                p.loopWidget->setLoop(tl::Loop::Loop);
                p.currentTimeEdit->setValue(tl::invalidTime);
                p.durationLabel->setValue(tl::invalidTime);
                p.speedModel->setValue(0.0);
                p.speedButton->setText(ftk::Format("{0}").arg(0.0, 2));

                p.speedObserver.reset();
                p.actualSpeedObserver.reset();
                p.loopObserver.reset();
                p.playbackObserver.reset();
                p.currentTimeObserver.reset();
                p.currentVideoObserver.reset();
                p.inOutRangeObserver.reset();
            }

            p.loopWidget->setEnabled(p.player.get());
            p.playbackShuttle->setEnabled(p.player.get());
            p.frameShuttle->setEnabled(p.player.get());
            p.currentTimeEdit->setEnabled(p.player.get());
            p.durationLabel->setEnabled(p.player.get());
            p.speedButton->setEnabled(p.player.get());
        }

        void BottomToolBar::_showSpeedPopup()
        {
            FTK_P();
            if (!p.speedPopup)
            {
                const double defaultSpeed =
                    p.player ?
                    p.player->getDefaultSpeed() :
                    0.0;
                p.speedPopup = ui::SpeedPopup::create(getContext(), p.speedModel, defaultSpeed);
                p.speedPopup->open(getWindow(), p.speedButton->getGeometry());
                std::weak_ptr<BottomToolBar> weak(std::dynamic_pointer_cast<BottomToolBar>(shared_from_this()));
                p.speedPopup->setCallback(
                    [weak](double value)
                    {
                        if (auto widget = weak.lock())
                        {
                            if (auto app = widget->_p->app.lock())
                            {
                                app->transportSetSpeed(value);
                            }
                            widget->_p->speedPopup->close();
                        }
                    });
                p.speedPopup->setCloseCallback(
                    [weak]
                    {
                        if (auto widget = weak.lock())
                        {
                            widget->_p->speedPopup.reset();
                        }
                    });
            }
            else
            {
                p.speedPopup->close();
                p.speedPopup.reset();
            }
        }

        void BottomToolBar::_showAudioPopup()
        {
            FTK_P();
            if (!p.audioPopup)
            {
                p.audioPopup = ui::AudioPopup::create(getContext(), p.app.lock()->getAudioModel());
                p.audioPopup->open(getWindow(), p.audioButton->getGeometry());
                std::weak_ptr<BottomToolBar> weak(std::dynamic_pointer_cast<BottomToolBar>(shared_from_this()));
                p.audioPopup->setCloseCallback(
                    [weak]
                    {
                        if (auto widget = weak.lock())
                        {
                            widget->_p->audioPopup.reset();
                        }
                    });
            }
            else
            {
                p.audioPopup->close();
                p.audioPopup.reset();
            }
        }
    }
}

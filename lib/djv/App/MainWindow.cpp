// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/MainWindow.h>

#include <djv/App/App.h>
#include <djv/App/AudioActions.h>
#include <djv/App/AudioMenu.h>
#include <djv/App/BottomToolBar.h>
#include <djv/App/ColorActions.h>
#include <djv/App/ColorMenu.h>
#include <djv/App/CompareActions.h>
#include <djv/App/CompareMenu.h>
#include <djv/App/CompareToolBar.h>
#include <djv/App/FileActions.h>
#include <djv/App/FileMenu.h>
#include <djv/App/FileToolBar.h>
#include <djv/App/FrameActions.h>
#include <djv/App/FrameMenu.h>
#include <djv/App/HelpActions.h>
#include <djv/App/HelpMenu.h>
#include <djv/App/PlaybackActions.h>
#include <djv/App/PlaybackMenu.h>
#include <djv/App/StatusBar.h>
#include <djv/App/TabBar.h>
#include <djv/App/TimelineActions.h>
#include <djv/App/TimelineMenu.h>
#include <djv/App/ToolsActions.h>
#include <djv/App/ToolsMenu.h>
#include <djv/App/ToolsToolBar.h>
#include <djv/App/ToolsWidget.h>
#include <djv/App/ViewActions.h>
#include <djv/App/ViewMenu.h>
#include <djv/App/ViewToolBar.h>
#include <djv/App/Viewport.h>
#include <djv/App/WindowActions.h>
#include <djv/App/WindowMenu.h>
#include <djv/App/WindowToolBar.h>
#include <djv/UI/SpeedPopup.h>
#include <djv/UI/AboutDialog.h>
#include <djv/UI/SetupDialog.h>
#include <djv/UI/SysInfoDialog.h>
#include <djv/Models/AppInfoModel.h>
#include <djv/Models/ColorModel.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/TimeUnitsModel.h>
#include <djv/Models/ViewportModel.h>

#include <tlRender/UI/TimeEdit.h>
#include <tlRender/UI/TimelineWidget.h>
#if defined(TLRENDER_BMD)
#include <tlRender/Device/BMDOutputDevice.h>
#endif // TLRENDER_BMD
#include <tlRender/GL/Render.h>

#include <ftk/UI/ButtonGroup.h>
#include <ftk/UI/ComboBox.h>
#include <ftk/UI/Divider.h>
#include <ftk/UI/DoubleModel.h>
#include <ftk/UI/IconSystem.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/Menu.h>
#include <ftk/UI/MenuBar.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/Splitter.h>
#include <ftk/UI/ToolButton.h>
#include <ftk/Core/Format.h>

#include <cmath>

namespace djv_resource
{
    extern std::vector<uint8_t> DJV_Icon;
}

namespace djv
{
    namespace app
    {
        struct MainWindow::Private
        {
            struct PaneControls
            {
                std::shared_ptr<ftk::HorizontalLayout> layout;
                std::shared_ptr<ftk::ToolButton> reverseButton;
                std::shared_ptr<ftk::ToolButton> stopButton;
                std::shared_ptr<ftk::ToolButton> forwardButton;
                std::shared_ptr<tl::ui::TimeEdit> timeEdit;
                std::shared_ptr<ftk::DoubleModel> speedModel;
                std::shared_ptr<ftk::ToolButton> speedButton;
                std::shared_ptr<ui::SpeedPopup> speedPopup;
                std::shared_ptr<ftk::Label> forwardStartLabel;
                std::shared_ptr<ftk::ToolButton> forwardStartButton;
                std::shared_ptr<ftk::Label> reverseStartLabel;
                std::shared_ptr<ftk::ToolButton> reverseStartButton;
                std::shared_ptr<tl::Player> player;
                std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > currentTimeObserver;
                std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > currentVideoObserver;
                std::shared_ptr<ftk::Observer<double> > speedObserver;
                std::shared_ptr<ftk::Observer<double> > actualSpeedObserver;
                std::shared_ptr<ftk::Observer<tl::Playback> > playbackObserver;
            };

            std::weak_ptr<App> app;
            std::shared_ptr<models::SettingsModel> settingsModel;
            tl::ui::ItemOptions itemOptions;

            std::shared_ptr<Viewport> viewport;
            std::shared_ptr<Viewport> viewportA;
            std::shared_ptr<Viewport> viewportB;
            std::shared_ptr<tl::ui::TimelineWidget> timelineWidget;
            std::shared_ptr<tl::ui::TimelineWidget> timelineWidgetA;
            std::shared_ptr<tl::ui::TimelineWidget> timelineWidgetB;
            PaneControls paneAControls;
            PaneControls paneBControls;
            std::shared_ptr<FileActions> fileActions;
            std::shared_ptr<CompareActions> compareActions;
            std::shared_ptr<PlaybackActions> playbackActions;
            std::shared_ptr<FrameActions> frameActions;
            std::shared_ptr<TimelineActions> timelineActions;
            std::shared_ptr<AudioActions> audioActions;
            std::shared_ptr<ViewActions> viewActions;
            std::shared_ptr<WindowActions> windowActions;
            std::shared_ptr<ColorActions> colorActions;
            std::shared_ptr<ToolsActions> toolsActions;
            std::shared_ptr<HelpActions> helpActions;
            std::shared_ptr<FileMenu> fileMenu;
            std::shared_ptr<CompareMenu> compareMenu;
            std::shared_ptr<PlaybackMenu> playbackMenu;
            std::shared_ptr<FrameMenu> frameMenu;
            std::shared_ptr<TimelineMenu> timelineMenu;
            std::shared_ptr<AudioMenu> audioMenu;
            std::shared_ptr<ViewMenu> viewMenu;
            std::shared_ptr<WindowMenu> windowMenu;
            std::shared_ptr<ColorMenu> colorMenu;
            std::shared_ptr<ToolsMenu> toolsMenu;
            std::shared_ptr<HelpMenu> helpMenu;
            std::shared_ptr<ftk::MenuBar> menuBar;
            std::shared_ptr<FileToolBar> fileToolBar;
            std::shared_ptr<CompareToolBar> compareToolBar;
            std::shared_ptr<ViewToolBar> viewToolBar;
            std::shared_ptr<WindowToolBar> windowToolBar;
            std::shared_ptr<ToolsToolBar> toolsToolBar;
            std::shared_ptr<TabBar> tabBar;
            std::shared_ptr<BottomToolBar> bottomToolBar;
            std::shared_ptr<StatusBar> statusBar;
            std::shared_ptr<ToolsWidget> toolsWidget;
            std::shared_ptr<ui::SetupDialog> setupDialog;
            std::shared_ptr<ui::AboutDialog> aboutDialog;
            std::shared_ptr<ui::SysInfoDialog> sysInfoDialog;
            std::map<std::string, std::shared_ptr<ftk::Divider> > dividers;
            std::shared_ptr<ftk::Splitter> splitter;
            std::shared_ptr<ftk::Splitter> splitter2;
            std::shared_ptr<ftk::Splitter> compareSplitterH;
            std::shared_ptr<ftk::Splitter> compareSplitterV;
            std::shared_ptr<ftk::VerticalLayout> splitterLayout;
            std::shared_ptr<ftk::VerticalLayout> primaryViewportLayout;
            std::shared_ptr<ftk::VerticalLayout> compareLayout;
            std::shared_ptr<ftk::HorizontalLayout> compareHeaderLayout;
            std::shared_ptr<ftk::ComboBox> splitModeComboBox;
            std::shared_ptr<ftk::VerticalLayout> compareHorizontalLayout;
            std::shared_ptr<ftk::VerticalLayout> compareVerticalLayout;
            std::shared_ptr<ftk::VerticalLayout> comparePaneALayoutH;
            std::shared_ptr<ftk::VerticalLayout> comparePaneBLayoutH;
            std::shared_ptr<ftk::VerticalLayout> comparePaneALayoutV;
            std::shared_ptr<ftk::VerticalLayout> comparePaneBLayoutV;
            std::shared_ptr<ftk::VerticalLayout> layout;

            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > playerObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > playerAObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > playerBObserver;
            std::shared_ptr<ftk::Observer<models::DualPlaybackMode> > splitPlaybackModeObserver;
            std::shared_ptr<ftk::Observer<models::DisplayMode> > displayModeObserver;
            std::shared_ptr<ftk::Observer<tl::OCIOOptions> > ocioOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::LUTOptions> > lutOptionsObserver;
            std::shared_ptr<ftk::Observer<ftk::gl::TextureType> > colorBufferObserver;
            std::shared_ptr<ftk::Observer<models::MouseSettings> > mouseSettingsObserver;
            std::shared_ptr<ftk::Observer<models::TimelineSettings> > timelineSettingsObserver;
            std::shared_ptr<ftk::Observer<bool> > timelineFrameViewObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > timelineTimeScrubObserver;
            std::shared_ptr<ftk::Observer<bool> > timelineScrubObserverA;
            std::shared_ptr<ftk::Observer<bool> > timelineScrubObserverB;
            std::shared_ptr<ftk::Observer<models::WindowSettings> > windowSettingsObserver;
        };

        void MainWindow::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app)
        {
            const models::WindowSettings& settings = app->getSettingsModel()->getWindow();
            ftk::Size2I startupSize = settings.size;
            if (const auto& monitors = app->observeMonitors()->get(); !monitors.empty())
            {
                startupSize = monitors.front().bounds.size();
            }
            Window::_init(
                context,
                app,
                ftk::Format("{0} {1}").
                    arg(app->getAppInfoModel()->getFullName()).
                    arg(app->getAppInfoModel()->getVersion()),
                startupSize);
            FTK_P();

            auto iconSystem = context->getSystem<ftk::IconSystem>();
            iconSystem->add("DJV_Icon", djv_resource::DJV_Icon);
            setIcon(iconSystem->get("DJV_Icon", 1.0));

            p.app = app;
            p.settingsModel = app->getSettingsModel();

            p.viewport = Viewport::create(context, app, Viewport::Role::Primary);
            p.viewportA = Viewport::create(context, app, Viewport::Role::SplitA);
            p.viewportB = Viewport::create(context, app, Viewport::Role::SplitB);
            p.viewport->setVStretch(ftk::Stretch::Expanding);
            p.viewportA->setVStretch(ftk::Stretch::Expanding);
            p.viewportB->setVStretch(ftk::Stretch::Expanding);

            auto timeUnitsModel = app->getTimeUnitsModel();
            p.timelineWidget = tl::ui::TimelineWidget::create(context, timeUnitsModel);
            p.timelineWidgetA = tl::ui::TimelineWidget::create(context, timeUnitsModel);
            p.timelineWidgetB = tl::ui::TimelineWidget::create(context, timeUnitsModel);
            p.timelineWidget->setVStretch(ftk::Stretch::Fixed);
            p.timelineWidgetA->setVStretch(ftk::Stretch::Fixed);
            p.timelineWidgetB->setVStretch(ftk::Stretch::Fixed);

            p.splitModeComboBox = ftk::ComboBox::create(
                context,
                std::vector<std::string>{ "独立模式", "同步模式" });

            auto appWeak = std::weak_ptr<App>(app);
            auto createPaneControls = [
                this,
                context,
                timeUnitsModel,
                appWeak](
                    Private::PaneControls& pane,
                    App::SplitPane splitPane,
                    const std::string& prefix)
            {
                pane.layout = ftk::HorizontalLayout::create(context);
                pane.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
                pane.layout->setMarginRole(ftk::SizeRole::MarginInside);

                pane.reverseButton = ftk::ToolButton::create(context);
                pane.reverseButton->setIcon("PlaybackReverse");
                pane.reverseButton->setTooltip(prefix + "倒放");
                pane.stopButton = ftk::ToolButton::create(context);
                pane.stopButton->setIcon("PlaybackStop");
                pane.stopButton->setTooltip(prefix + "停止");
                pane.forwardButton = ftk::ToolButton::create(context);
                pane.forwardButton->setIcon("PlaybackForward");
                pane.forwardButton->setTooltip(prefix + "正放");

                pane.timeEdit = tl::ui::TimeEdit::create(context, timeUnitsModel);
                pane.timeEdit->setTooltip(prefix + "当前时间");
                pane.timeEdit->setHStretch(ftk::Stretch::Expanding);

                pane.speedModel = ftk::DoubleModel::create();
                pane.speedButton = ftk::ToolButton::create(context);
                pane.speedButton->setPopupIcon("MenuArrow");
                pane.speedButton->setTooltip(prefix + "播放速率");

                pane.forwardStartButton = ftk::ToolButton::create(context);
                pane.forwardStartButton->setText("设正放点");
                pane.forwardStartButton->setTooltip(prefix + "正放时从该时间开始");
                pane.forwardStartLabel = ftk::Label::create(context);
                pane.forwardStartLabel->setFont(ftk::FontType::Mono);

                pane.reverseStartButton = ftk::ToolButton::create(context);
                pane.reverseStartButton->setText("设倒放点");
                pane.reverseStartButton->setTooltip(prefix + "倒放时从该时间开始");
                pane.reverseStartLabel = ftk::Label::create(context);
                pane.reverseStartLabel->setFont(ftk::FontType::Mono);

                pane.reverseButton->setParent(pane.layout);
                pane.stopButton->setParent(pane.layout);
                pane.forwardButton->setParent(pane.layout);
                pane.timeEdit->setParent(pane.layout);
                pane.speedButton->setParent(pane.layout);
                pane.forwardStartButton->setParent(pane.layout);
                pane.forwardStartLabel->setParent(pane.layout);
                pane.reverseStartButton->setParent(pane.layout);
                pane.reverseStartLabel->setParent(pane.layout);

                pane.reverseButton->setPressedCallback(
                    [appWeak, splitPane]
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->splitReverse(splitPane);
                        }
                    });
                pane.stopButton->setPressedCallback(
                    [appWeak, splitPane]
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->splitStop(splitPane);
                        }
                    });
                pane.forwardButton->setPressedCallback(
                    [appWeak, splitPane]
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->splitForward(splitPane);
                        }
                    });
                pane.timeEdit->setCallback(
                    [appWeak, splitPane](const OTIO_NS::RationalTime& value)
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->splitSeek(splitPane, value);
                        }
                    });
                pane.forwardStartButton->setPressedCallback(
                    [appWeak, splitPane, timeUnitsModel, &pane]
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->splitSetForwardStart(splitPane);
                            const auto filesModel = app->getFilesModel();
                            const auto item = App::SplitPane::B == splitPane ?
                                (!filesModel->getB().empty() ? filesModel->getB().front() : std::shared_ptr<models::FilesModelItem>()) :
                                filesModel->getA();
                            pane.forwardStartLabel->setText(
                                item && !item->forwardStartTime.strictly_equal(tl::invalidTime) ?
                                timeUnitsModel->getLabel(item->forwardStartTime) :
                                "未设置");
                        }
                    });
                pane.reverseStartButton->setPressedCallback(
                    [appWeak, splitPane, timeUnitsModel, &pane]
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->splitSetReverseStart(splitPane);
                            const auto filesModel = app->getFilesModel();
                            const auto item = App::SplitPane::B == splitPane ?
                                (!filesModel->getB().empty() ? filesModel->getB().front() : std::shared_ptr<models::FilesModelItem>()) :
                                filesModel->getA();
                            pane.reverseStartLabel->setText(
                                item && !item->reverseStartTime.strictly_equal(tl::invalidTime) ?
                                timeUnitsModel->getLabel(item->reverseStartTime) :
                                "未设置");
                        }
                    });
                pane.speedButton->setPressedCallback(
                    [this, &pane, splitPane]
                    {
                        if (!pane.speedPopup)
                        {
                            const double defaultSpeed =
                                pane.player ?
                                pane.player->getDefaultSpeed() :
                                0.0;
                            pane.speedPopup = ui::SpeedPopup::create(getContext(), pane.speedModel, defaultSpeed);
                            pane.speedPopup->open(getWindow(), pane.speedButton->getGeometry());
                            std::weak_ptr<MainWindow> weak(std::dynamic_pointer_cast<MainWindow>(shared_from_this()));
                            pane.speedPopup->setCallback(
                                [weak, splitPane](double value)
                                {
                                    if (auto window = weak.lock())
                                    {
                                        if (auto app = window->_p->app.lock())
                                        {
                                            app->splitSetSpeed(splitPane, value);
                                        }
                                        auto& controls = App::SplitPane::A == splitPane ?
                                            window->_p->paneAControls :
                                            window->_p->paneBControls;
                                        if (controls.speedPopup)
                                        {
                                            controls.speedPopup->close();
                                        }
                                    }
                                });
                            pane.speedPopup->setCloseCallback(
                                [weak, splitPane]
                                {
                                    if (auto window = weak.lock())
                                    {
                                        auto& controls = App::SplitPane::A == splitPane ?
                                            window->_p->paneAControls :
                                            window->_p->paneBControls;
                                        controls.speedPopup.reset();
                                    }
                                });
                        }
                        else
                        {
                            pane.speedPopup->close();
                            pane.speedPopup.reset();
                        }
                    });
            };
            createPaneControls(p.paneAControls, App::SplitPane::A, "A 视频");
            createPaneControls(p.paneBControls, App::SplitPane::B, "B 视频");
            p.splitModeComboBox->setIndexCallback(
                [appWeak](int value)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->setDualPlaybackMode(
                            0 == value ?
                            models::DualPlaybackMode::Independent :
                            models::DualPlaybackMode::Sync);
                    }
                });

            p.fileActions = FileActions::create(context, app);
            p.compareActions = CompareActions::create(context, app);
            p.playbackActions = PlaybackActions::create(context, app);
            p.frameActions = FrameActions::create(
                context,
                app,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));
            p.timelineActions = TimelineActions::create(
                context,
                app,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));
            p.audioActions = AudioActions::create(context, app);
            p.viewActions = ViewActions::create(
                context,
                app,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));
            p.windowActions = WindowActions::create(
                context,
                app,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));
            p.colorActions = ColorActions::create(context, app);
            p.toolsActions = ToolsActions::create(context, app);
            p.helpActions = HelpActions::create(
                context,
                app,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));

            p.fileMenu = FileMenu::create(context, app, p.fileActions);
            p.compareMenu = CompareMenu::create(context, app, p.compareActions);
            p.playbackMenu = PlaybackMenu::create(context, p.playbackActions);
            p.frameMenu = FrameMenu::create(context, p.frameActions);
            p.timelineMenu = TimelineMenu::create(context, p.timelineActions);
            p.audioMenu = AudioMenu::create(context, p.audioActions);
            p.viewMenu = ViewMenu::create(context, p.viewActions);
            p.windowMenu = WindowMenu::create(
                context,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()),
                p.windowActions);
            p.colorMenu = ColorMenu::create(context, p.colorActions);
            p.toolsMenu = ToolsMenu::create(context, p.toolsActions);
            p.helpMenu = HelpMenu::create(context, p.helpActions);
            p.menuBar = ftk::MenuBar::create(context);
            p.menuBar->addMenu("File", p.fileMenu);
            p.menuBar->addMenu("Playback", p.playbackMenu);
            p.menuBar->addMenu("Frame", p.frameMenu);
            p.menuBar->addMenu("Timeline", p.timelineMenu);
            p.menuBar->addMenu("Audio", p.audioMenu);
            p.menuBar->addMenu("View", p.viewMenu);
            p.menuBar->addMenu("Window", p.windowMenu);
            p.menuBar->addMenu("Color", p.colorMenu);
            p.menuBar->addMenu("Tools", p.toolsMenu);
            p.menuBar->addMenu("Help", p.helpMenu);

            p.fileToolBar = FileToolBar::create(
                context,
                p.fileActions->getActions());
            p.compareToolBar = CompareToolBar::create(
                context,
                p.compareActions->getActions());
            p.viewToolBar = ViewToolBar::create(
                context,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()),
                p.viewActions);
            p.windowToolBar = WindowToolBar::create(
                context,
                p.windowActions->getActions());
            p.toolsToolBar = ToolsToolBar::create(
                context,
                p.toolsActions->getActions());
            p.tabBar = TabBar::create(context, app);
            p.bottomToolBar = BottomToolBar::create(
                context,
                app,
                p.playbackActions,
                p.frameActions,
                p.audioActions);
            p.statusBar = StatusBar::create(context, app);

            p.toolsWidget = ToolsWidget::create(
                context,
                app,
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));

            p.layout = ftk::VerticalLayout::create(context, shared_from_this());
            p.layout->setSpacingRole(ftk::SizeRole::None);
            p.menuBar->setParent(p.layout);
            p.dividers["MenuBar"] = ftk::Divider::create(context, ftk::Orientation::Vertical, p.layout);
            auto hLayout = ftk::HorizontalLayout::create(context, p.layout);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.fileToolBar->setParent(hLayout);
            p.dividers["File"] = ftk::Divider::create(context, ftk::Orientation::Horizontal, hLayout);
            p.compareToolBar->setParent(hLayout);
            p.compareToolBar->setVisible(false);
            p.dividers["Compare"] = ftk::Divider::create(context, ftk::Orientation::Horizontal, hLayout);
            p.dividers["Compare"]->setVisible(false);
            p.windowToolBar->setParent(hLayout);
            p.dividers["Window"] = ftk::Divider::create(context, ftk::Orientation::Horizontal, hLayout);
            p.viewToolBar->setParent(hLayout);
            p.dividers["View"] = ftk::Divider::create(context, ftk::Orientation::Horizontal, hLayout);
            p.toolsToolBar->setParent(hLayout);
            p.dividers["ToolBars"] = ftk::Divider::create(context, ftk::Orientation::Vertical, p.layout);
            p.splitterLayout = ftk::VerticalLayout::create(context, p.layout);
            p.splitterLayout->setSpacingRole(ftk::SizeRole::None);
            p.splitterLayout->setVStretch(ftk::Stretch::Expanding);
            p.splitter = ftk::Splitter::create(context, ftk::Orientation::Vertical, p.splitterLayout);
            p.splitter->setSplit(settings.splitter);
            p.splitter2 = ftk::Splitter::create(context, ftk::Orientation::Horizontal, p.splitter);
            p.splitter2->setSplit(settings.splitter2);
            auto vLayout = ftk::VerticalLayout::create(context, p.splitter2);
            vLayout->setSpacingRole(ftk::SizeRole::None);
            p.tabBar->setVisible(false);
            p.primaryViewportLayout = ftk::VerticalLayout::create(context, vLayout);
            p.primaryViewportLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.primaryViewportLayout->setVStretch(ftk::Stretch::Expanding);
            p.compareLayout = ftk::VerticalLayout::create(context, vLayout);
            p.compareLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.compareLayout->setVStretch(ftk::Stretch::Expanding);
            p.compareLayout->setVisible(false);
            p.compareHeaderLayout = ftk::HorizontalLayout::create(context, p.compareLayout);
            p.compareHeaderLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.compareHeaderLayout->setMarginRole(ftk::SizeRole::MarginInside);
            p.compareHeaderLayout->setVisible(false);
            p.compareHorizontalLayout = ftk::VerticalLayout::create(context, p.compareLayout);
            p.compareHorizontalLayout->setSpacingRole(ftk::SizeRole::None);
            p.compareHorizontalLayout->setVStretch(ftk::Stretch::Expanding);
            p.compareSplitterH = ftk::Splitter::create(context, ftk::Orientation::Horizontal, p.compareHorizontalLayout);
            p.compareSplitterH->setSplit(.5F);
            p.comparePaneALayoutH = ftk::VerticalLayout::create(context, p.compareSplitterH);
            p.comparePaneALayoutH->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.comparePaneALayoutH->setVStretch(ftk::Stretch::Expanding);
            p.comparePaneBLayoutH = ftk::VerticalLayout::create(context, p.compareSplitterH);
            p.comparePaneBLayoutH->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.comparePaneBLayoutH->setVStretch(ftk::Stretch::Expanding);
            p.compareVerticalLayout = ftk::VerticalLayout::create(context, p.compareLayout);
            p.compareVerticalLayout->setSpacingRole(ftk::SizeRole::None);
            p.compareVerticalLayout->setVStretch(ftk::Stretch::Expanding);
            p.compareVerticalLayout->setVisible(false);
            p.compareSplitterV = ftk::Splitter::create(context, ftk::Orientation::Vertical, p.compareVerticalLayout);
            p.compareSplitterV->setSplit(.5F);
            p.comparePaneALayoutV = ftk::VerticalLayout::create(context, p.compareSplitterV);
            p.comparePaneALayoutV->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.comparePaneALayoutV->setVStretch(ftk::Stretch::Expanding);
            p.comparePaneBLayoutV = ftk::VerticalLayout::create(context, p.compareSplitterV);
            p.comparePaneBLayoutV->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.comparePaneBLayoutV->setVStretch(ftk::Stretch::Expanding);

            p.viewport->setParent(p.primaryViewportLayout);
            p.viewportA->setParent(p.comparePaneALayoutH);
            p.timelineWidgetA->setParent(p.comparePaneALayoutH);
            p.paneAControls.layout->setParent(p.comparePaneALayoutH);
            p.viewportB->setParent(p.comparePaneBLayoutH);
            p.timelineWidgetB->setParent(p.comparePaneBLayoutH);
            p.paneBControls.layout->setParent(p.comparePaneBLayoutH);
            p.toolsWidget->setParent(p.splitter2);
            p.timelineWidget->setParent(p.splitter);
            p.dividers["Bottom"] = ftk::Divider::create(context, ftk::Orientation::Vertical, p.layout);
            p.bottomToolBar->setParent(p.layout);
            p.dividers["Status"] = ftk::Divider::create(context, ftk::Orientation::Vertical, p.layout);
            p.statusBar->setParent(p.layout);

            auto miscSettings = app->getSettingsModel()->getMisc();
            if (miscSettings.showSetup)
            {
                miscSettings.showSetup = false;
                auto settingsModel = app->getSettingsModel();
                settingsModel->setMisc(miscSettings);
                p.setupDialog = ui::SetupDialog::create(
                    context,
                    app->getAppInfoModel(),
                    settingsModel,
                    app->getTimeUnitsModel());
                p.setupDialog->open(std::dynamic_pointer_cast<IWindow>(shared_from_this()));
                p.setupDialog->setCloseCallback(
                    [this]
                    {
                        _p->setupDialog.reset();
                    });
            }

            auto getPaneItem = [app](App::SplitPane splitPane)
            {
                const auto filesModel = app->getFilesModel();
                if (App::SplitPane::B == splitPane)
                {
                    const auto& b = filesModel->getB();
                    return !b.empty() ? b.front() : std::shared_ptr<models::FilesModelItem>();
                }
                return filesModel->getA();
            };
            auto getPaneControls = [this](App::SplitPane splitPane) -> Private::PaneControls&
            {
                return App::SplitPane::A == splitPane ?
                    _p->paneAControls :
                    _p->paneBControls;
            };
            auto updatePaneStartLabels = [this, timeUnitsModel, getPaneItem, getPaneControls](App::SplitPane splitPane)
            {
                auto& pane = getPaneControls(splitPane);
                const auto item = getPaneItem(splitPane);
                pane.forwardStartLabel->setText(
                    item && !item->forwardStartTime.strictly_equal(tl::invalidTime) ?
                    timeUnitsModel->getLabel(item->forwardStartTime) :
                    "未设置");
                pane.reverseStartLabel->setText(
                    item && !item->reverseStartTime.strictly_equal(tl::invalidTime) ?
                    timeUnitsModel->getLabel(item->reverseStartTime) :
                    "未设置");
            };
            auto updatePanePlaybackButtons = [getPaneControls](App::SplitPane splitPane, tl::Playback playback)
            {
                auto& pane = getPaneControls(splitPane);
                pane.stopButton->setChecked(tl::Playback::Stop == playback);
                pane.forwardButton->setChecked(tl::Playback::Forward == playback);
                pane.reverseButton->setChecked(tl::Playback::Reverse == playback);
            };
            auto updatePaneSpeedText = [this, getPaneItem, getPaneControls](App::SplitPane splitPane)
            {
                auto& pane = getPaneControls(splitPane);
                const auto item = getPaneItem(splitPane);
                double speed = 0.0;
                tl::Playback playback = pane.player ? pane.player->getPlayback() : tl::Playback::Stop;
                if (pane.player)
                {
                    speed = tl::Playback::Stop == playback ?
                        pane.player->getSpeed() :
                        pane.player->getActualSpeed();
                }
                if (tl::Playback::Stop == playback && item)
                {
                    playback = item->playback;
                }
                if (tl::Playback::Reverse == playback)
                {
                    speed = -std::abs(speed);
                }
                pane.speedButton->setText(ftk::Format("{0}").arg(speed, 2));
            };
            auto updatePaneEnableState = [this, app, getPaneControls](App::SplitPane splitPane)
            {
                auto& pane = getPaneControls(splitPane);
                const bool enabled = static_cast<bool>(pane.player);
                pane.reverseButton->setEnabled(enabled);
                pane.stopButton->setEnabled(enabled);
                pane.forwardButton->setEnabled(enabled);
                pane.speedButton->setEnabled(enabled);
                pane.forwardStartButton->setEnabled(enabled);
                pane.reverseStartButton->setEnabled(enabled);
                pane.timeEdit->setEnabled(enabled);
            };
            auto updatePaneControls = [this, getPaneControls, updatePaneStartLabels, updatePanePlaybackButtons, updatePaneSpeedText, updatePaneEnableState](
                App::SplitPane splitPane,
                const std::shared_ptr<tl::Player>& player)
            {
                auto& pane = getPaneControls(splitPane);
                pane.currentTimeObserver.reset();
                pane.currentVideoObserver.reset();
                pane.speedObserver.reset();
                pane.actualSpeedObserver.reset();
                pane.playbackObserver.reset();
                pane.player = player;

                if (pane.player)
                {
                    const double defaultSpeed = pane.player->getDefaultSpeed();
                    pane.speedModel->setRange(ftk::RangeD(
                        defaultSpeed / 3.0,
                        defaultSpeed * 3.0));
                    pane.speedModel->setStep(defaultSpeed);
                    pane.speedModel->setLargeStep(defaultSpeed);
                    pane.currentTimeObserver = ftk::Observer<OTIO_NS::RationalTime>::create(
                        pane.player->observeCurrentTime(),
                        [getPaneControls, splitPane](const OTIO_NS::RationalTime& value)
                        {
                            auto& controls = getPaneControls(splitPane);
                            if (controls.player &&
                                controls.player->isStopped() &&
                                controls.player->getCurrentVideo().empty())
                            {
                                controls.timeEdit->setValue(value);
                            }
                        });
                    pane.currentVideoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                        pane.player->observeCurrentVideo(),
                        [getPaneControls, splitPane](const std::vector<tl::VideoFrame>& value)
                        {
                            auto& controls = getPaneControls(splitPane);
                            if (!value.empty() &&
                                !value.front().time.strictly_equal(tl::invalidTime))
                            {
                                controls.timeEdit->setValue(value.front().time);
                            }
                            else if (controls.player && controls.player->isStopped())
                            {
                                controls.timeEdit->setValue(controls.player->getCurrentTime());
                            }
                        });
                    pane.speedObserver = ftk::Observer<double>::create(
                        pane.player->observeSpeed(),
                        [getPaneControls, updatePaneSpeedText, splitPane](double value)
                        {
                            auto& controls = getPaneControls(splitPane);
                            controls.speedModel->setValue(value);
                            updatePaneSpeedText(splitPane);
                        });
                    pane.actualSpeedObserver = ftk::Observer<double>::create(
                        pane.player->observeActualSpeed(),
                        [updatePaneSpeedText, splitPane](double)
                        {
                            updatePaneSpeedText(splitPane);
                        });
                    pane.playbackObserver = ftk::Observer<tl::Playback>::create(
                        pane.player->observePlayback(),
                        [this, splitPane, updatePanePlaybackButtons, updatePaneSpeedText](tl::Playback value)
                        {
                            updatePanePlaybackButtons(splitPane, value);
                            updatePaneSpeedText(splitPane);
                        });
                    pane.timeEdit->setValue(pane.player->getCurrentTime());
                    pane.speedModel->setValue(pane.player->getSpeed());
                    updatePanePlaybackButtons(splitPane, pane.player->getPlayback());
                }
                else
                {
                    pane.timeEdit->setValue(tl::invalidTime);
                    pane.speedModel->setValue(0.0);
                    updatePanePlaybackButtons(splitPane, tl::Playback::Stop);
                }

                updatePaneStartLabels(splitPane);
                updatePaneSpeedText(splitPane);
                updatePaneEnableState(splitPane);
            };

            p.playerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                app->observePlayer(),
                [this](const std::shared_ptr<tl::Player>& player)
                {
                    FTK_P();
                    p.viewport->setPlayer(player);
                    p.timelineWidget->setPlayer(player);
                });

            p.timelineTimeScrubObserver = ftk::Observer<OTIO_NS::RationalTime>::create(
                p.timelineWidget->observeTimeScrub(),
                [appWeak](const OTIO_NS::RationalTime& value)
                {
                    if (value.strictly_equal(tl::invalidTime))
                    {
                        return;
                    }
                    if (auto app = appWeak.lock();
                        app && models::DisplayMode::Dual == app->getFilesModel()->getDisplayMode())
                    {
                        app->transportSeek(value);
                    }
                });

            p.playerAObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                app->observePlayerA(),
                [this, updatePaneControls](const std::shared_ptr<tl::Player>& player)
                {
                    FTK_P();
                    p.viewportA->setPlayer(player);
                    p.timelineWidgetA->setPlayer(player);
                    updatePaneControls(App::SplitPane::A, player);
                });

            p.playerBObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                app->observePlayerB(),
                [this, app, updatePaneControls](const std::shared_ptr<tl::Player>& player)
                {
                    FTK_P();
                    const auto effectivePlayer =
                        app->getFilesModel()->getB().empty() ?
                        std::shared_ptr<tl::Player>() :
                        player;
                    p.viewportB->setPlayer(effectivePlayer);
                    p.timelineWidgetB->setPlayer(effectivePlayer);
                    updatePaneControls(App::SplitPane::B, effectivePlayer);
                });

            p.splitPlaybackModeObserver = ftk::Observer<models::DualPlaybackMode>::create(
                app->getFilesModel()->observeDualPlaybackMode(),
                [this, updatePaneEnableState](models::DualPlaybackMode value)
                {
                    FTK_P();
                    const bool syncMode = models::DualPlaybackMode::Sync == value;
                    p.splitModeComboBox->setCurrentIndex(syncMode ? 1 : 0);
                    updatePaneEnableState(App::SplitPane::A);
                    updatePaneEnableState(App::SplitPane::B);
                });

            p.timelineScrubObserverA = ftk::Observer<bool>::create(
                p.timelineWidgetA->observeScrub(),
                [appWeak](bool value)
                {
                    if (value)
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->setSplitActivePane(App::SplitPane::A);
                        }
                    }
                });

            p.timelineScrubObserverB = ftk::Observer<bool>::create(
                p.timelineWidgetB->observeScrub(),
                [appWeak](bool value)
                {
                    if (value)
                    {
                        if (auto app = appWeak.lock())
                        {
                            app->setSplitActivePane(App::SplitPane::B);
                        }
                    }
                });

            p.displayModeObserver = ftk::Observer<models::DisplayMode>::create(
                app->getFilesModel()->observeDisplayMode(),
                [this, app, updatePaneEnableState](models::DisplayMode value)
                {
                    const bool splitMode = models::DisplayMode::Dual == value;
                    const bool showMasterTimeline =
                        splitMode ||
                        _p->settingsModel->getWindow().timeline;
                    _p->primaryViewportLayout->setVisible(!splitMode);
                    _p->compareLayout->setVisible(splitMode);
                    _p->compareHeaderLayout->setVisible(false);
                    _p->compareHorizontalLayout->setVisible(splitMode);
                    _p->compareVerticalLayout->setVisible(false);
                    _p->timelineWidget->setVisible(showMasterTimeline);
                    _p->timelineWidgetA->setVisible(splitMode);
                    _p->timelineWidgetB->setVisible(splitMode);
                    _p->paneAControls.layout->setVisible(splitMode);
                    _p->paneBControls.layout->setVisible(splitMode);
                    if (splitMode)
                    {
                        if (_p->viewportA->getParent() != _p->comparePaneALayoutH)
                        {
                            _p->viewportA->setParent(_p->comparePaneALayoutH);
                        }
                        if (_p->timelineWidgetA->getParent() != _p->comparePaneALayoutH)
                        {
                            _p->timelineWidgetA->setParent(_p->comparePaneALayoutH);
                        }
                        if (_p->paneAControls.layout->getParent() != _p->comparePaneALayoutH)
                        {
                            _p->paneAControls.layout->setParent(_p->comparePaneALayoutH);
                        }
                        if (_p->viewportB->getParent() != _p->comparePaneBLayoutH)
                        {
                            _p->viewportB->setParent(_p->comparePaneBLayoutH);
                        }
                        if (_p->timelineWidgetB->getParent() != _p->comparePaneBLayoutH)
                        {
                            _p->timelineWidgetB->setParent(_p->comparePaneBLayoutH);
                        }
                        if (_p->paneBControls.layout->getParent() != _p->comparePaneBLayoutH)
                        {
                            _p->paneBControls.layout->setParent(_p->comparePaneBLayoutH);
                        }
                    }
                    updatePaneEnableState(App::SplitPane::A);
                    updatePaneEnableState(App::SplitPane::B);
                    _updateTimelineDisplayOptions();
                });

            p.ocioOptionsObserver = ftk::Observer<tl::OCIOOptions>::create(
                app->getColorModel()->observeOCIOOptions(),
                [this](const tl::OCIOOptions& value)
                {
                    for (const auto& timeline :
                        { _p->timelineWidget, _p->timelineWidgetA, _p->timelineWidgetB })
                    {
                        auto options = timeline->getDisplayOptions();
                        options.ocio = value;
                        timeline->setDisplayOptions(options);
                    }
                });

            p.lutOptionsObserver = ftk::Observer<tl::LUTOptions>::create(
                app->getColorModel()->observeLUTOptions(),
                [this](const tl::LUTOptions& value)
                {
                    for (const auto& timeline :
                        { _p->timelineWidget, _p->timelineWidgetA, _p->timelineWidgetB })
                    {
                        auto options = timeline->getDisplayOptions();
                        options.lut = value;
                        timeline->setDisplayOptions(options);
                    }
                });

            p.colorBufferObserver = ftk::Observer<ftk::gl::TextureType>::create(
                app->getViewportModel()->observeColorBuffer(),
                [this](ftk::gl::TextureType value)
                {
                    setBufferType(ftk::gl::TextureType::RGBA_U8 == value ?
                        ftk::WindowBufferType::U8 :
                        ftk::WindowBufferType::F32);
                });

            p.mouseSettingsObserver = ftk::Observer<models::MouseSettings>::create(
                p.settingsModel->observeMouse(),
                [this](const models::MouseSettings& value)
                {
                    _settingsUpdate(value);
                });

            p.timelineSettingsObserver = ftk::Observer<models::TimelineSettings>::create(
                p.settingsModel->observeTimeline(),
                [this](const models::TimelineSettings& value)
                {
                    _settingsUpdate(value);
                });

            auto appWeak2 = std::weak_ptr<App>(app);
            p.timelineFrameViewObserver = ftk::Observer<bool>::create(
                p.timelineWidget->observeFrameView(),
                [appWeak2](bool value)
                {
                    if (auto app = appWeak2.lock())
                    {
                        auto settings = app->getSettingsModel()->getTimeline();
                        settings.frameView = value;
                        app->getSettingsModel()->setTimeline(settings);
                    }
                });

            p.windowSettingsObserver = ftk::Observer<models::WindowSettings>::create(
                p.settingsModel->observeWindow(),
                [this](const models::WindowSettings& value)
                {
                    _settingsUpdate(value);
                });

            setSizeUpdate();
        }

        MainWindow::MainWindow() :
            _p(new Private)
        {}

        MainWindow::~MainWindow()
        {
            FTK_P();
            _makeCurrent();
            p.viewport->setParent(nullptr);
            p.viewportA->setParent(nullptr);
            p.viewportB->setParent(nullptr);
            p.timelineWidget->setParent(nullptr);
            p.timelineWidgetA->setParent(nullptr);
            p.timelineWidgetB->setParent(nullptr);
            p.paneAControls.layout->setParent(nullptr);
            p.paneBControls.layout->setParent(nullptr);

            models::WindowSettings settings = p.settingsModel->getWindow();
            settings.size = getGeometry().size();
#if defined(__APPLE__)
            //! \bug The window size needs to be scaled on macOS?
            const float displayScale = getDisplayScale();
            if (displayScale > 0.F)
            {
                settings.size = settings.size / displayScale;
            }
#endif // __APPLE__
            settings.splitter = p.splitter->getSplit();
            settings.splitter2 = p.splitter2->getSplit();
            p.settingsModel->setWindow(settings);
        }

        std::shared_ptr<MainWindow> MainWindow::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app)
        {
            auto out = std::shared_ptr<MainWindow>(new MainWindow);
            out->_init(context, app);
            return out;
        }

        const std::shared_ptr<ftk::MenuBar> MainWindow::getMenuBar() const
        {
            return _p->menuBar;
        }

        const std::shared_ptr<Viewport>& MainWindow::getViewport() const
        {
            return _p->viewport;
        }

        const std::shared_ptr<tl::ui::TimelineWidget>& MainWindow::getTimelineWidget() const
        {
            return _p->timelineWidget;
        }

        const std::shared_ptr<tl::ui::TimelineWidget>& MainWindow::getTimelineWidgetA() const
        {
            return _p->timelineWidgetA;
        }

        const std::shared_ptr<tl::ui::TimelineWidget>& MainWindow::getTimelineWidgetB() const
        {
            return _p->timelineWidgetB;
        }

        void MainWindow::focusCurrentFrame()
        {
            _p->bottomToolBar->focusCurrentFrame();
        }

        void MainWindow::showAboutDialog()
        {
            FTK_P();
            p.aboutDialog = ui::AboutDialog::create(
                getContext(),
                p.app.lock()->getAppInfoModel());
            p.aboutDialog->open(std::dynamic_pointer_cast<IWindow>(shared_from_this()));
            p.aboutDialog->setCloseCallback(
                [this]
                {
                    _p->aboutDialog.reset();
                });
        }

        void MainWindow::showSysInfoDialog()
        {
            FTK_P();
            p.sysInfoDialog = ui::SysInfoDialog::create(
                getContext(),
                p.app.lock()->getAppInfoModel(),
                std::dynamic_pointer_cast<MainWindow>(shared_from_this()));
            p.sysInfoDialog->open(std::dynamic_pointer_cast<IWindow>(shared_from_this()));
            p.sysInfoDialog->setCloseCallback(
                [this]
                {
                    _p->sysInfoDialog.reset();
                });
        }

        void MainWindow::setGeometry(const ftk::Box2I& value)
        {
            Window::setGeometry(value);
            _p->layout->setGeometry(value);
        }

        void MainWindow::keyPressEvent(ftk::KeyEvent& event)
        {
            FTK_P();
            event.accept = p.menuBar->shortcut(event.key, event.modifiers);
        }

        void MainWindow::keyReleaseEvent(ftk::KeyEvent& event)
        {
            event.accept = true;
        }

        void MainWindow::dropEvent(ftk::DragDropEvent& event)
        {
            FTK_P();
            event.accept = true;
            if (auto textData = std::dynamic_pointer_cast<ftk::DragDropTextData>(event.data))
            {
                if (auto app = p.app.lock())
                {
                    for (const auto& i : textData->getText())
                    {
                        app->open(ftk::Path(i));
                    }
                }
            }
        }

        void MainWindow::_settingsUpdate(const models::MouseSettings& settings)
        {
            FTK_P();
            p.timelineWidget->setMouseWheelScale(settings.wheelScale);
            p.timelineWidgetA->setMouseWheelScale(settings.wheelScale);
            p.timelineWidgetB->setMouseWheelScale(settings.wheelScale);
            p.viewport->setMouseWheelScale(settings.wheelScale);
            p.viewportA->setMouseWheelScale(settings.wheelScale);
            p.viewportB->setMouseWheelScale(settings.wheelScale);
        }

        void MainWindow::_updateTimelineDisplayOptions()
        {
            FTK_P();

            const auto settings = p.settingsModel->getTimeline();
            const auto app = p.app.lock();
            const bool splitMode =
                app &&
                models::DisplayMode::Dual == app->getFilesModel()->getDisplayMode();
            const int thumbnailHeight = getTimelineThumbnailSize(settings.thumbnailSize);
            const int paneThumbnailHeight =
                getPaneTimelineThumbnailSize(settings.thumbnailSize);
            const int waveformHeight = getTimelineWaveformSize(settings.thumbnailSize);
            const bool enableMasterThumbnails =
                settings.thumbnails && !splitMode;
            const bool enablePaneThumbnails =
                settings.thumbnails && splitMode;

            auto display = p.timelineWidget->getDisplayOptions();
            auto displayA = p.timelineWidgetA->getDisplayOptions();
            auto displayB = p.timelineWidgetB->getDisplayOptions();

            display.minimize = settings.minimize;
            // Master timeline: transport-only in dual mode; filmstrip in single mode.
            display.thumbnails = enableMasterThumbnails;
            display.thumbnailHeight = thumbnailHeight;
            display.waveformHeight = waveformHeight;

            displayA.minimize = true;
            displayA.thumbnails = enablePaneThumbnails;
            displayA.thumbnailHeight = paneThumbnailHeight;
            displayA.waveformHeight = waveformHeight;
            displayA.cacheDisplay = tl::ui::CacheDisplay::VideoOnly;

            displayB.minimize = true;
            displayB.thumbnails = enablePaneThumbnails;
            displayB.thumbnailHeight = paneThumbnailHeight;
            displayB.waveformHeight = waveformHeight;
            displayB.cacheDisplay = tl::ui::CacheDisplay::VideoOnly;

            auto applyTimelineDisplay = [](
                const std::shared_ptr<tl::ui::TimelineWidget>& widget,
                const tl::ui::DisplayOptions& options,
                bool paneTransportBar)
            {
                widget->setDisplayOptions(options);
                if (paneTransportBar)
                {
                    widget->setScrollBarsVisible(false);
                    widget->setFrameView(true);
                }
                widget->setSizeUpdate();
                if (widget->hasFrameView())
                {
                    widget->frameView();
                }
            };

            applyTimelineDisplay(p.timelineWidget, display, false);
            applyTimelineDisplay(p.timelineWidgetA, displayA, splitMode);
            applyTimelineDisplay(p.timelineWidgetB, displayB, splitMode);
        }

        void MainWindow::_settingsUpdate(const models::TimelineSettings& settings)
        {
            FTK_P();

            p.timelineWidget->setFrameView(settings.frameView);
            p.timelineWidget->setScrollBarsVisible(settings.scrollBars);
            p.timelineWidget->setAutoScroll(settings.autoScroll);
            p.timelineWidget->setStopOnScrub(settings.stopOnScrub);
            p.timelineWidgetA->setFrameView(settings.frameView);
            p.timelineWidgetA->setScrollBarsVisible(settings.scrollBars);
            p.timelineWidgetA->setAutoScroll(settings.autoScroll);
            p.timelineWidgetA->setStopOnScrub(settings.stopOnScrub);
            p.timelineWidgetB->setFrameView(settings.frameView);
            p.timelineWidgetB->setScrollBarsVisible(settings.scrollBars);
            p.timelineWidgetB->setAutoScroll(settings.autoScroll);
            p.timelineWidgetB->setStopOnScrub(settings.stopOnScrub);
            _updateTimelineDisplayOptions();

            if (settings.minimize)
            {
                if (p.splitter->getParent())
                {
                    p.splitter->setParent(nullptr);
                    p.splitter2->setParent(p.splitterLayout);
                    p.timelineWidget->setParent(p.splitterLayout);
                }
            }
            else
            {
                if (!p.splitter->getParent())
                {
                    p.splitter->setParent(p.splitterLayout);
                    p.splitter2->setParent(p.splitter);
                    p.timelineWidget->setParent(p.splitter);
                }
            }
        }

        void MainWindow::_settingsUpdate(const models::WindowSettings& settings)
        {
            FTK_P();

            p.fileToolBar->setVisible(settings.fileToolBar);
            p.dividers["File"]->setVisible(settings.fileToolBar);

            p.compareToolBar->setVisible(false);
            p.dividers["Compare"]->setVisible(false);

            p.windowToolBar->setVisible(settings.windowToolBar);
            p.dividers["Window"]->setVisible(settings.windowToolBar);

            p.viewToolBar->setVisible(settings.viewToolBar);
            p.dividers["View"]->setVisible(settings.viewToolBar);

            p.toolsToolBar->setVisible(settings.toolsToolBar);

            p.dividers["ToolBars"]->setVisible(
                settings.fileToolBar ||
                settings.windowToolBar ||
                settings.viewToolBar ||
                settings.toolsToolBar);

            p.tabBar->setVisible(false);

            const bool splitMode =
                models::DisplayMode::Dual == p.app.lock()->getFilesModel()->getDisplayMode();
            p.timelineWidget->setVisible(splitMode || settings.timeline);
            p.timelineWidgetA->setVisible(splitMode);
            p.timelineWidgetB->setVisible(splitMode);

            p.bottomToolBar->setVisible(settings.bottomToolBar);
            p.dividers["Bottom"]->setVisible(settings.bottomToolBar);

            p.statusBar->setVisible(settings.statusToolBar);
            p.dividers["Status"]->setVisible(settings.statusToolBar);

            p.splitter->setSplit(settings.splitter);
            p.splitter2->setSplit(settings.splitter2);
        }
    }
}

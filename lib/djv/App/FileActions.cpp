// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/FileActions.h>

#include <djv/App/App.h>
#include <djv/Models/FilesModel.h>

namespace djv
{
    namespace app
    {
        struct FileActions::Private
        {
            std::shared_ptr<ftk::ListObserver<std::shared_ptr<models::FilesModelItem> > > filesObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<models::FilesModelItem> > > aObserver;
            std::shared_ptr<ftk::ListObserver<std::shared_ptr<models::FilesModelItem> > > bObserver;
            std::shared_ptr<ftk::Observer<models::DisplayMode> > displayModeObserver;
        };

        void FileActions::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app)
        {
            IActions::_init(context, app, "File");
            FTK_P();

            auto appWeak = std::weak_ptr<App>(app);
            _actions["Open"] = ftk::Action::create(
                "打开视频",
                "FileOpen",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->openDialog();
                    }
                });

            _actions["OpenB"] = ftk::Action::create(
                "打开 B 视频",
                "FileOpen",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->openToBDialog();
                    }
                });

            _actions["OpenAudio"] = ftk::Action::create(
                "打开视频并指定音频",
                "FileOpenAudio",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->openSeparateAudioDialog();
                    }
                });

            _actions["SingleMode"] = ftk::Action::create(
                "单视频模式",
                "CompareA",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->setDisplayMode(models::DisplayMode::Single);
                    }
                });

            _actions["DualMode"] = ftk::Action::create(
                "双视频模式",
                "CompareHorizontal",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->setDisplayMode(models::DisplayMode::Dual);
                    }
                });

            _actions["Close"] = ftk::Action::create(
                "关闭当前视频",
                "FileClose",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->close();
                    }
                });

            _actions["CloseAll"] = ftk::Action::create(
                "关闭全部视频",
                "FileCloseAll",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->closeAll();
                    }
                });

            _actions["Reload"] = ftk::Action::create(
                "重新加载",
                "FileReload",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->reload();
                    }
                });

            _actions["Next"] = ftk::Action::create(
                "下一个 A 视频",
                "Next",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->next();
                    }
                });

            _actions["Prev"] = ftk::Action::create(
                "上一个 A 视频",
                "Prev",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->prev();
                    }
                });

            _actions["NextLayer"] = ftk::Action::create(
                "Next Layer",
                "Next",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->nextLayer();
                    }
                });

            _actions["PrevLayer"] = ftk::Action::create(
                "Previous Layer",
                "Prev",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getFilesModel()->prevLayer();
                    }
                });

            _actions["Exit"] = ftk::Action::create(
                "Exit",
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->exit();
                    }
                });

            _tooltips =
            {
                { "Open", "打开 A 视频。" },
                { "OpenB", "打开 B 视频并进入双视频模式。" },
                { "OpenAudio", "打开 A 视频并指定独立音频。" },
                { "SingleMode", "切换到单视频模式，默认显示 A 视频。" },
                { "DualMode", "切换到左右双视频模式。" },
                { "Close", "关闭当前视频。" },
                { "CloseAll", "关闭全部视频。" },
                { "Reload", "重新加载当前视频。" },
                { "Next", "切换到下一个 A 视频。" },
                { "Prev", "切换到上一个 A 视频。" },
                { "NextLayer", "Change to the next layer." },
                { "PrevLayer", "Change to the previous layer." },
                { "Exit", "Exit the application." }
            };

            _shortcutsUpdate(app->getSettingsModel()->getShortcuts());

            p.filesObserver = ftk::ListObserver<std::shared_ptr<models::FilesModelItem> >::create(
                app->getFilesModel()->observeFiles(),
                [this](const std::vector<std::shared_ptr<models::FilesModelItem> >& value)
                {
                    FTK_P();
                    _actions["Close"]->setEnabled(!value.empty());
                    _actions["CloseAll"]->setEnabled(!value.empty());
                    _actions["Reload"]->setEnabled(!value.empty());
                    _actions["Next"]->setEnabled(value.size() > 1);
                    _actions["Prev"]->setEnabled(value.size() > 1);
                });

            p.bObserver = ftk::ListObserver<std::shared_ptr<models::FilesModelItem> >::create(
                app->getFilesModel()->observeB(),
                [this](const std::vector<std::shared_ptr<models::FilesModelItem> >&)
                {
                    FTK_P();
                    _actions["DualMode"]->setEnabled(true);
                });

            p.displayModeObserver = ftk::Observer<models::DisplayMode>::create(
                app->getFilesModel()->observeDisplayMode(),
                [this](models::DisplayMode value)
                {
                    FTK_P();
                    _actions["SingleMode"]->setChecked(models::DisplayMode::Single == value);
                    _actions["DualMode"]->setChecked(models::DisplayMode::Dual == value);
                });

            p.aObserver = ftk::Observer<std::shared_ptr<models::FilesModelItem> >::create(
                app->getFilesModel()->observeA(),
                [this](const std::shared_ptr<models::FilesModelItem>& value)
                {
                    _actions["NextLayer"]->setEnabled(value ? value->videoLayers.size() > 1 : false);
                    _actions["PrevLayer"]->setEnabled(value ? value->videoLayers.size() > 1 : false);
                });
        }

        FileActions::FileActions() :
            _p(new Private)
        {}

        FileActions::~FileActions()
        {}

        std::shared_ptr<FileActions> FileActions::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app)
        {
            auto out = std::shared_ptr<FileActions>(new FileActions);
            out->_init(context, app);
            return out;
        }
    }
}

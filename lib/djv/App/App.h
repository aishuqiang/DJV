// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <tlRender/Timeline/Player.h>

#include <ftk/UI/App.h>

#include <filesystem>

namespace ftk
{
    class Settings;
    class SysLogModel;
}

namespace tl
{
#if defined(TLRENDER_BMD)
    namespace bmd
    {
        class OutputDevice;
    }
#endif // TLRENDER_BMD
}

namespace djv
{
    namespace models
    {
        struct FilesModelItem;

        class AppInfoModel;
        class AudioModel;
        class ColorModel;
        class FilesModel;
        class RecentFilesModel;
        class SettingsModel;
        class TimeUnitsModel;
        class ToolsModel;
        class ViewportModel;
#if defined(TLRENDER_BMD)
        class BMDDevicesModel;
#endif // TLRENDER_BMD
    }

    //! DJV Application
    namespace app
    {
        class MainWindow;

        //! Application.
        class App : public ftk::App
        {
            FTK_NON_COPYABLE(App);

        public:
            enum class SplitPane
            {
                A,
                B
            };

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                std::vector<std::string>&,
                const std::shared_ptr<models::AppInfoModel>&);

            App();

        public:
            ~App();

            //! Create a new application.
            static std::shared_ptr<App> create(
                const std::shared_ptr<ftk::Context>&,
                std::vector<std::string>&,
                const std::shared_ptr<models::AppInfoModel>& = nullptr);

            //! Get the application information model.
            const std::shared_ptr<models::AppInfoModel>& getAppInfoModel() const;

            //! Get the settings.
            const std::shared_ptr<ftk::Settings>& getSettings() const;

            //! Get the settings model.
            const std::shared_ptr<models::SettingsModel>& getSettingsModel() const;

            //! Get the system log model.
            const std::shared_ptr<ftk::SysLogModel>& getSysLogModel() const;

            //! Get the time units model.
            const std::shared_ptr<models::TimeUnitsModel>& getTimeUnitsModel() const;

            //! Get the files model.
            const std::shared_ptr<models::FilesModel>& getFilesModel() const;

            //! Get the recent files model.
            const std::shared_ptr<models::RecentFilesModel>& getRecentFilesModel() const;

            //! Get the color model.
            const std::shared_ptr<models::ColorModel>& getColorModel() const;

            //! Get the viewport model.
            const std::shared_ptr<models::ViewportModel>& getViewportModel() const;

            //! Get the audio model.
            const std::shared_ptr<models::AudioModel>& getAudioModel() const;

            //! Get the tools model.
            const std::shared_ptr<models::ToolsModel>& getToolsModel() const;

#if defined(TLRENDER_BMD)
            //! Get the BMD devices model.
            const std::shared_ptr<models::BMDDevicesModel>& getBMDDevicesModel() const;

            //! Get the BMD output device.
            const std::shared_ptr<tl::bmd::OutputDevice>& getBMDOutputDevice() const;
#endif // TLRENDER_BMD

            //! Open a file.
            void open(
                const ftk::Path& path,
                const ftk::Path& audioPath = ftk::Path());

            //! Open a file explicitly as the A input.
            void openToA(
                const ftk::Path& path,
                const ftk::Path& audioPath = ftk::Path());

            //! Open a file explicitly as the B input.
            void openToB(const ftk::Path& path);

            //! Open a file as the compare/B input.
            void openCompare(const ftk::Path& path);

            //! Open a file dialog.
            void openDialog();

            //! Open an A file dialog.
            void openToADialog();

            //! Open a compare/B file dialog.
            void openCompareDialog();

            //! Open a B file dialog.
            void openToBDialog();

            //! Open a file and separate audio file dialog.
            void openSeparateAudioDialog();

            //! Reload the active files.
            void reload();

            //! Observe the timeline player.
            std::shared_ptr<ftk::IObservable<std::shared_ptr<tl::Player> > > observePlayer() const;

            //! Observe the A timeline player used by split compare layouts.
            std::shared_ptr<ftk::IObservable<std::shared_ptr<tl::Player> > > observePlayerA() const;

            //! Observe the B timeline player used by split compare layouts.
            std::shared_ptr<ftk::IObservable<std::shared_ptr<tl::Player> > > observePlayerB() const;

            //! Global transport controls. In split compare mode these control
            //! the master transport and both A/B players together.
            void transportStop();
            void transportForward();
            void transportReverse();
            void transportTogglePlayback();
            void transportTimeAction(tl::TimeAction);
            void transportSeek(const OTIO_NS::RationalTime&);
            void transportSetLoop(tl::Loop);
            void transportSetSpeed(double);
            void transportSetSpeedMult(double);

            //! Split-pane transport controls. In synchronized mode these are
            //! routed to the shared transport; in independent mode they only
            //! affect the addressed pane.
            void setSplitActivePane(SplitPane);
            void splitStop(SplitPane);
            void splitForward(SplitPane);
            void splitReverse(SplitPane);
            void splitSeek(SplitPane, const OTIO_NS::RationalTime&);
            void splitSetSpeed(SplitPane, double);
            void splitSetForwardStart(SplitPane);
            void splitSetReverseStart(SplitPane);

            //! Get the main window.
            const std::shared_ptr<MainWindow>& getMainWindow() const;

            //! Observe whether the secondary window is active.
            std::shared_ptr<ftk::IObservable<bool> > observeSecondaryWindow() const;

            //! Set whether the secondary window is active.
            void setSecondaryWindow(bool);

            //! Print the version and exit.
            bool hasPrintVersion() const;

            void run() override;

        private:
            void _modelsInit();
            void _devicesInit();
            void _observersInit();
            void _inputFilesInit();
            void _windowsInit();

            std::filesystem::path _appDocsPath();
            std::filesystem::path _getLogFilePath();
            std::filesystem::path _getSettingsPath();

            void _filesUpdate(const std::vector<std::shared_ptr<models::FilesModelItem> >&);
            void _activeUpdate(const std::vector<std::shared_ptr<models::FilesModelItem> >&);
            void _layersUpdate(const std::vector<int>&);
            void _viewUpdate(const ftk::V2I& pos, double zoom, bool frame);
            void _audioUpdate();

            FTK_PRIVATE();
        };
    }
}

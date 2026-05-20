// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <tlRender/UI/Viewport.h>

#include <array>
#include <vector>

namespace djv
{
    namespace app
    {
        class App;

        //! Viewport.
        class Viewport : public tl::ui::Viewport
        {
            FTK_NON_COPYABLE(Viewport);

        public:
            enum class Role
            {
                Primary,
                SplitA,
                SplitB
            };

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                Role,
                const std::shared_ptr<IWidget>& parent);

            Viewport();

        public:
            virtual ~Viewport();

            static std::shared_ptr<Viewport> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                Role = Role::Primary,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! Observe picking.
            TL_API std::shared_ptr<ftk::IObservable<ftk::V2I> > observePick() const;

            //! Observe the sample position.
            TL_API std::shared_ptr<ftk::IObservable<ftk::V2I> > observeSamplePos() const;

            //! Observe the color sample.
            TL_API std::shared_ptr<ftk::IObservable<ftk::Color4F> > observeColorSample() const;

            void setPlayer(const std::shared_ptr<tl::Player>&) override;

            ftk::Size2I getSizeHint() const override;
            void setGeometry(const ftk::Box2I&) override;
            void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            void mouseMoveEvent(ftk::MouseMoveEvent&) override;
            void mousePressEvent(ftk::MouseClickEvent&) override;
            void mouseReleaseEvent(ftk::MouseClickEvent&) override;
            void dropEvent(ftk::DragDropEvent&) override;

        private:
            enum class PaneState
            {
                Hidden,
                Empty,
                Loading
            };

            bool _isDualEmptyState() const;
            std::array<PaneState, 2> _getPaneStates() const;
            std::vector<ftk::Box2I> _getEmptyStateBoxes(const ftk::Box2I&) const;
            size_t _getPaneIndex(const ftk::V2I&) const;
            void _drawDualDivider(const ftk::Box2I&, const ftk::DrawEvent&) const;
            void _layoutEmptyState(const ftk::Box2I&);
            void _emptyStateUpdate();
            void _videoUpdate();
            void _hudUpdate();

            FTK_PRIVATE();
        };
    }
}

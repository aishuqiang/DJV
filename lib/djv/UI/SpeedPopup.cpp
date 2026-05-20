// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/SpeedPopup.h>

#include <ftk/UI/Divider.h>
#include <ftk/UI/DoubleEdit.h>
#include <ftk/UI/ListItemsWidget.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/Core/Format.h>

namespace djv
{
    namespace ui
    {
        struct SpeedPopup::Private
        {
            std::vector<double> speeds;
            std::vector<std::string> labels;
            std::shared_ptr<ftk::ListItemsWidget> listWidget;
            std::shared_ptr<ftk::DoubleEdit> speedEdit;
            std::function<void(double)> callback;
        };

        void SpeedPopup::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<ftk::DoubleModel>& model,
            double defaultSpeed,
            const std::shared_ptr<IWidget>& parent)
        {
            IWidgetPopup::_init(
                context,
                "djv::ui::SpeedPopup",
                parent);
            FTK_P();

            p.speeds.push_back(defaultSpeed);
            p.labels.push_back("1x");
            for (int i = 2; i <= 3; ++i)
            {
                p.speeds.push_back(defaultSpeed * i);
                p.labels.push_back(ftk::Format("{0}x").arg(i));
            }
            for (int i = 2; i <= 3; ++i)
            {
                p.speeds.push_back(defaultSpeed / i);
                p.labels.push_back(ftk::Format("1/{0}x").arg(i));
            }

            p.listWidget = ftk::ListItemsWidget::create(context, ftk::ButtonGroupType::Click);

            p.speedEdit = ftk::DoubleEdit::create(context, model);

            auto layout = ftk::VerticalLayout::create(context);
            layout->setSpacingRole(ftk::SizeRole::None);
            p.listWidget->setParent(layout);
            ftk::Divider::create(context, ftk::Orientation::Vertical, layout);
            auto hLayout = ftk::HorizontalLayout::create(context, layout);
            hLayout->setMarginRole(ftk::SizeRole::MarginSmall);
            p.speedEdit->setParent(hLayout);
            setWidget(layout);

            _widgetUpdate();

            auto weak = std::weak_ptr<SpeedPopup>(std::dynamic_pointer_cast<SpeedPopup>(shared_from_this()));
            p.listWidget->setCallback(
                [weak](int index, bool value)
                {
                    if (auto widget = weak.lock())
                    {
                        if (value && index >= 0 && index < widget->_p->speeds.size())
                        {
                            if (widget->_p->callback)
                            {
                                widget->_p->callback(widget->_p->speeds[index]);
                            }
                        }
                    }
                });
        }

        SpeedPopup::SpeedPopup() :
            _p(new Private)
        {}

        SpeedPopup::~SpeedPopup()
        {}

        std::shared_ptr<SpeedPopup> SpeedPopup::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<ftk::DoubleModel>& model,
            double defaultSpeed,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<SpeedPopup>(new SpeedPopup);
            out->_init(context, model, defaultSpeed, parent);
            return out;
        }

        void SpeedPopup::setCallback(const std::function<void(double)>& value)
        {
            _p->callback = value;
        }

        void SpeedPopup::open(
            const std::shared_ptr<ftk::IWindow>& window,
            const ftk::Box2I& buttonGeometry,
            const std::optional<ftk::Box2I>& widgetGeometry)
        {
            IWidgetPopup::open(window, buttonGeometry, widgetGeometry);
            _p->speedEdit->takeKeyFocus();
        }

        void SpeedPopup::_widgetUpdate()
        {
            FTK_P();
            std::vector<std::string> items;
            const size_t size = p.speeds.size();
            for (size_t i = 0; i < size; ++i)
            {
                items.push_back(p.labels[i]);
            }
            p.listWidget->setItems(items);
        }
    }
}

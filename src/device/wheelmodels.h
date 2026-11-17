// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace Wheelstar::Device
{
    /// How a wheel reports its pedals. Like upstream Oversteer, Wheelstar expects steering on ABS_X, throttle on ABS_Z,
    /// brake on ABS_RZ and clutch on ABS_Y; the models that do it differently are remapped by remapAxis().
    enum class AxisLayout : std::uint8_t
    {
        Standard,
        /// Throttle on ABS_Y, brake on ABS_Z, clutch on ABS_RZ (the older Logitech wheels, G920, G923 Xbox).
        Shifted,
        /// Separate wheel/gas/brake codes, and the paddles on the gear buttons (Wingman Formula Force).
        WingmanFormulaForce,
        /// Pedals on ABS_RX/RY/RZ, reporting pressed as high rather than low (G PRO).
        GPro,
    };

    struct WheelModel
    {
        std::uint16_t vendorId;
        std::uint16_t productId;
        int maxRange;
        AxisLayout layout = AxisLayout::Standard;
    };

    inline constexpr std::uint16_t VendorLogitech = 0x046d;

    /// The Logitech wheels upstream Oversteer knows. The other vendors' entries were left out on purpose:
    /// this port targets the new-lg4ff driver.
    inline constexpr std::array KnownWheels{
        // Driving Force / Formula EX
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc294, .maxRange = 270, .layout = AxisLayout::Shifted},
        // Driving Force GT
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc29a, .maxRange = 900, .layout = AxisLayout::Shifted},
        // Driving Force Pro
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc298, .maxRange = 900, .layout = AxisLayout::Shifted},
        // G25
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc299, .maxRange = 900},
        // G27
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc29b, .maxRange = 900},
        // G29
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc24f, .maxRange = 900},
        // G920
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc262, .maxRange = 900, .layout = AxisLayout::Shifted},
        // G923 PlayStation/PC
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc266, .maxRange = 900},
        // G923 Xbox/PC
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc26e, .maxRange = 900, .layout = AxisLayout::Shifted},
        // G PRO PlayStation/PC
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc268, .maxRange = 1080, .layout = AxisLayout::GPro},
        // G PRO Xbox/PC
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc272, .maxRange = 1080, .layout = AxisLayout::GPro},
        // Momo Force
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc295, .maxRange = 270, .layout = AxisLayout::Shifted},
        // Momo Racing
        WheelModel{.vendorId = VendorLogitech, .productId = 0xca03, .maxRange = 270, .layout = AxisLayout::Shifted},
        // Speed Force Wireless
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc29c, .maxRange = 270, .layout = AxisLayout::Shifted},
        // Wingman Formula Force
        WheelModel{.vendorId = VendorLogitech,
                   .productId = 0xc291,
                   .maxRange = 180,
                   .layout = AxisLayout::WingmanFormulaForce},
        // Wingman Formula Force GP
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc293, .maxRange = 180, .layout = AxisLayout::Shifted},
        // Wingman Formula GP
        WheelModel{.vendorId = VendorLogitech, .productId = 0xc20e, .maxRange = 180, .layout = AxisLayout::Shifted},
    };

    constexpr const WheelModel* findWheelModel(std::uint16_t vendorId, std::uint16_t productId)
    {
        const auto* found =
            std::ranges::find_if(KnownWheels, [=](const WheelModel& model)
                                 { return model.vendorId == vendorId && model.productId == productId; });

        return found == KnownWheels.end() ? nullptr : found;
    }

    /// The axis code `code` stands for in Oversteer's layout.
    constexpr int remapAxis(AxisLayout layout, int code)
    {
        switch (layout)
        {
            case AxisLayout::Standard:
                return code;
            case AxisLayout::Shifted:
                switch (code)
                {
                    case ABS_Y:
                        return ABS_Z;
                    case ABS_Z:
                        return ABS_RZ;
                    case ABS_RZ:
                        return ABS_Y;
                    default:
                        return code;
                }
            case AxisLayout::WingmanFormulaForce:
                switch (code)
                {
                    case ABS_WHEEL:
                        return ABS_X;
                    case ABS_GAS:
                        return ABS_Z;
                    case ABS_BRAKE:
                        return ABS_RZ;
                    default:
                        return code;
                }
            case AxisLayout::GPro:
                switch (code)
                {
                    case ABS_RX:
                        return ABS_Z;
                    case ABS_RY:
                        return ABS_RZ;
                    case ABS_RZ:
                        return ABS_Y;
                    default:
                        return code;
                }
        }

        return code;
    }

    /// Whether a released pedal reports the axis maximum, as every Logitech pedal set except the G PRO's does.
    constexpr bool pedalsInverted(AxisLayout layout)
    {
        return layout != AxisLayout::GPro;
    }

    /// The button index a key code is shown as; -1 for keys that are not wheel buttons. Kept identical to upstream,
    /// so the button assignments in a shared config.ini mean the same thing in both programs.
    constexpr int buttonIndex(AxisLayout layout, int code)
    {
        if (layout == AxisLayout::WingmanFormulaForce && (code == BTN_GEAR_DOWN || code == BTN_GEAR_UP))
        {
            code = code - BTN_GEAR_DOWN + BTN_TRIGGER;
        }

        if (code >= BTN_JOYSTICK && code <= BTN_DEAD)
        {
            return code - BTN_JOYSTICK;
        }

        if (code >= BTN_GAMEPAD && code <= BTN_MODE)
        {
            return code - BTN_GAMEPAD;
        }

        if (code >= BTN_TRIGGER_HAPPY1 && code <= BTN_TRIGGER_HAPPY12)
        {
            return code - BTN_TRIGGER_HAPPY1 + 16;
        }

        return -1;
    }

    static_assert(findWheelModel(VendorLogitech, 0xc266)->maxRange == 900);
    static_assert(findWheelModel(VendorLogitech, 0x0001) == nullptr);
    static_assert(remapAxis(AxisLayout::Shifted, ABS_Y) == ABS_Z);
    static_assert(buttonIndex(AxisLayout::Standard, BTN_TRIGGER_HAPPY1) == 16);
    static_assert(buttonIndex(AxisLayout::Standard, KEY_A) == -1);
} // namespace Wheelstar::Device

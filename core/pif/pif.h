#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

class Pif
{
public:
    struct ControllerState
    {
        bool connected = false;
        std::uint16_t buttons = 0;
        std::int8_t stickX = 0;
        std::int8_t stickY = 0;
    };

    void setControllerConnected(std::size_t port, bool connected);
    void setControllerState(std::size_t port, std::uint16_t buttons,
                           std::int8_t stickX, std::int8_t stickY);
    void processJoybus(std::vector<std::uint8_t> &ram) const;

private:
    std::array<ControllerState, 4> controllers_{};
};

#include "core/pif/pif.h"

#include <algorithm>
#include <cstddef>

void Pif::setControllerConnected(std::size_t port, bool connected)
{
    if (port < controllers_.size())
        controllers_[port].connected = connected;
}

void Pif::setControllerState(std::size_t port, std::uint16_t buttons,
                             std::int8_t stickX, std::int8_t stickY)
{
    if (port >= controllers_.size())
        return;

    controllers_[port].buttons = buttons;
    controllers_[port].stickX = stickX;
    controllers_[port].stickY = stickY;
}

void Pif::processJoybus(std::vector<std::uint8_t> &ram) const
{
    // The final byte is the PIF control byte. Bit 0 requests Joybus execution.
    if (ram.size() < 64 || (ram[63] & 0x01) == 0)
        return;

    std::size_t offset = 0;
    std::size_t channel = 0;

    while (offset < 63)
    {
        const std::uint8_t marker = ram[offset];
        if (marker == 0xFE)
            break;
        if (marker == 0x00 || marker == 0xFF)
        {
            ++offset;
            ++channel;
            continue;
        }

        if (offset + 2 > 63)
            break;

        const std::size_t txLength = ram[offset] & 0x3F;
        const std::size_t rxLength = ram[offset + 1] & 0x3F;
        const std::size_t commandOffset = offset + 2;
        const std::size_t responseOffset = commandOffset + txLength;

        if (txLength == 0 || commandOffset + txLength > 63 ||
            responseOffset + rxLength > 63)
        {
            ram[offset + 1] |= 0xC0;
            break;
        }

        const std::uint8_t command = ram[commandOffset];
        const bool hasController = channel < controllers_.size() &&
                                   controllers_[channel].connected;

        if (!hasController)
        {
            // The high bit in the receive-length byte reports no device.
            ram[offset + 1] |= 0x80;
        }
        else if (command == 0x00 && rxLength >= 3)
        {
            // Standard N64 controller identification: gamepad, no Controller Pak.
            ram[responseOffset] = 0x05;
            ram[responseOffset + 1] = 0x00;
            ram[responseOffset + 2] = 0x02;
        }
        else if (command == 0x01 && rxLength >= 4)
        {
            const ControllerState &controller = controllers_[channel];
            ram[responseOffset] = static_cast<std::uint8_t>(controller.buttons >> 8);
            ram[responseOffset + 1] = static_cast<std::uint8_t>(controller.buttons);
            ram[responseOffset + 2] = static_cast<std::uint8_t>(controller.stickX);
            ram[responseOffset + 3] = static_cast<std::uint8_t>(controller.stickY);
        }
        else
        {
            // Unsupported commands and undersized replies are reported as errors.
            ram[offset + 1] |= 0x40;
        }

        offset = responseOffset + rxLength;
        ++channel;
    }

    // Hardware clears the execute request after handling the buffer.
    ram[63] &= static_cast<std::uint8_t>(~0x01);
}

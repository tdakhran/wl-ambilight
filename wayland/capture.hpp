#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace wl_ambilight
{
class Capture_t
{
public:
    using Callback_t = std::function<void(uint32_t Width, uint32_t Height, uint32_t Format, uint8_t const *Data)>;

    explicit Capture_t(std::string const &DisplayID);

    bool capture(Callback_t const &Callback);

    ~Capture_t();

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

}    // namespace wl_ambilight

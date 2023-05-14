#include "capture.hpp"

#include "wayland-client-export-dmabuf.hpp"

#include <cstring>
#include <fcntl.h>
#include <future>
#include <iostream>
#include <memory>
#include <queue>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include "gbm.h"

namespace wl_ambilight
{

using std::unique_ptr;

class Capture_t::Impl
{
public:
    explicit Impl(std::string const &DisplayID)
    {
        mCDisplay = wl_display_connect(nullptr);
        if (!mCDisplay)
            throw std::runtime_error("Failed to connect to wayland display");

        mDisplay              = std::make_unique<wayland::display_t>(mCDisplay);
        mRegistry             = mDisplay->get_registry();
        mRegistry.on_global() = [&](uint32_t Name, std::string const &Interface, uint32_t Version)
        {
            if (Interface == wayland::output_t::interface_name)
            {
                mOutputs.emplace_back();
                mRegistry.bind(Name, mOutputs.back(), Version);
            }
            if (Interface == decltype(mDmabufManager)::interface_name)
            {
                mRegistry.bind(Name, mDmabufManager, Version);
            }
        };

        mDisplay->roundtrip();

        if (!mDmabufManager)
            throw std::runtime_error("Interface not supported: " + decltype(mDmabufManager)::interface_name);

        if (mOutputs.empty())
            throw std::runtime_error("Failed to find outputs");

        for (auto &&Output : mOutputs)
        {
            Output.on_description() = [&](std::string const &Description)
            {
                if (Description.find(DisplayID) != std::string::npos)
                {
                    mOutput = &Output;
                }
                std::cout << "Found output with description: " << Description << std::endl;
            };
        }
        mDisplay->roundtrip();

        if (!mOutput)
            throw std::runtime_error("Requested output not found");

        std::cout << "Using output: " << DisplayID << std::endl;

        mDrmFd = open(RenderNode, O_RDWR);
        if (mDrmFd < 0)
            throw std::runtime_error("Failed to open drm render node");

        mGBMDevice = gbm_create_device(mDrmFd);
        if (!mGBMDevice)
            throw std::runtime_error("Failed to create gbm device");
    }

    ~Impl()
    {
        gbm_device_destroy(mGBMDevice);
        close(mDrmFd);
    }

    bool capture(Callback_t const &Callback)
    {
        gbm_import_fd_modifier_data Data;
        for (auto &Fd : Data.fds)
            Fd = -1;

        bool FrameReadyForProcessing = false;
        auto Frame                   = mDmabufManager.capture_output(0, *mOutput);
        if (!Frame)
            throw std::runtime_error("Failed to capture frame");

        Frame.on_frame() = [&](uint32_t Width,
                               uint32_t Height,
                               uint32_t,
                               uint32_t,
                               uint32_t,
                               wayland::zwlr_export_dmabuf_frame_v1_flags,
                               uint32_t Format,
                               uint32_t ModHigh,
                               uint32_t ModLow,
                               uint32_t NumObject)
        {
            Data.width    = Width;
            Data.height   = Height;
            Data.format   = Format;
            Data.num_fds  = NumObject;
            Data.modifier = (static_cast<uint64_t>(ModHigh) << 32ULL) | static_cast<uint64_t>(ModLow);
        };
        Frame.on_object() =
            [&](uint32_t Index, int Fd, uint32_t Size, uint32_t Offset, uint32_t Stride, uint32_t PlaneIndex)
        {
            Data.fds[PlaneIndex]     = Fd;
            Data.strides[PlaneIndex] = Stride;
            Data.offsets[PlaneIndex] = Offset;
        };
        Frame.on_ready() = [&](uint32_t, uint32_t, uint32_t)
        {
            std::lock_guard<std::mutex> Lock(mFrameMutex);
            FrameReadyForProcessing = true;
        };
        Frame.on_cancel() = [&](wayland::zwlr_export_dmabuf_frame_v1_cancel_reason Reason)
        {
            switch (Reason)
            {
            case wayland::zwlr_export_dmabuf_frame_v1_cancel_reason::permanent:
            {
                std::cerr << "Reason: "
                          << "permanent" << std::endl;
                throw std::runtime_error("Error processing frame");
            }
            case wayland::zwlr_export_dmabuf_frame_v1_cancel_reason::temporary:
            {
                std::cerr << "Reason: "
                          << "temporary" << std::endl;
            }
            case wayland::zwlr_export_dmabuf_frame_v1_cancel_reason::resizing:
            {
                std::cerr << "Reason: "
                          << "resizing" << std::endl;
            }
            }

            std::lock_guard<std::mutex> Lock(mFrameMutex);
            FrameReadyForProcessing = true;
        };

        mDisplay->dispatch();

        std::unique_lock<std::mutex> Lock(mFrameMutex);
        mFrameCv.wait(Lock, [&FrameReadyForProcessing] { return FrameReadyForProcessing; });

        // here we have planes
        uint32_t Stride = 0;
        void    *MapData { nullptr };
        auto    *Bo     = gbm_bo_import(mGBMDevice, GBM_BO_IMPORT_FD_MODIFIER, &Data, GBM_BO_USE_SCANOUT);
        void    *Buffer = gbm_bo_map(Bo, 0, 0, Data.width, Data.height, GBM_BO_TRANSFER_READ, &Stride, &MapData);

        if (!Buffer)
            throw std::runtime_error("Failed to map gbm bo");

        if (Buffer)
        {
            Callback(Data.width, Data.height, Data.format, reinterpret_cast<uint8_t *>(Buffer));
        }

        gbm_bo_unmap(Bo, MapData);
        gbm_bo_destroy(Bo);

        for (auto &Fd : Data.fds)
        {
            if (Fd >= 0)
                close(Fd);
        }

        return Buffer != nullptr;
    }

private:
    struct gbm_device           *mGBMDevice { nullptr };
    int                          mDrmFd { -1 };
    static constexpr char const *RenderNode = "/dev/dri/renderD128";

private:
    std::chrono::milliseconds mPeriod { 16 };

    std::vector<wayland::output_t> mOutputs;
    wayland::output_t             *mOutput { nullptr };

    wayland::zwlr_export_dmabuf_manager_v1_t mDmabufManager;

    wl_display                    *mCDisplay = nullptr;
    unique_ptr<wayland::display_t> mDisplay;
    wayland::registry_t            mRegistry;

    std::future<void> mFuture;

    std::mutex              mFrameMutex;
    std::condition_variable mFrameCv;
};

Capture_t::Capture_t(std::string const &DisplayID) : mImpl(new Capture_t::Impl(DisplayID))
{
}

Capture_t::~Capture_t()
{
}

bool Capture_t::capture(Callback_t const &Callback)
{
    return mImpl->capture(Callback);
}

}    // namespace wl_ambilight

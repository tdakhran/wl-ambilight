#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <termios.h>
#include <cerrno>
#include <unistd.h>

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "arduino/led_layout.hpp"
#include "wayland/capture.hpp"

std::array<uint8_t, sizeof(SerialDataHeader) + sizeof(SerialDataFooter) + 3 * LedsTotal> SerialData;

void computeColors(uint32_t Width, uint32_t Height, uint32_t const *Img)
{
    constexpr float DepthPercentFromSize = 0.02;
    size_t          Depth                = DepthPercentFromSize * (Width + Height);    // in pixels

    auto *It = SerialData.begin() + sizeof(SerialDataHeader);

    auto ComputeAverageAndPush =
        [&](size_t const RowStart, size_t const RowCount, size_t const ColStart, size_t const ColCount)
    {
        uint32_t R           = 0;
        uint32_t G           = 0;
        uint32_t B           = 0;
        uint32_t TotalPixels = 0;
        for (size_t Row = RowStart; Row < RowStart + RowCount; ++Row)
        {
            for (size_t Col = ColStart; Col < ColStart + ColCount; ++Col)
            {
                auto Pixel = Img[Row * Width + Col];
                R += (Pixel >> 0) & 0xFF;
                G += (Pixel >> 8) & 0xFF;
                B += (Pixel >> 16) & 0xFF;
                ++TotalPixels;
            }
        }

        if (TotalPixels)
        {
            R /= TotalPixels;
            G /= TotalPixels;
            B /= TotalPixels;
        }

        *It++ = R;
        *It++ = G;
        *It++ = B;
    };

    size_t const HorizontalPixelsPerLed = Width / (LedsTop + 2);
    static_assert(LedsLeft == LedsRight, "");
    static_assert(LedsBottomLeft == LedsBottomRight, "");
    size_t const VerticalPixelsPerLed = Height / (LedsLeft + 2);

    // bottom right, left to right
    for (size_t Id = 0; Id < LedsBottomRight; ++Id)
    {
        ComputeAverageAndPush(Height - Depth,
                              Depth,
                              Width - HorizontalPixelsPerLed * (LedsBottomRight + 1 - Id),
                              HorizontalPixelsPerLed);
    }

    // right, bottom to top
    for (size_t Id = 0; Id < LedsRight; ++Id)
    {
        ComputeAverageAndPush(Height - VerticalPixelsPerLed * (2 + Id), VerticalPixelsPerLed, Width - Depth, Depth);
    }

    // top, right to left
    for (size_t Id = 0; Id < LedsTop; ++Id)
    {
        ComputeAverageAndPush(0, Depth, Width - HorizontalPixelsPerLed * (2 + Id), HorizontalPixelsPerLed);
    }

    // left, top to bottom
    for (size_t Id = 0; Id < LedsLeft; ++Id)
    {
        ComputeAverageAndPush((Id + 1) * VerticalPixelsPerLed, VerticalPixelsPerLed, 0, Depth);
    }

    // bottom left, left to right
    for (size_t Id = 0; Id < LedsBottomLeft; ++Id)
    {
        ComputeAverageAndPush(Height - Depth, Depth, (Id + 1) * HorizontalPixelsPerLed, HorizontalPixelsPerLed);
    }
};

int openAndConfigureSerial(std::string const &Device)
{
    int SerialPort = open(Device.c_str(), O_WRONLY);
    if (SerialPort < 0)
    {
        printf("Error %i from open: %s\n", errno, strerror(errno));
        return -1;
    }

    struct termios TTY;
    if (tcgetattr(SerialPort, &TTY) != 0)
    {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        close(SerialPort);
        return -1;
    }

    TTY.c_cflag &= ~PARENB;
    TTY.c_cflag &= ~CSTOPB;
    TTY.c_cflag |= CS8;
    TTY.c_cflag &= ~CRTSCTS;
    TTY.c_lflag &= ~ISIG;
    TTY.c_iflag &= ~(IXON | IXOFF | IXANY);
    TTY.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    TTY.c_oflag &= ~OPOST;
    TTY.c_oflag &= ~ONLCR;
    cfsetispeed(&TTY, B230400);
    if (tcsetattr(SerialPort, TCSANOW, &TTY) != 0)
    {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        close(SerialPort);
        return -1;
    }

    return SerialPort;
}

void usage()
{
    std::cerr << "Usage: {} -o DP-3 -d /dev/ttyUSB2" << std::endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    std::string SerialDevice;
    std::string Output;

    for (;;)
    {
        switch (getopt(argc, argv, "d:o:"))
        {
        case 'o': Output = optarg; continue;
        case 'd': SerialDevice = optarg; continue;
        case 'h':
        default: usage();
        case -1: break;
        }
        break;
    }

    if (SerialDevice.empty() or Output.empty())
        usage();

    std::cout << "Received output argument: " << Output << std::endl;
    std::cout << "Received serial argument: " << SerialDevice << std::endl;

    constexpr std::chrono::milliseconds UpdatePeriodMs(33);

    wl_ambilight::Capture_t CaptureService(Output);
    int                     Serial = openAndConfigureSerial(SerialDevice);

    if (Serial < 0)
    {
        printf("Failed to open and configure serial port %s\n", SerialDevice.c_str());
        exit(EXIT_FAILURE);
    }

    if (flock(Serial, LOCK_EX | LOCK_NB) == -1)
    {
        printf("Failed to lock SerialPort %s\n", SerialDevice.c_str());
        close(Serial);
        exit(EXIT_FAILURE);
    }

    // populate header and footer
    std::copy(SerialDataHeader, SerialDataHeader + sizeof(SerialDataHeader), SerialData.begin());
    std::copy(SerialDataFooter,
              SerialDataFooter + sizeof(SerialDataFooter),
              std::prev(SerialData.end(), sizeof(SerialDataFooter)));

    std::cout << "Starting service..." << std::endl;
    // periodically update leds
    std::chrono::steady_clock::time_point LastUpdate(std::chrono::steady_clock::now());
    while (true)
    {
        std::this_thread::sleep_until(LastUpdate + UpdatePeriodMs);
        LastUpdate += UpdatePeriodMs;

        CaptureService.capture(
            [&](uint32_t Width, uint32_t Height, uint32_t, uint8_t const *Data)
            {
                computeColors(Width, Height, reinterpret_cast<uint32_t const *>(Data));
                write(Serial, SerialData.data(), SerialData.size());
            });
    };

    close(Serial);
}

#pragma once

// Layout of leds is
//
// ***********************
// *                     *
// *                     *
// *       Screen        *
// *                     *
// *                     *
// ****E             S****
//
// All are connected into single stripe and addressed counter clockwise.

constexpr unsigned LedsBottomRight = 6;
constexpr unsigned LedsRight       = 19;
constexpr unsigned LedsTop         = 35;
constexpr unsigned LedsLeft        = 19;
constexpr unsigned LedsBottomLeft  = 6;

constexpr unsigned LedsTotal = LedsBottomRight + LedsRight + LedsTop + LedsLeft + LedsBottomLeft;

// serial data is sent as |Header|RGBRGBRGB....|Footer|
constexpr char const *SerialDataHeader = "WAMB";
constexpr char const *SerialDataFooter = "BMAW";

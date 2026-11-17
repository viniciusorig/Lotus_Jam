#pragma once
#include <vector>

#include "audio/pipewire_backend.h"

// Default device choice (Task 12 wraps these with the persisted per-user choice).
// Input: the first device whose name or description contains "PCM2902" or
// "USB_PnP_Sound_Device" (the bass interface), else the first one.
// Output: the first device whose name or description contains "HDMI", else the first one.
// -1 when the list is empty.
int defaultInputIndex(const std::vector<AudioDevice>& inputs);
int defaultOutputIndex(const std::vector<AudioDevice>& outputs);

struct DeviceLists {
    std::vector<AudioDevice> inputs, outputs;
};

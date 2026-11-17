#include "ui/device_select.h"

#include <initializer_list>
#include <string>

namespace {

int firstMatch(const std::vector<AudioDevice>& devs, std::initializer_list<const char*> keys) {
    if (devs.empty()) return -1;
    for (size_t i = 0; i < devs.size(); ++i)
        for (const char* k : keys)
            if (devs[i].name.find(k) != std::string::npos || devs[i].description.find(k) != std::string::npos)
                return int(i);
    return 0;
}

}  // namespace

int defaultInputIndex(const std::vector<AudioDevice>& inputs) {
    return firstMatch(inputs, {"PCM2902", "USB_PnP_Sound_Device"});
}

int defaultOutputIndex(const std::vector<AudioDevice>& outputs) { return firstMatch(outputs, {"HDMI"}); }

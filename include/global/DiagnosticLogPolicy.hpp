#pragma once

#include <string_view>

namespace Logging::Policy {
    // These are peer/client disconnects routinely produced by browsers and the
    // Windows TCP stack. They are useful once, but thousands of identical copies
    // hide actual DNS, routing, protocol and startup failures.
    inline bool IsCollapsibleTransportNoise(std::string_view line) {
        const bool connectionClose = line.find("connection upload closed:") != std::string_view::npos
                                  || line.find("connection download closed:") != std::string_view::npos;
        if (!connectionClose) return false;
        return line.find("An existing connection was forcibly closed by the remote host") != std::string_view::npos
            || line.find("An established connection was aborted by the software in your host machine") != std::string_view::npos
            || line.find("wsarecv:") != std::string_view::npos
            || line.find("wsasend:") != std::string_view::npos;
    }
} // namespace Logging::Policy

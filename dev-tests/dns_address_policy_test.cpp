#include "include/configs/common/DnsAddressPolicy.hpp"
#include <cassert>

int main() {
    using Configs::normalizeLegacyDnsAddress;
    assert(normalizeLegacyDnsAddress("tls://77.88.8.8") == "tls://common.dot.dns.yandex.net");
    assert(normalizeLegacyDnsAddress("tls://77.88.8.1:853") == "tls://common.dot.dns.yandex.net:853");
    for (const auto &input : {"77.88.8.8", "udp://77.88.8.8", "tls://1.1.1.1",
             "tls://common.dot.dns.yandex.net", "tls://77.88.8.88", "tls://77.88.8.8?custom=1"}) {
        assert(normalizeLegacyDnsAddress(input) == input);
    }
}

#include <cassert>

#include "include/global/DiagnosticLogPolicy.hpp"

int main() {
    using Logging::Policy::IsCollapsibleTransportNoise;
    assert(IsCollapsibleTransportNoise(
        "ERROR[48] connection upload closed: raw-read tcp4 172.28.0.1:6566->172.28.0.2:10087: "
        "An existing connection was forcibly closed by the remote host"));
    assert(IsCollapsibleTransportNoise(
        "connection download closed: wsarecv: An existing connection was forcibly closed by the remote host"));
    assert(!IsCollapsibleTransportNoise("Profile start failed: invalid XHTTP method"));
    assert(!IsCollapsibleTransportNoise("dns: lookup failed for example.test"));
    assert(!IsCollapsibleTransportNoise("connection upload closed: unexpected EOF"));
}

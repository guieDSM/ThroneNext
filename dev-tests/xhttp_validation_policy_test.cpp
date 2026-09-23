#include "include/configs/common/XHTTPValidationPolicy.hpp"

#include <cassert>

using Configs::xhttp_validation::Input;
using Configs::xhttp_validation::Validate;
using Configs::xhttp_validation::parseRange;

int main() {
    assert(parseRange("1")->from == 1 && parseRange("1")->to == 1);
    assert(parseRange("9-3")->from == 3 && parseRange("9-3")->to == 9);
    assert(parseRange("-4--2")->from == -4 && parseRange("-4--2")->to == -2);
    assert(!parseRange("1-"));
    assert(!parseRange("1.5"));

    Input valid;
    valid.mode = "packet-up";
    valid.uplinkHTTPMethod = "get";
    valid.uplinkDataPlacement = "header";
    valid.xPaddingBytes = "100-1000";
    valid.sessionIDTable = "Base62";
    valid.sessionIDLength = "8-12";
    valid.maxConnections = "3";
    assert(Validate(valid).ok);

    Input getStream;
    getStream.mode = "stream-up";
    getStream.uplinkHTTPMethod = "GET";
    assert(!Validate(getStream).ok);

    Input dataStream;
    dataStream.mode = "stream-one";
    dataStream.uplinkDataPlacement = "cookie";
    assert(!Validate(dataStream).ok);

    Input conflictingXmux;
    conflictingXmux.maxConnections = "1-3";
    conflictingXmux.maxConcurrency = "4";
    assert(!Validate(conflictingXmux).ok);

    Input externalHeader;
    externalHeader.headerNames = {"User-Agent", "HOST"};
    assert(!Validate(externalHeader).ok);

    Input invalidPadding;
    invalidPadding.xPaddingBytes = "0-100";
    assert(!Validate(invalidPadding).ok);

    Input weakSession;
    weakSession.sessionIDTable = "ab";
    weakSession.sessionIDLength = "1-8";
    assert(!Validate(weakSession).ok);

    Input downloadInStreamOne;
    downloadInStreamOne.mode = "stream-one";
    downloadInStreamOne.downloadSettingsPresent = true;
    assert(!Validate(downloadInStreamOne).ok);

    Input malformedDownload;
    malformedDownload.downloadSettingsPresent = true;
    malformedDownload.downloadSettingsValid = false;
    assert(!Validate(malformedDownload).ok);
}

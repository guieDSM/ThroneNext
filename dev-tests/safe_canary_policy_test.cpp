#include "include/global/SafeCanaryPolicy.hpp"

#include <cassert>

using Configs::safe_canary::Decision;
using Configs::safe_canary::Evaluate;
using Configs::safe_canary::Input;

namespace {
    Input validNewRoot() {
        Input input;
        input.requested = true;
        input.appdataOptionPresent = true;
        input.explicitPathPresent = true;
        input.pathIsAbsolute = true;
        return input;
    }
}

int main() {
    assert(Evaluate(Input{}).decision == Decision::NormalLaunch);

    {
        auto input = validNewRoot();
        assert(Evaluate(input).decision == Decision::InitializeRoot);
    }
    {
        auto input = validNewRoot();
        input.directoryExists = true;
        input.directoryIsEmpty = true;
        assert(Evaluate(input).decision == Decision::InitializeRoot);
    }
    {
        auto input = validNewRoot();
        input.directoryExists = true;
        input.markerPresent = true;
        input.markerValid = true;
        assert(Evaluate(input).decision == Decision::ResumeRoot);
    }
    for (int scenario = 0; scenario < 7; ++scenario) {
        auto input = validNewRoot();
        input.directoryExists = true;
        input.directoryIsEmpty = true;
        switch (scenario) {
            case 0: input.appdataOptionPresent = false; break;
            case 1: input.explicitPathPresent = false; break;
            case 2: input.pathIsAbsolute = false; break;
            case 3: input.pathIsFilesystemRoot = true; break;
            case 4: input.pathMatchesApplicationDir = true; break;
            case 5: input.directoryIsReparsePoint = true; break;
            case 6:
                input.directoryIsEmpty = false;
                input.markerPresent = true;
                input.markerValid = false;
                break;
        }
        assert(Evaluate(input).decision == Decision::Deny);
    }
    {
        auto input = validNewRoot();
        input.directoryExists = true;
        input.directoryIsEmpty = false;
        assert(Evaluate(input).decision == Decision::Deny);
    }
}

#pragma once

namespace Configs::safe_canary {

enum class Decision {
    NormalLaunch,
    InitializeRoot,
    ResumeRoot,
    Deny,
};

struct Input {
    bool requested = false;
    bool appdataOptionPresent = false;
    bool explicitPathPresent = false;
    bool pathIsAbsolute = false;
    bool pathIsFilesystemRoot = false;
    bool pathMatchesApplicationDir = false;
    bool directoryExists = false;
    bool directoryIsReparsePoint = false;
    bool directoryIsEmpty = false;
    bool markerPresent = false;
    bool markerValid = false;
};

struct Result {
    Decision decision = Decision::Deny;
    const char* reason = "safe canary denied";

    [[nodiscard]] constexpr bool allowed() const {
        return decision != Decision::Deny;
    }
};

[[nodiscard]] constexpr Result Evaluate(const Input& input) {
    if (!input.requested) return {Decision::NormalLaunch, "normal launch"};
    if (!input.appdataOptionPresent || !input.explicitPathPresent)
        return {Decision::Deny, "safe canary requires an explicit -appdata directory"};
    if (!input.pathIsAbsolute)
        return {Decision::Deny, "safe canary appdata directory must be absolute"};
    if (input.pathIsFilesystemRoot || input.pathMatchesApplicationDir)
        return {Decision::Deny, "safe canary appdata directory is too broad"};
    if (input.directoryIsReparsePoint)
        return {Decision::Deny, "safe canary appdata directory must not be a reparse point"};
    if (!input.directoryExists)
        return {Decision::InitializeRoot, "initialize new safe canary root"};
    if (input.markerPresent)
        return input.markerValid
            ? Result{Decision::ResumeRoot, "resume marked safe canary root"}
            : Result{Decision::Deny, "safe canary marker is invalid"};
    if (input.directoryIsEmpty)
        return {Decision::InitializeRoot, "initialize empty safe canary root"};
    return {Decision::Deny, "safe canary refuses an unmarked non-empty directory"};
}

} // namespace Configs::safe_canary

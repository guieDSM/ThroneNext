#pragma once
#include <QString>

namespace Configs
{
    class outbound;

    // Concrete outbound for a Throne type string; unknown types yield a base
    // outbound flagged invalid. Never null; caller takes ownership.
    outbound* NewOutboundByType(const QString& type);
}

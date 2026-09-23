#pragma once
#include "include/configs/common/Outbound.h"

namespace Configs
{
    inline QStringList mieruTransports = {"TCP", "UDP"};
    inline QStringList mieruMultiplexing = {"", "MULTIPLEXING_OFF", "MULTIPLEXING_LOW", "MULTIPLEXING_MIDDLE", "MULTIPLEXING_HIGH"};

    class mieru : public outbound
    {
        public:
        QString transport = "TCP";
        QString username;
        QString password;
        QString multiplexing;
        QString traffic_pattern;
        // Comma-separated list of port ranges, e.g. "9000-9010,9020-9030".
        // Exported to sing-box as the "server_ports" string array.
        QString server_ports;

        // baseConfig overrides
        bool ParseFromLink(const QString& link) override;
        bool ParseFromJson(const QJsonObject& object) override;
        bool ParseFromClash(const clash::Proxies& object) override;
        QString ExportToLink() override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;

        QString DisplayType() override;
        SecurityInfo GetSecurity() override;
    };
}

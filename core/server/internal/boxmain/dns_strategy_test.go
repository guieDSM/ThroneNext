package boxmain

import (
	"strings"
	"testing"
)

const dnsStrategyConfig = `{
  "dns": {
    "servers": [{"type": "local", "tag": "dns-direct"}],
    "rules": [
      {
        "domain": "localhost",
        "action": "predefined",
        "query_type": "A",
        "rcode": "NOERROR",
        "answer": "localhost. IN A 127.0.0.1"
      },
      {"action": "route", "server": "dns-direct"}
    ],
    "strategy": "ipv4_only"
  },
  "outbounds": [{"type": "direct", "tag": "direct"}],
  "route": {"final": "direct"}
}`

func TestDNSClientLevelStrategyWithQueryType(t *testing.T) {
	if err := Check([]byte(dnsStrategyConfig)); err != nil {
		t.Fatalf("modern DNS strategy config rejected: %v", err)
	}
}

func TestLegacyDNSRuleStrategyWithQueryTypeIsRejected(t *testing.T) {
	legacy := strings.Replace(
		dnsStrategyConfig,
		`{"action": "route", "server": "dns-direct"}`,
		`{"action": "route", "server": "dns-direct", "strategy": "ipv4_only"}`,
		1,
	)
	if err := Check([]byte(legacy)); err == nil {
		t.Fatal("legacy DNS rule-action strategy unexpectedly accepted")
	}
}

func TestEmptyLegacyDNSRuleStrategyWithQueryTypeIsAccepted(t *testing.T) {
	compat := strings.Replace(
		dnsStrategyConfig,
		`{"action": "route", "server": "dns-direct"}`,
		`{"action": "route", "server": "dns-direct", "strategy": ""}`,
		1,
	)
	compat = strings.Replace(compat, `,
    "strategy": "ipv4_only"`, "", 1)
	if err := Check([]byte(compat)); err != nil {
		t.Fatalf("empty legacy DNS strategy should be treated as as-is: %v", err)
	}
}

package boxdns

import (
	"sync"

	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
)

// DnsManagerInstance is the process-wide, always-on default-interface monitor.
// It is created at package load on every platform, so the physical default-route
// interface can be queried at any time via DefaultInterface (and the
// GetDefaultInterface RPC), independently of the sing-box instance lifecycle.
//
// On Windows it additionally drives system-DNS management through HandleSystemDNS;
// on other platforms that callback is a no-op (see dns_manager_stub.go) while the
// monitor itself still runs so interface detection is consistent everywhere.
var DnsManagerInstance *DnsManager

type DnsManager struct {
	Monitor  tun.DefaultInterfaceMonitor
	dnsMu    sync.Mutex
	dnsIsSet bool
	lastIfc  *control.Interface
}

// DefaultInterface returns the current physical default-route interface tracked
// by the always-on monitor, or nil when the monitor is unavailable. Virtual
// (TUN) and loopback interfaces are excluded by the monitor, so the result is
// safe to bind a core egress to even while throne-tun is up.
func DefaultInterface() *control.Interface {
	if DnsManagerInstance == nil || DnsManagerInstance.Monitor == nil {
		return nil
	}
	return DnsManagerInstance.Monitor.DefaultInterface()
}

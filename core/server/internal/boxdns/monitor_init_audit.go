//go:build throne_audit

package boxdns

import (
	"fmt"
	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
	logger2 "github.com/sagernet/sing/common/logger"
)

// Audit builds need release-equivalent physical egress detection but must not
// register HandleSystemDNS, which can restore marked adapter state at startup.
func init() {
	logger := logger2.NOP()
	updates, err := tun.NewNetworkUpdateMonitor(logger)
	if err != nil {
		fmt.Println("Could not create audit NetworkUpdateMonitor")
		return
	}
	monitor, err := tun.NewDefaultInterfaceMonitor(updates, logger, tun.DefaultInterfaceMonitorOptions{
		InterfaceFinder: control.NewDefaultInterfaceFinder(),
	})
	if err != nil {
		_ = updates.Close()
		fmt.Println("Could not create audit DefaultInterfaceMonitor")
		return
	}
	manager := &DnsManager{Monitor: monitor}
	if err = updates.Start(); err != nil {
		_ = monitor.Close()
		_ = updates.Close()
		return
	}
	if err = monitor.Start(); err != nil {
		_ = monitor.Close()
		_ = updates.Close()
		return
	}
	DnsManagerInstance = manager
}

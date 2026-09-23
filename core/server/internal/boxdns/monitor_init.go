//go:build !throne_test && !throne_audit

package boxdns

import (
	"fmt"
	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
	logger2 "github.com/sagernet/sing/common/logger"
)

// Package-level DNS recovery must not run in isolated unit tests. The explicit
// throne_test build tag excludes this initializer, never used for release builds.
func init() {
	logger := logger2.NOP()
	updMonitor, err := tun.NewNetworkUpdateMonitor(logger)
	if err != nil {
		fmt.Println("Could not create NetworkUpdateMonitor")
		return
	}
	monitor, err := tun.NewDefaultInterfaceMonitor(updMonitor, logger, tun.DefaultInterfaceMonitorOptions{
		InterfaceFinder: control.NewDefaultInterfaceFinder(),
	})
	if err != nil {
		_ = updMonitor.Close()
		fmt.Println("Could not create DefaultInterfaceMonitor")
		return
	}
	manager := &DnsManager{Monitor: monitor}
	monitor.RegisterCallback(manager.HandleSystemDNS)
	if err = updMonitor.Start(); err != nil {
		_ = monitor.Close()
		_ = updMonitor.Close()
		fmt.Println("Could not start updMonitor")
		return
	}
	if err = monitor.Start(); err != nil {
		_ = monitor.Close()
		_ = updMonitor.Close()
		fmt.Println("Could not start monitor")
		return
	}
	DnsManagerInstance = manager
}

//go:build !windows

package main

import (
	"ThroneCore/internal/boxbox"
	"ThroneCore/internal/sys"
	"errors"
)

func restoreSystemDNSForStop(box *boxbox.Box) error {
	if box == nil {
		return errors.New("DNS restore needs the profile's interface monitor")
	}
	return sys.SetSystemDNS("Empty", box.Network().InterfaceMonitor())
}

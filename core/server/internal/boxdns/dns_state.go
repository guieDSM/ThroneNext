package boxdns

import (
	"errors"
	"github.com/sagernet/sing/common/control"
)

// The caller serializes transitions. Keeping OS operations as callbacks allows
// failures and adapter handoffs to be tested without changing the host's DNS.
func (d *DnsManager) transitionDNS(ifc *control.Interface, clear bool,
	apply, restore func(control.Interface) error) error {
	if ifc == nil {
		if !clear {
			return errors.New("default interface is unavailable")
		}
		ifc = d.lastIfc
		if ifc == nil {
			if d.dnsIsSet {
				return errors.New("DNS restore interface is unavailable")
			}
			return nil
		}
	}
	if d.lastIfc != nil && d.lastIfc.Index != ifc.Index {
		if err := restore(*d.lastIfc); err != nil {
			return err
		}
		d.lastIfc = nil
	}
	if clear {
		if err := restore(*ifc); err != nil {
			return err
		}
		d.dnsIsSet, d.lastIfc = false, nil
		return nil
	}
	if err := apply(*ifc); err != nil {
		return err
	}
	snapshot := *ifc
	d.dnsIsSet, d.lastIfc = true, &snapshot
	return nil
}

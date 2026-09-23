package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxbox"
	"ThroneCore/internal/boxdns"
	"context"
	"errors"
)

func (s *server) SetSystemDNS(ctx context.Context, in *gen.SetSystemDNSRequest) (*gen.EmptyResp, error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()
	if !in.GetClear() && currentBox() == nil {
		return nil, errors.New("cannot enable system DNS without a running profile")
	}
	err := boxdns.DnsManagerInstance.SetSystemDNS(nil, in.GetClear())
	if err != nil {
		return nil, err
	}
	needUnsetDNS = !in.GetClear()

	return &gen.EmptyResp{}, nil
}

func restoreSystemDNSForStop(_ *boxbox.Box) error {
	return boxdns.DnsManagerInstance.SetSystemDNS(nil, true)
}

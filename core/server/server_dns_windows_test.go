package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxbox"
	"ThroneCore/internal/boxdns"
	"context"
	"testing"
)

func TestCannotEnableDNSWithoutRunningProfile(t *testing.T) {
	_, err := (&server{}).SetSystemDNS(context.Background(), &gen.SetSystemDNSRequest{Clear: To(false)})
	if err == nil {
		t.Fatal("DNS enabled without its listener")
	}
}

func TestDNSRestoreFailureRetainsTunnel(t *testing.T) {
	// This test uses the throne_test build tag: it never creates an OS monitor.
	oldManager := boxdns.DnsManagerInstance
	boxdns.DnsManagerInstance = nil
	box := new(boxbox.Box)
	setBoxInstance(box, func() { t.Error("tunnel was cancelled before DNS recovery") })
	needUnsetDNS = true
	t.Cleanup(func() {
		setBoxInstance(nil, nil)
		needUnsetDNS = false
		boxdns.DnsManagerInstance = oldManager
	})
	resp, err := (&server{}).Stop(context.Background(), &gen.EmptyReq{})
	if err != nil || resp.GetError() == "" || currentBox() != box || !needUnsetDNS {
		t.Fatal("failed DNS recovery lost ownership of the tunnel")
	}
}

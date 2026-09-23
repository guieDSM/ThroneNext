package boxdns

import (
	"errors"
	"github.com/sagernet/sing/common/control"
	"reflect"
	"testing"
)

func TestDNSTransitionsPreserveRetryState(t *testing.T) {
	first := &control.Interface{Index: 1}
	second := &control.Interface{Index: 2}
	failed := errors.New("synthetic OS failure")
	for _, tc := range []struct {
		name                          string
		clear, failRestore, failApply bool
		wantIndex                     int
		wantSet                       bool
		wantCalls                     []int
	}{
		{"handoff", false, false, false, 2, true, []int{-1, 2}},
		{"failed old restore", false, true, false, 1, true, []int{-1}},
		{"failed new apply", false, false, true, 0, true, []int{-1, 2}},
		{"clear after handoff", true, false, false, 0, false, []int{-1, -2}},
		{"failed clear", true, true, false, 1, true, []int{-1}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			d := &DnsManager{lastIfc: first, dnsIsSet: true}
			var calls []int
			err := d.transitionDNS(second, tc.clear, func(ifc control.Interface) error {
				calls = append(calls, ifc.Index)
				if tc.failApply {
					return failed
				}
				return nil
			}, func(ifc control.Interface) error {
				calls = append(calls, -ifc.Index)
				if tc.failRestore {
					return failed
				}
				return nil
			})
			if (err != nil) != (tc.failApply || tc.failRestore) {
				t.Fatalf("wrong error: %v", err)
			}
			index := 0
			if d.lastIfc != nil {
				index = d.lastIfc.Index
			}
			if index != tc.wantIndex || d.dnsIsSet != tc.wantSet || !reflect.DeepEqual(calls, tc.wantCalls) {
				t.Fatalf("wrong transition: index=%d enabled=%v calls=%v", index, d.dnsIsSet, calls)
			}
		})
	}
}

func TestFailedDNSEnableDoesNotClaimSuccess(t *testing.T) {
	d := &DnsManager{}
	err := d.transitionDNS(&control.Interface{Index: 1}, false,
		func(control.Interface) error { return errors.New("denied") },
		func(control.Interface) error { t.Fatal("unexpected restore"); return nil })
	if err == nil || d.dnsIsSet || d.lastIfc != nil {
		t.Fatal("failed enable was published")
	}
}

func TestDNSRestoreWithoutDefaultRoute(t *testing.T) {
	d := &DnsManager{lastIfc: &control.Interface{Index: 7}, dnsIsSet: true}
	restored := 0
	apply := func(control.Interface) error { t.Fatal("unexpected enable"); return nil }
	restore := func(ifc control.Interface) error { restored = ifc.Index; return nil }
	if err := d.transitionDNS(nil, true, apply, restore); err != nil {
		t.Fatal(err)
	}
	if restored != 7 || d.dnsIsSet || d.lastIfc != nil {
		t.Fatal("owned adapter was not restored")
	}
	restored = 0
	if err := d.transitionDNS(nil, true, apply, restore); err != nil || restored != 0 {
		t.Fatal("repeated clear should be a no-op")
	}
	if err := d.transitionDNS(nil, false, apply, restore); err == nil {
		t.Fatal("enable needs an adapter")
	}
}

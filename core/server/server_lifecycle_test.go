package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxbox"
	"ThroneCore/internal/process"
	"context"
	"os"
	"path/filepath"
	"testing"
)

// A second Start must never unpublish the instance that Stop still owns.
func TestDuplicateStartPreservesInstance(t *testing.T) {
	box := new(boxbox.Box)
	cancelled := false
	setBoxInstance(box, func() { cancelled = true })
	autoRedirectMark.Store(42)
	t.Cleanup(func() {
		setBoxInstance(nil, nil)
		autoRedirectMark.Store(0)
	})
	resp, err := (&server{}).Start(context.Background(), &gen.LoadConfigReq{})
	if err != nil || resp.GetError() == "" {
		t.Fatalf("duplicate Start must return a profile error: %v, %v", resp, err)
	}
	got, cancel := currentInstance()
	if got != box || cancel == nil || cancelled || autoRedirectMark.Load() != 42 {
		t.Fatal("duplicate Start lost ownership of the active tunnel")
	}
}

func TestStartMissingFieldsDoesNotPanic(t *testing.T) {
	resp, err := (&server{}).Start(context.Background(), &gen.LoadConfigReq{})
	if err != nil || resp.GetError() == "" {
		t.Fatalf("empty Start must fail safely: %v, %v", resp, err)
	}
	if currentBox() != nil {
		t.Fatal("invalid Start published an instance")
	}
}

func TestFailedStartCleansOrphanedExtraProcess(t *testing.T) {
	path := filepath.Join(t.TempDir(), "extra-config")
	if err := os.WriteFile(path, []byte("synthetic"), 0600); err != nil {
		t.Fatal(err)
	}
	extraProcess = process.NewProcess("", nil, true)
	extraProcess.SetCleanupPath(path)
	defer func() {
		if extraProcess != nil {
			extraProcess.Stop()
			extraProcess = nil
		}
	}()
	resp, err := (&server{}).Start(context.Background(), &gen.LoadConfigReq{
		CoreConfig: To(`{"outbounds":[{"type":"direct"}]}`),
		NeedXray:   To(true), XrayConfig: To(`{invalid-json`),
	})
	if err != nil || resp.GetError() == "" {
		t.Fatalf("invalid Xray was accepted: %v %v", resp, err)
	}
	if extraProcess != nil || currentBox() != nil {
		t.Fatal("failed start retained partial resources")
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatalf("private extra config was not removed: %v", err)
	}
}

func TestStopCleansSidecarsWithoutRouter(t *testing.T) {
	path := filepath.Join(t.TempDir(), "orphan-config")
	if err := os.WriteFile(path, []byte("synthetic"), 0600); err != nil {
		t.Fatal(err)
	}
	extraProcess = process.NewProcess("", nil, true)
	extraProcess.SetCleanupPath(path)
	resp, err := (&server{}).Stop(context.Background(), &gen.EmptyReq{})
	if err != nil || resp.GetError() != "" || extraProcess != nil {
		t.Fatal("orphan stop failed")
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatal("orphan config survived stop")
	}
}

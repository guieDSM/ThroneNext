package process

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

type fakeChild struct{ killed chan struct{} }

func (f *fakeChild) Kill() error { close(f.killed); return nil }
func (f *fakeChild) Wait() error { return nil }

func TestStopWaitsForChildCleanup(t *testing.T) {
	path := filepath.Join(t.TempDir(), "private-config")
	if err := os.WriteFile(path, []byte("synthetic"), 0600); err != nil {
		t.Fatal(err)
	}
	child := &fakeChild{killed: make(chan struct{})}
	p := &Process{run: child, done: make(chan struct{}), cleanupPath: path}
	go func() {
		<-child.killed
		p.cleanup()
		close(p.done)
	}()
	p.Stop()
	select {
	case <-p.done:
	default:
		t.Fatal("Stop returned before the child finished")
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatal("private config survived Stop")
	}
}

func TestStopBeforeStartCleansConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "private-config")
	if err := os.WriteFile(path, nil, 0600); err != nil {
		t.Fatal(err)
	}
	p := NewProcess("", nil, true)
	p.SetCleanupPath(path)
	start := time.Now()
	p.Stop()
	p.Stop()
	if time.Since(start) > time.Second {
		t.Fatal("Stop without a child blocked")
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatal("config survived Stop")
	}
}

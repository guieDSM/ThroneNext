package main

import (
	"context"
	"errors"
	"strings"
	"testing"
	"time"

	xnet "github.com/xtls/xray-core/common/net"
	xdns "github.com/xtls/xray-core/features/dns"
)

type failingXrayResolver struct {
	instanceContext context.Context
	result          []xnet.IP
	err             error
}

func (*failingXrayResolver) Type() interface{} { return xdns.ClientType() }
func (*failingXrayResolver) Start() error      { return nil }
func (*failingXrayResolver) Close() error      { return nil }
func (r *failingXrayResolver) SetInstanceContext(ctx context.Context) {
	r.instanceContext = ctx
}
func (r *failingXrayResolver) LookupIP(string, xdns.IPOption) ([]xnet.IP, uint32, error) {
	return r.result, 10, r.err
}

func TestDiagnosticXrayResolverPreservesContextAndResult(t *testing.T) {
	inner := &failingXrayResolver{err: errors.New("record not found")}
	resolver := newDiagnosticXrayResolver(inner)
	var reports []string
	resolver.report = func(message string) { reports = append(reports, message) }
	clock := time.Unix(100, 0)
	resolver.now = func() time.Time { return clock }
	ctx := context.WithValue(context.Background(), struct{}{}, true)
	resolver.SetInstanceContext(ctx)
	if inner.instanceContext != ctx {
		t.Fatal("the Xray instance context was not forwarded")
	}
	option := xdns.IPOption{IPv4Enable: true}
	_, ttl, err := resolver.LookupIP("example.test", option)
	if ttl != 10 || !errors.Is(err, inner.err) {
		t.Fatal("the resolver changed the DNS result")
	}
	resolver.LookupIP("example.test", option)
	if len(reports) != 1 || !strings.Contains(reports[0], `domain="example.test"`) ||
		!strings.Contains(reports[0], "record not found") {
		t.Fatalf("expected one actionable, rate-limited diagnostic, got %q", reports)
	}
	clock = clock.Add(time.Minute)
	resolver.LookupIP("example.test", option)
	if len(reports) != 2 {
		t.Fatalf("expected a renewed diagnostic after the rate limit, got %d", len(reports))
	}
	inner.err = nil
	inner.result = []xnet.IP{{127, 0, 0, 1}}
	resolver.LookupIP("example.test", option)
	if len(reports) != 2 {
		t.Fatal("a successful DNS lookup was logged as a failure")
	}
}

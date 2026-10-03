package main

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	xnet "github.com/xtls/xray-core/common/net"
	xdns "github.com/xtls/xray-core/features/dns"
)

// Xray only uses this resolver for outbound server hostnames. Include the name
// and requested address family when a lookup fails so an XHTTP server DNS
// failure can be distinguished from a rejected VLESS connection. Keep the
// original result: diagnostics must not change DNS or routing behavior.
type diagnosticXrayResolver struct {
	xdns.Client
	mu         sync.Mutex
	lastReport map[string]time.Time
	now        func() time.Time
	report     func(string)
}

func newDiagnosticXrayResolver(client xdns.Client) *diagnosticXrayResolver {
	return &diagnosticXrayResolver{
		Client:     client,
		lastReport: make(map[string]time.Time),
		now:        time.Now,
		report:     func(message string) { log.Print(message) },
	}
}

// core.SetOutboundDNS supplies the owning Xray instance through this method.
// Forwarding it is required by the wrapped throne-dns resolver.
func (r *diagnosticXrayResolver) SetInstanceContext(ctx context.Context) {
	if aware, ok := r.Client.(interface{ SetInstanceContext(context.Context) }); ok {
		aware.SetInstanceContext(ctx)
	}
}

func (r *diagnosticXrayResolver) LookupIP(domain string, option xdns.IPOption) ([]xnet.IP, uint32, error) {
	ips, ttl, err := r.Client.LookupIP(domain, option)
	if err == nil && len(ips) != 0 {
		return ips, ttl, nil
	}

	key := fmt.Sprintf("%s:%t:%t", domain, option.IPv4Enable, option.IPv6Enable)
	now := r.now()
	r.mu.Lock()
	last := r.lastReport[key]
	if !last.IsZero() && now.Sub(last) < time.Minute {
		r.mu.Unlock()
		return ips, ttl, err
	}
	r.lastReport[key] = now
	r.mu.Unlock()

	reason := "empty response"
	if err != nil {
		reason = err.Error()
	}
	r.report(fmt.Sprintf("Xray outbound DNS lookup failed: domain=%q ipv4=%t ipv6=%t: %s",
		domain, option.IPv4Enable, option.IPv6Enable, reason))
	return ips, ttl, err
}

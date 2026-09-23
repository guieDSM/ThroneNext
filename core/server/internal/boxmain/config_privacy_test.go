package boxmain

import (
	"strings"
	"testing"
)

func TestMalformedConfigDoesNotEchoCredentials(t *testing.T) {
	_, err := parseConfig(newBoxContext(), []byte(`{"outbounds":[{"type":"socks","server":"127.0.0.1","server_port":11880,"password":"private-test-password"}],"unexpected_field":true}`))
	if err == nil {
		t.Fatal("expected an invalid field error")
	}
	if strings.Contains(err.Error(), "private-test-password") || strings.Contains(err.Error(), `"outbounds"`) {
		t.Fatal("parse error copied the private configuration")
	}
}

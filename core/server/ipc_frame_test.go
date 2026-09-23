package main

import (
	"bytes"
	"encoding/binary"
	"strings"
	"testing"
)

func ipcFrame(method string, size uint32, body []byte) []byte {
	var frame bytes.Buffer
	_ = binary.Write(&frame, binary.LittleEndian, uint32(17))
	_ = binary.Write(&frame, binary.LittleEndian, uint16(len(method)))
	frame.WriteString(method)
	_ = binary.Write(&frame, binary.LittleEndian, size)
	frame.Write(body)
	return frame.Bytes()
}

func TestReadIPCRequest(t *testing.T) {
	for _, tc := range []struct {
		name  string
		frame []byte
		valid bool
	}{
		{"normal", ipcFrame("Start", 3, []byte("abc")), true},
		{"empty payload", ipcFrame("Stop", 0, nil), true},
		{"empty method", ipcFrame("", 0, nil), false},
		{"long method", ipcFrame(strings.Repeat("a", 129), 0, nil), false},
		{"oversized", ipcFrame("Start", maxIPCPayload+1, nil), false},
		{"max uint32", ipcFrame("Start", ^uint32(0), nil), false},
		{"truncated payload", ipcFrame("Start", 3, []byte("a")), false},
		{"truncated header", []byte{1, 2}, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			id, method, payload, err := readIPCRequest(bytes.NewReader(tc.frame))
			if (err == nil) != tc.valid {
				t.Fatalf("unexpected result: %v", err)
			}
			if tc.valid && (id != 17 || method == "" || (method == "Start" && string(payload) != "abc")) {
				t.Fatal("decoded frame was corrupted")
			}
		})
	}
}

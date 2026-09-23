package main

import (
	"encoding/binary"
	"fmt"
	"io"
)

const maxIPCPayload = 64 * 1024 * 1024
const maxIPCMethod = 128

func readIPCRequest(r io.Reader) (id uint32, method string, payload []byte, err error) {
	if err = binary.Read(r, binary.LittleEndian, &id); err != nil {
		return
	}
	var methodLen uint16
	if err = binary.Read(r, binary.LittleEndian, &methodLen); err != nil {
		return
	}
	if methodLen == 0 || methodLen > maxIPCMethod {
		err = fmt.Errorf("invalid IPC method length: %d", methodLen)
		return
	}
	methodBytes := make([]byte, methodLen)
	if _, err = io.ReadFull(r, methodBytes); err != nil {
		return
	}
	method = string(methodBytes)
	var payloadLen uint32
	if err = binary.Read(r, binary.LittleEndian, &payloadLen); err != nil {
		return
	}
	if payloadLen > maxIPCPayload {
		err = fmt.Errorf("IPC payload exceeds %d bytes", maxIPCPayload)
		return
	}
	payload = make([]byte, payloadLen)
	_, err = io.ReadFull(r, payload)
	return
}

package main

// #cgo LDFLAGS:
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #cgo CFLAGS: -I../include
// #cgo CFLAGS: -I../libs/gsl-parser/include
// #cgo LDFLAGS: -L../build/lib/ -lknowdy_static
// #cgo LDFLAGS: -L../build/libs/gsl-parser/lib/ -lgsl-parser_static
// #include <knd_steward.h>
// #include <knd_task.h>
// static void kndSteward_del__(struct kndSteward *steward)
// {
//     if (steward) {
//         knd_steward_del(steward);
//     }
// }
import "C"
import (
	"errors"
	"log"
	"unsafe"
)

type kndProc struct {
	Name          string
	Role          string
	steward         *C.struct_kndSteward
	parentAddress string
	readerReadyQueue   chan *C.struct_kndTask
	writerReadyQueue   chan *C.struct_kndTask
	writerConfirmQueue chan *C.struct_kndTask
}

func New(conf string, parentAddress string, concurrencyFactor int) (*kndProc, error) {
	var steward *C.struct_kndSteward = nil

	errCode := C.knd_steward_new((**C.struct_kndSteward)(&steward),
		C.CString(conf), C.size_t(len(conf)))
	if errCode != C.int(0) {
		return nil, errors.New("failed to create a Knowdy steward")
	}

	proc := kndProc{
		steward:         steward,
		parentAddress: parentAddress,
		writerReadyQueue:       make(chan *C.struct_kndTask, concurrencyFactor),
		writerConfirmQueue:     make(chan *C.struct_kndTask, concurrencyFactor),
		readerReadyQueue:       make(chan *C.struct_kndTask, concurrencyFactor),
	}

	proc.Name = C.GoStringN(&steward.name[0], C.int(steward.name_size))

	for i := 0; i < concurrencyFactor; i++ {
		var task *C.struct_kndTask
		errCode := C.knd_task_new(&task, C.KND_AGENT_WRITER, C.int(i + 1), steward)
		if errCode != C.int(0) {
			proc.Del()
			return nil, errors.New("failed to create kndTask writer")
		}
		proc.writerReadyQueue <- task
	}

	return &proc, nil
}

func (p *kndProc) Del() error {
	maxWriters := len(p.writerReadyQueue)
	for i := 0; i < maxWriters; i++ {
		t := <-p.writerReadyQueue
		C.knd_task_del(t)
	}
	C.kndSteward_del__(p.steward)
	return nil
}

func (p *kndProc) ResetWriter(w *C.struct_kndTask) error {
        C.knd_task_reset(w)
	p.writerReadyQueue <- w
	return nil
}

func (p *kndProc) ResetReader(r *C.struct_kndTask) error {
        C.knd_task_reset(r)
	p.readerReadyQueue <- r
	return nil
}

func (p *kndProc) RunCommandTask(request string, request_len int) (string, error) {
	writer := <-p.writerReadyQueue

	defer func() {
		switch C.int(writer.phase) {
		case C.KND_CONFIRM_COMMIT:
			p.writerConfirmQueue <- writer
			break
		default:
			// non-blocking reset
			go p.ResetWriter(writer)
		}
	}()

	var block *C.char = nil
	var block_size C.size_t = 0

	req := C.CString(request)
	defer C.free(unsafe.Pointer(req))

	log.Printf(">> writer #%d got %s", writer.id, req)

	errCode := C.knd_task_run(writer, block, C.size_t(block_size))
	if errCode != C.int(0) {
		msg := "task execution failed"
		if (C.int(writer.output_size) != C.int(0)) {
			msg = C.GoStringN((*C.char)(writer.output), C.int(writer.output_size))
		}
		return "", errors.New(msg)
	}

	// generate transaction id
	// generate reply msg
	msg := "no output"
	if (C.int(writer.output_size) != C.int(0)) {
		msg = C.GoStringN((*C.char)(writer.output), C.int(writer.output_size))
	}
	log.Printf(">> result: %s", msg)

	// pass control to the arbiter

	return msg, nil
}

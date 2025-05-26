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
	writers       chan *C.struct_kndTask
	writersWait   chan *C.struct_kndTask
	readers       chan *C.struct_kndTask
	readersWait   chan *C.struct_kndTask
}

func New(conf string, parentAddress string, concurrencyFactor int) (*kndProc, error) {
	var steward *C.struct_kndSteward = nil
	errCode := C.knd_steward_new((**C.struct_kndSteward)(&steward),\
		C.CString(conf), C.size_t(len(conf)))
	if errCode != C.int(0) {
		return nil, errors.New("failed to create a steward")
	}

	proc := kndProc{
		steward:         steward,
		parentAddress: parentAddress,
		writers:       make(chan *C.struct_kndTask, concurrencyFactor),
		readers:       make(chan *C.struct_kndTask, concurrencyFactor),
	}
	proc.Name = C.GoStringN(&steward.name[0], C.int(steward.name_size))
        switch C.int(steward.role) {
	case C.KND_ARBITER:
		proc.Role = "Arbiter"
		break
	case C.KND_READER:
		proc.Role = "Reader"
		break
	default:
		proc.Role = "Default"
		break
	}
	
	for i := 0; i < concurrencyFactor; i++ {
		var task *C.struct_kndTask
		errCode := C.knd_task_new(steward, nil, C.int(i + 1), &task)
		if errCode != C.int(0) {
			proc.Del()
			return nil, errors.New("failed to create kndTask")
		}
		proc.workers <- task
	}
	return &proc, nil
}

func (p *kndProc) Del() error {
	maxWorkers := len(p.workers)
	for i := 0; i < maxWorkers; i++ {
		t := <-p.workers
		C.knd_task_del(t)
	}
	C.kndSteward_del__(p.steward)
	return nil
}

func (p *kndProc) CommandTask(task string, task_len int) (string, string, error) {
	writer := <-p.writers
	defer func() { p.writers <- writer }()

	var block *C.char = nil
	var block_size C.size_t = 0
	C.knd_task_reset(worker)

	cs := C.CString(task)
	defer C.free(unsafe.Pointer(cs))

	errCode := C.knd_task_copy_block(worker, cs, C.size_t(task_len),
		(**C.char)(&block), (*C.size_t)(&block_size))
	if (errCode != C.int(0)) {
		return "", "", errors.New("block alloc failed")
	}

	log.Printf(">> worker #%d got %s", worker.id, task)

	errCode = C.knd_task_run(worker, block, C.size_t(block_size))
	if errCode != C.int(0) {
		msg := "task execution failed"
		if (C.int(worker.output_size) != C.int(0)) {
			msg = C.GoStringN((*C.char)(worker.output), C.int(worker.output_size))
		}
		return "", "", errors.New(msg)
	}

	// any thresholds reached?

	// TODO check what replication level is needed for new commits
	msg := "no output"
	if (C.int(worker.output_size) != C.int(0)) {
		msg = C.GoStringN((*C.char)(worker.output), C.int(worker.output_size))
	}
	log.Printf(">> result: %s", msg)
	
	return C.GoStringN((*C.char)(worker.output), C.int(worker.output_size)), "meta", nil
}

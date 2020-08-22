package main

// #cgo LDFLAGS:
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #cgo CFLAGS: -I../include
// #cgo CFLAGS: -I../libs/gsl-parser/include
// #cgo LDFLAGS: -L../build/lib/ -lknowdy_static
// #cgo LDFLAGS: -L../build/libs/gsl-parser/lib/ -lgsl-parser_static
// #include <knd_shard.h>
// #include <knd_task.h>
// static void kndShard_del__(struct kndShard *shard)
// {
//     if (shard) {
//         knd_shard_del(shard);
//     }
// }
import "C"
import (
	"errors"
	"unsafe"
)

type kndProc struct {
	Name          string
	Role          string
	shard         *C.struct_kndShard
	parentAddress string
	workers       chan *C.struct_kndTask
}

func New(conf string, parentAddress string, concurrencyFactor int) (*kndProc, error) {
	var shard *C.struct_kndShard = nil
	errCode := C.knd_shard_new((**C.struct_kndShard)(&shard), C.CString(conf), C.size_t(len(conf)))
	if errCode != C.int(0) {
		return nil, errors.New("could not create shard struct")
	}

	proc := kndProc{
		shard:         shard,
		parentAddress: parentAddress,
		workers:       make(chan *C.struct_kndTask, concurrencyFactor),
	}
	proc.Name = C.GoStringN(&shard.name[0], C.int(shard.name_size))
        switch C.int(shard.role) {
	case C.KND_WRITER:
		proc.Role = "Writer"
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
		errCode := C.knd_task_new(shard, nil, C.int(i + 1), &task)
		if errCode != C.int(0) {
			proc.Del()
			return nil, errors.New("could not create kndTask")
		}
		var ctx C.struct_kndTaskContext
		task.ctx = &ctx

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
	C.kndShard_del__(p.shard)
	return nil
}

func (p *kndProc) RunTask(task string, task_len int) (string, string, error) {
	worker := <-p.workers
	defer func() { p.workers <- worker }()

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

	errCode = C.knd_task_run(worker, block, C.size_t(block_size))
	if errCode != C.int(0) {
		msg := "task execution failed"
		if (C.int(worker.output_size) != C.int(0)) {
			msg = C.GoStringN((*C.char)(worker.output), C.int(worker.output_size))
		}
		return "", "", errors.New(msg)
	}
	// TODO check what replication level is needed for new commits
	
	return C.GoStringN((*C.char)(worker.output), C.int(worker.output_size)), "meta", nil
}

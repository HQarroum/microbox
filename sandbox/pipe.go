//go:build linux

package sandbox

import (
	"golang.org/x/sys/unix"
)

/**
 * Create a pipe for synchronization between parent and child processes.
 * The pipe is created with the O_CLOEXEC flag to ensure that file descriptors
 * are closed on execve() calls.
 * @return read and write file descriptors of the pipe, or an error if any
 */
func MakeSyncPipe() (int, int, error) {
	var p [2]int
	if err := unix.Pipe2(p[:], unix.O_CLOEXEC); err != nil {
		return -1, -1, err
	}
	return p[0], p[1], nil
}

/**
 * Wait for a signal from the parent process by reading from the pipe
 * and then close the read end of the pipe.
 * @param rfd the read file descriptor of the pipe
 * @return error if any
 */
func WaitForParent(rfd int) error {
	var one [1]byte
	_, err := unix.Read(rfd, one[:])
	_ = unix.Close(rfd)
	if err != nil {
		return err
	}
	return nil
}

/**
 * Send a signal to the child process by writing to the pipe
 * and then close the write end of the pipe.
 * @param wfd the write file descriptor of the pipe
 * @return error if any
 */
func SignalChild(wfd int) error {
	_, err := unix.Write(wfd, []byte{1})
	cerr := unix.Close(wfd)
	if err != nil {
		return err
	}
	return cerr
}

/**
 * Close both ends of the pipe.
 * @param rfd the read file descriptor of the pipe
 * @param wfd the write file descriptor of the pipe
 */
func ClosePipe(rfd, wfd int) {
	_ = unix.Close(rfd)
	_ = unix.Close(wfd)
}

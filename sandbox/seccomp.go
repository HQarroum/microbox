//go:build linux

package sandbox

import (
	"fmt"
	"maps"
	"slices"

	seccomp "github.com/seccomp/libseccomp-golang"
	"golang.org/x/sys/unix"
)

/**
 * Default list of denied syscalls for the seccomp filter.
 */
var defaultDenySyscalls = []string{
	/* module & kexec */
	"create_module", "init_module", "finit_module", "delete_module",
	"kexec_load", "kexec_file_load",

	/* keyring & bpf */
	"add_key", "request_key", "keyctl",
	"bpf",

	/* ptrace & process vm */
	"ptrace", "process_vm_readv", "process_vm_writev",

	/* time & clock adjustments */
	"adjtimex", "clock_adjtime", "settimeofday", "stime",

	/* reboot, quotas, nfs, sysfs, legacy */
	"reboot", "quotactl", "nfsservctl", "sysfs", "_sysctl",

	/* personality tweaks (restricted in Docker; safest is block) */
	"personality",

	/* mount-related / root switching */
	"mount", "umount", "umount2", "pivot_root",

	/* namespace / isolation escape hatches (Docker restricts; we block outright here) */
	"setns", "unshare", "nsenter",

	/* open-by-handle (host fs handle bypass) */
	"open_by_handle_at",

	/* perf & fanotify */
	"perf_event_open", "fanotify_init",

	/* handle name lookups and cookies */
	"name_to_handle_at", "lookup_dcookie",

	/* userfault / vm86 & low-level io privs */
	"userfaultfd", "vm86", "vm86old", "iopl", "ioperm",

	/* memory policy & page moving */
	"set_mempolicy", "move_pages",

	/* kcmp info-leak style */
	"kcmp",

	/* accounting */
	"acct",

	/* Modern mount syscalls */
	"open_tree", "move_mount", "fsopen", "fsconfig", "fsmount", "fspick", "mount_setattr",

	/* io_uring */
	"io_uring_setup", "io_uring_enter", "io_uring_register",
}

/**
 * A helper function to merge user-specified allow/deny syscall lists
 * with the default deny list.
 * @param userAllow list of user-allowed syscalls
 * @param userDeny list of user-denied syscalls
 * @return the final merged deny list
 */
func mergeSyscallLists(userAllow, userDeny []string) []string {
	denySet := make(map[string]struct{}, len(defaultDenySyscalls)+len(userDeny))

	// Add default deny list.
	for _, s := range defaultDenySyscalls {
		denySet[s] = struct{}{}
	}

	// Add user deny list.
	for _, s := range userDeny {
		denySet[s] = struct{}{}
	}

	// Remove any user allow overrides.
	for _, s := range userAllow {
		delete(denySet, s)
	}
	out := slices.Sorted(maps.Keys(denySet))
	return out
}

/**
 * SetupSeccomp installs a seccomp filter with default action ALLOW,
 * and adds ERRNO(EPERM) rules for all syscalls in the final deny-list.
 * Must be called in the child *after* filesystem/cgroup/uidmap work,
 * and right before Exec.
 */
func SetupSeccomp(opts *SandboxOptions) error {
	if err := unix.Prctl(unix.PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0); err != nil && err != unix.EINVAL {
		return fmt.Errorf("prctl(NO_NEW_PRIVS): %w", err)
	}

	// By default, we allow syscalls not explicitly denied.
	filter, err := seccomp.NewFilter(seccomp.ActAllow)
	if err != nil {
		return err
	}
	defer filter.Release()

	// Merge lists
	denySet := mergeSyscallLists(opts.AllowSys, opts.DenySys)

	// Deny by returning ENOSYS to signal to applications that they might
	// want to fallback to a different syscall.
	denyAct := seccomp.ActErrno.SetReturnCode(int16(unix.ENOSYS))
	for _, name := range denySet {
		sc, err := seccomp.GetSyscallFromName(name)
		if err != nil {
			continue
		}
		if err := filter.AddRule(sc, denyAct); err != nil {
			continue
		}
	}

	// Load the filter.
	if err := filter.Load(); err != nil {
		return fmt.Errorf("seccomp: load: %w", err)
	}

	return nil
}

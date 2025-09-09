#include "_seccomp.h"

static const char* docker_default_denylist[] = {
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
  "setns", "unshare",

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

  /* accounting & new clone */
  "acct", "clone3"
};

/**
 * @return 1 if name is in list, 0 otherwise.
 */
static int name_in_list(const char *name, const char *const *list, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (list[i] && strcmp(name, list[i]) == 0) {
      return (1);
    }
  }
  return (0);
}

/**
 * @return the length of the default denylist.
 */
size_t docker_default_denylist_len(void) {
  return (sizeof(docker_default_denylist) / sizeof(docker_default_denylist[0]));
}

/**
 * @return the default denylist.
 */
const char** microbox_get_docker_default_denylist() {
  return (docker_default_denylist);
}

/**
 * Install a “default-allow + denylist” seccomp filter.
 *
 * @param deny         array of syscall names to block with ENOSYS
 * @param deny_len     number of entries in deny
 * @param allow_ovr    array of syscall names to *allow* even if in deny
 * @param allow_len    number of entries in allow_ovr
 * @return 0 on success, negative errno on failure
 */
int microbox_setup_seccomp(
  const char **deny,
  size_t deny_len,
  const char **allow_ovr,
  size_t allow_len
) {
  int rc;

  scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
  if (!ctx) {
    return (-ENOMEM);
  }

  for (size_t i = 0; i < deny_len; i++) {
    const char *name = deny[i];
    if (!name || !*name) {
      continue;
    }

    /* Skip if user explicitly wants this syscall allowed */
    if (allow_ovr && name_in_list(name, allow_ovr, allow_len)) {
      continue;
    }

    int nr = seccomp_syscall_resolve_name(name);
    if (nr == __NR_SCMP_ERROR) {
      /* Not present on this arch/kernel; ignore gracefully */
      continue;
    }

    rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOSYS), nr, 0);
    if (rc < 0) {
      seccomp_release(ctx);
      return (-errno);
    }
  }

  rc = seccomp_load(ctx);
  if (rc < 0) {
    seccomp_release(ctx);
    return (-errno);
  }

  seccomp_release(ctx);
  return (0);
}

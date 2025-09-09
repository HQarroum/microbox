#include "sandbox.h"
#include "utils.h"
#include "_seccomp.h"
#include "netns.h"

int microsandbox_setup_fs(const sandbox_options_t* opts);

static const env_var_t safe_env[] = {
  {"PATH", "/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin"},
  {"HOME", "/root"},
  {"TERM", "xterm"},
  {NULL, NULL}
};

/* Create a UID/GID mapping for the child process.  Must be called from the
 * parent after clone3() when the child created a new user namespace.  It
 * writes to /proc/<pid>/setgroups, uid_map, gid_map as described in
 * user_namespaces(7)【466713056869849†L220-L229】. */
static int setup_uid_gid_map(pid_t pid) {
  char path[128];
  int fd;

  /* Deny setgroups; some kernels require this before writing gid_map. */
  snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
  if (write_file(path, "deny") < 0) {
    return (-1);
  }

  /* Map UID 0 in the child namespace to our real UID in the parent. */
  snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
  fd = open(path, O_WRONLY);
  if (fd < 0) {
    return (-1);
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "0 %d 1\n", getuid());
  if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
    close(fd);
    return (-1);
  }
  close(fd);

  /* Map GID 0 in the child namespace to our real GID. */
  snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
  fd = open(path, O_WRONLY);
  if (fd < 0) {
    return (-1);
  }
  snprintf(buf, sizeof(buf), "0 %d 1\n", getgid());
  if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
    return (-1);
  }
  close(fd);

  return (0);
}

/* Attach to cgroup v2 and set CPU/memory limits.  cpus=0 means no limit.
 * memory=NULL means no limit.  Limits are written to cpu.max and memory.max
 * as described in the cgroup documentation【830962371868610†L220-L234】. */
static int setup_cgroup_limits(pid_t pid, double cpus, uint64_t memory) {
  char cg_path[128] = {0};

  // Enable CPU/memory controller.
  {
    int fd = open("/sys/fs/cgroup/cgroup.subtree_control", O_WRONLY);
    if (fd >= 0) {
      if (write(fd, "+memory", 7) < 0 && errno != EBUSY) {
        return (-errno);
      }
      if (write(fd, "+cpu", 4) < 0 && errno != EBUSY) {
        return (-errno);
      }
      close(fd);
    }
  }

  // Create cgroup directory under /sys/fs/cgroup.
  snprintf(cg_path, sizeof(cg_path), "/sys/fs/cgroup/microbox-%d", pid);
  if (mkdir_safe(cg_path, 0755) == -1) {
    return (-errno);
  }

  /* CPU limit */
  if (cpus > 0.0) {
    unsigned int period = 100000; /* 100ms period */
    unsigned int quota = (unsigned int)(cpus * period);
    char buf[64];
    snprintf(buf, sizeof(buf), "%u %u", quota, period);
    char path[256];
    snprintf(path, sizeof(path), "%s/cpu.max", cg_path);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
      return (-errno);
    }
    if (write(fd, buf, strlen(buf)) < 0) {
      return (-errno);
    }
    close(fd);
  }

  /* Memory limit */
  if (memory > 0) {
    char buf[64] = {0};
    char path[256] = {0};

    snprintf(buf, sizeof(buf), "%" PRIu64, memory);
    snprintf(path, sizeof(path), "%s/memory.max", cg_path);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
      return (-errno);
    }
    if (write(fd, buf, strlen(buf)) < 0) {
      return (-errno);
    }
    close(fd);

    snprintf(path, sizeof(path), "%s/memory.swap.max", cg_path);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
      return (-errno);
    }
    if (write(fd, "0", 1) < 0) {
      return (-errno);
    }
    close(fd);
  }

  /* Attach this process to the new cgroup by writing its PID to cgroup.procs */
  char procs_path[256] = {0};
  char buf[32] = {0};
  snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", cg_path);
  int fd = open(procs_path, O_WRONLY);
  if (fd < 0) {
    return (-errno);
  }
  snprintf(buf, sizeof(buf), "%d", pid);
  if (write(fd, buf, strlen(buf)) < 0) {
    return (-errno);
  }
  close(fd);

  return (0);
}

/* Count entries in a NULL-terminated env_var_t array */
static size_t env_len(const env_var_t* env) {
  size_t n = 0;

  if (env) {
    while (env[n].name) {
      n++;
    }
  }
  return (n);
}

/* Free a merged environment array */
void free_env(env_var_t* env) {
  if (!env) {
    return;
  }
  for (size_t i = 0; env[i].name; ++i) {
    free(env[i].name);
    free(env[i].value);
  }
  free(env);
}

/*
 * Merge env1 and env2 into a new env array.
 * - env1 and env2 are NULL-terminated arrays of env_var_t.
 * - env2 overrides env1 on duplicate names.
 * - Returns a newly allocated NULL-terminated array.
 */
env_var_t* merge(const env_var_t* env1, const env_var_t* env2) {
    size_t n1 = env_len(env1);
    size_t n2 = env_len(env2);

    /* Worst case: all variables are unique */
    env_var_t* out = calloc(n1 + n2 + 1, sizeof(env_var_t));
    if (!out) return NULL;

    size_t n = 0;

    /* Copy env1 */
    for (size_t i = 0; i < n1; ++i) {
        out[n].name  = strdup(env1[i].name);
        out[n].value = strdup(env1[i].value);
        if (!out[n].name || !out[n].value) { free_env(out); return NULL; }
        n++;
    }

    /* Add/override with env2 */
    for (size_t i = 0; i < n2; ++i) {
        /* Check if name already exists in out */
        size_t j;
        for (j = 0; j < n; ++j) {
            if (strcmp(out[j].name, env2[i].name) == 0) {
                /* Override */
                free(out[j].value);
                out[j].value = strdup(env2[i].value);
                if (!out[j].value) { free_env(out); return NULL; }
                break;
            }
        }
        if (j == n) {
            /* New variable */
            out[n].name  = strdup(env2[i].name);
            out[n].value = strdup(env2[i].value);
            if (!out[n].name || !out[n].value) { free_env(out); return NULL; }
            n++;
        }
    }

    /* NULL-terminate */
    out[n].name = NULL;
    out[n].value = NULL;

    return out;
}

void free_flat_env(char **flat) {
    if (!flat) return;
    for (size_t i = 0; flat[i]; ++i) free(flat[i]);
    free(flat);
}

/*
 * Flatten env (NULL-terminated env_var_t array) into a new NULL-terminated
 * array of "NAME=VALUE" strings suitable for execve.
 *
 * Returns:
 *   char **flat on success (each element malloc'd, plus the array itself),
 *   NULL on OOM (errno = ENOMEM). On failure, any partial allocations are freed.
 *
 * Notes:
 * - If env[i].value is NULL, we emit "NAME=" (empty value).
 * - NAME is copied verbatim; no validation against '=' is performed.
 */
char **flatten_env(const env_var_t *env) {
    size_t n = env_len(env);
    char **out = (char **)malloc((n + 1) * sizeof(char *));
    if (!out) { errno = ENOMEM; return NULL; }

    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        const char *name  = env[i].name;
        const char *value = env[i].value ? env[i].value : "";

        /* allocate "name=value\0" */
        size_t ln = strlen(name);
        size_t lv = strlen(value);
        char *nv = (char *)malloc(ln + 1 /* '=' */ + lv + 1 /* '\0' */);
        if (!nv) {
            errno = ENOMEM;
            out[j] = NULL;
            free_flat_env(out);
            return NULL;
        }

        memcpy(nv, name, ln);
        nv[ln] = '=';
        memcpy(nv + ln + 1, value, lv);
        nv[ln + 1 + lv] = '\0';

        out[j++] = nv;
    }

    out[j] = NULL;
    return out;
}

int microbox_drop_capabilities(void) {
  /* 1) Prevent gaining new privs (needed for unprivileged seccomp too). */
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
    return (-errno);
  }

  /* 2) Clear ambient set (in case some are present). */
#ifdef PR_CAP_AMBIENT
  if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) == -1) {
    /* Not fatal on old kernels; ignore EINVAL. */
    if (errno != EINVAL) {
      return (-errno);
    }
  }
#endif

  /* 3) Clear Effective, Permitted, Inheritable capability sets */
  struct __user_cap_header_struct hdr;
  struct __user_cap_data_struct data[2];

  memset(&hdr, 0, sizeof(hdr));
  memset(&data, 0, sizeof(data));
  hdr.version = _LINUX_CAPABILITY_VERSION_3;
  hdr.pid = 0; /* self */

  /* All zeros in data[] → empty sets */
  if (syscall(SYS_capset, &hdr, &data) == -1) {
    return -1;
  }

  return (0);
}

/**
 * Merge syscall lists according to microbox policy:
 * - If no deny/allow syscalls specified, use default denylist only
 * - If deny/allow syscalls specified, merge default + user deny lists, with user allow as overrides
 *
 * @param opts The sandbox options containing user-specified syscalls
 * @param final_denylist Output parameter for the final denylist to use
 * @param final_denylist_len Output parameter for the final denylist length
 * @param final_allowlist Output parameter for the final allowlist to use (may be NULL)
 * @param final_allowlist_len Output parameter for the final allowlist length
 * @return 0 on success, -1 on failure. Caller must free *final_denylist if it differs from default.
 */
static int merge_syscall_lists(
    const sandbox_options_t* opts,
    const char*** final_denylist,
    size_t* final_denylist_len,
    const char*** final_allowlist,
    size_t* final_allowlist_len
) {
    size_t default_denylist_len = docker_default_denylist_len();
    const char** default_denylist = microbox_get_docker_default_denylist();

    if (opts->syscalls_deny_len == 0 && opts->syscalls_allow_len == 0) {
        // Case 1: No command-line syscalls specified, use default denylist only
        *final_denylist = default_denylist;
        *final_denylist_len = default_denylist_len;
        *final_allowlist = NULL;
        *final_allowlist_len = 0;
    } else {
        // Case 2: Command-line syscalls specified, merge with default denylist
        // Create merged denylist (default + user deny list)
        *final_denylist_len = default_denylist_len + opts->syscalls_deny_len;
        *final_denylist = xcalloc(*final_denylist_len, sizeof(char*));

        // Copy default denylist
        for (size_t i = 0; i < default_denylist_len; i++) {
            (*final_denylist)[i] = default_denylist[i];
        }

        // Add user-specified deny syscalls
        for (size_t i = 0; i < opts->syscalls_deny_len; i++) {
            (*final_denylist)[default_denylist_len + i] = opts->syscalls_deny[i];
        }

        // Use user-specified allow list as overrides (these take priority)
        *final_allowlist = (const char**)opts->syscalls_allow;
        *final_allowlist_len = opts->syscalls_allow_len;
    }

    return 0;
}

/**
 * Spawns a new process in a new sandbox.
 * @param opts The sandbox options.
 * @param process The sandbox process.
 * @return 0 on success, -1 on failure.
 */
int microbox_sandbox_spawn(const sandbox_options_t* opts, sandbox_process_t* process) {
  sandbox_flag_t flags = CLONE_NEWUSER
    | CLONE_NEWPID
    | CLONE_NEWUTS
    | CLONE_NEWIPC
    | CLONE_PIDFD
    | CLONE_NEWCGROUP
    | CLONE_NEWTIME;

  if (!opts || !process) {
    return (-EINVAL);
  }

  // If host network is used, we don't create a new network namespace.
  if (opts->net_mode != NET_HOST) {
    flags |= CLONE_NEWNET;
  }

  // If using host filesystem, we may opt out of a new mount namespace.
  if (opts->fs_mode != FS_HOST) {
    flags |= CLONE_NEWNS;
  }

  // Set up clone arguments.
  struct clone_args args = {
    .flags = flags,
    .pidfd = (uint64_t)&process->pidfd,
    .exit_signal = SIGCHLD
  };

  int sync_pipe[2];
  MAKE_PIPE(sync_pipe);

  /* Create a new process in a new namespace. */
  pid_t pid = syscall(SYS_clone3, &args, sizeof(args));
  if (pid < 0) {
    close(sync_pipe[0]);
    close(sync_pipe[1]);
    return (-errno);
  }

  if (pid == 0) {
    // Wait for parent to finish uid/gid mapping.
    WAIT_FOR_PARENT(sync_pipe);

    // UTS hostname.
    if (opts->hostname) {
      if (sethostname(opts->hostname, strlen(opts->hostname)) < 0) {
        fprintf(stderr, "sethostname(%s) failed: %s\n", opts->hostname, strerror(errno));
        _exit(127);
      }
    }

    // Setup the sandbox filesystem.
    if (microsandbox_setup_fs(opts) < 0) {
      fprintf(stderr, "microsandbox_setup_fs() failed: %s\n", strerror(errno));
      _exit(127);
    }

    // Configure container networking
    if (opts->net_mode == NET_BRIDGE) {
      netns_config_t net_config = {0};
      // Use parent PID for interface naming consistency
      pid_t parent_pid = getppid();
      if (generate_interface_names(&net_config, parent_pid) != 0 ||
          microbox_configure_container_network(&net_config) != 0) {
        fprintf(stderr, "Failed to configure container networking\n");
        _exit(127);
      }
    }

    // Drop all capabilities.
    // if (microbox_drop_capabilities() < 0) {
    //   fprintf(stderr, "drop caps failed: %s\n", strerror(errno));
    //   _exit(127);
    // }

    // Apply seccomp filter with merged syscall lists.
    const char** final_denylist;
    size_t final_denylist_len;
    const char** final_allowlist;
    size_t final_allowlist_len;

    if (merge_syscall_lists(opts, &final_denylist, &final_denylist_len, &final_allowlist, &final_allowlist_len) < 0) {
      fprintf(stderr, "merge syscall lists failed\n");
      _exit(127);
    }

    if (microbox_setup_seccomp(final_denylist, final_denylist_len, final_allowlist, final_allowlist_len) < 0) {
      fprintf(stderr, "apply seccomp filter failed: %s\n", strerror(errno));
      // Clean up allocated memory if needed
      if (final_denylist != microbox_get_docker_default_denylist()) {
        free((void*) final_denylist);
      }
      _exit(127);
    }

    // Clean up allocated memory if needed
    if (final_denylist != microbox_get_docker_default_denylist()) {
      free((void*)final_denylist);
    }

    // Execute the user-provided binary in the sandbox.
    env_var_t* merged_env = opts->env ? merge(safe_env, opts->env) : (env_var_t*)safe_env;
    char** flat_env = flatten_env(merged_env);

    execve(opts->cmd[0], opts->cmd, flat_env);

    // Clean up before execve (though this won't execute if execve succeeds)
    if (opts->env && merged_env != safe_env) {
        free_env(merged_env);
    }
    free_flat_env(flat_env);

    /* If execve returns, something failed. */
    fprintf(stderr, "execve(%s) failed: %s\n", opts->cmd[0], strerror(errno));
    _exit(127);
  }

  process->pid = pid;

  // Setup UID/GID mapping.
  if (setup_uid_gid_map(pid) < 0) {
    fprintf(stderr, "setup_uid_gid_map() failed: %s\n", strerror(errno));
    return (-errno);
  }

  // Setup bridge networking if requested
  if (opts->net_mode == NET_BRIDGE) {
    netns_config_t net_config = {0};

    if (getuid() != 0) {
      fprintf(stderr, "Bridge networking requires root privileges.\n");
      fprintf(stderr, "Run: sudo ./microbox --net bridge ...\n");
      return (-1);
    }

    if (generate_interface_names(&net_config, pid) != 0) {
      fprintf(stderr, "Failed to generate interface names\n");
      return (-1);
    }

    if (microbox_setup_bridge_network(&net_config) != 0) {
      fprintf(stderr, "Failed to setup bridge network\n");
      return (-1);
    }

    if (move_veth_to_container(&net_config, pid) != 0) {
      fprintf(stderr, "Failed to move veth to container\n");
      return (-1);
    }
  }

  // Setup cgroup limits.
  if (setup_cgroup_limits(pid, opts->cpus, opts->memory) < 0) {
    fprintf(stderr, "setup_cgroup_limits() failed: %s\n", strerror(errno));
    return (-errno);
  }

  // Unblock child
  SIGNAL_CHILD(sync_pipe);

  return (0);
}

/**
 * Wait for the sandboxed process to exit.
 * @param proc The sandbox process to wait for.
 * @return The exit status of the sandboxed process.
 */
int microbox_sandbox_wait(const sandbox_process_t *proc) {
  siginfo_t si = {0};

  if (!proc) {
    return (-EINVAL);
  }

  // Wait for the child process to exit.
  if (waitid(P_PIDFD, proc->pidfd, &si, WEXITED) == -1) {
    return (-errno);
  }

  // Clean up network interfaces after container exits
  if (microbox_cleanup_network_interfaces(proc->pid) != 0) {
    fprintf(stderr, "Warning: Failed to clean up network interfaces for container %d\n", proc->pid);
  }

  if (si.si_code == CLD_EXITED) {
    // The child process exited normally.
    return (si.si_status);
  } else if (si.si_code == CLD_KILLED || si.si_code == CLD_DUMPED) {
    // The child process was killed or crashed.
    return (128 + si.si_status);
  }

  return (0);
}

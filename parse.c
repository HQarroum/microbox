#include "parse.h"
#include "sandbox.h"
#include "utils.h"

/**
 * Parse network mode from string.
 * For example:
 *   1. parse_net_mode("none"); // NET_NONE
 *   2. parse_net_mode("host"); // NET_HOST
 *   3. parse_net_mode("private"); // NET_PRIVATE
 * @param s Network mode string.
 * @return Network mode value or NET_INVALID if invalid.
 */
static net_mode_t parse_net_mode(const char *s) {
  if (!s || !*s) {
    return (NET_NONE);
  }

  // No networking in the sandbox.
  if (!strcmp(s, "none")) {
    return (NET_NONE);
  }

  // Use host network namespace.
  if (!strcmp(s, "host")) {
    return (NET_HOST);
  }

  // Use private network namespace.
  if (!strcmp(s, "private")) {
    return (NET_PRIVATE);
  }

  // Use bridge network namespace.
  if (!strcmp(s, "bridge")) {
    return (NET_BRIDGE);
  }

  return (NET_INVALID);
}

/**
 * Parse file-system mode from string.
 * For example:
 *   1. parse_fs_mode("host"); // FS_HOST
 *   2. parse_fs_mode("private"); // FS_PRIVATE
 *   3. parse_fs_mode("none"); // FS_NONE
 * @param s File-system mode string.
 * @return File-system mode value or FS_INVALID if invalid.
 */
static fs_mode_t parse_fs_mode(const char *s) {
  // Use host filesystem.
  if (!strcmp(s, "host")) {
    return (FS_HOST);
  }

  // Use tmpfs filesystem.
  if (!strcmp(s, "tmpfs")) {
    return (FS_TMPFS);
  }

  return (FS_ROOTFS);
}

/**
 * Print usage information.
 * @param argtable The argument table.
 */
static void print_usage(void **argtable) {
  fprintf(stderr, "Usage:\n  microbox [options] -- command [args...]\n\n");
  arg_print_syntax(stderr, argtable, "\n");
  fprintf(stderr, "\nOptions:\n");
  arg_print_glossary(stderr, argtable, "  %-28s %s\n");
}

/**
 * Parses pretty-printed memory size strings.
 * @param memory The memory size string to parse.
 * @return The parsed memory size in bytes.
 */
static uint64_t parse_memory(const char *memory) {
  uint64_t result = 0;
  char *endptr = NULL;

  if (!memory) {
    return (result);
  }

  result = strtoull(memory, &endptr, 10);
  if (result == ULLONG_MAX && errno == ERANGE) {
    return (0); // Overflow during parsing
  }

  uint64_t multiplier = 1;
  if (*endptr == 'k' || *endptr == 'K') {
    multiplier = 1024;
  } else if (*endptr == 'm' || *endptr == 'M') {
    multiplier = 1024 * 1024;
  } else if (*endptr == 'g' || *endptr == 'G') {
    multiplier = 1024 * 1024 * 1024;
  } else if (*endptr != 'b' && *endptr != 'B' && *endptr != '\0') {
    return (0); // Invalid suffix
  }

  // Check for overflow before multiplication
  if (multiplier > 1 && result > UINT64_MAX / multiplier) {
    return (0); // Would overflow
  }

  return result * multiplier;
}

/**
 * A function to pretty-print the filesystem mode.
 * @param mode The filesystem mode to convert to a string.
 * @return A string representation of the filesystem mode.
 */
const char* fs_mode_to_string(fs_mode_t mode) {
  switch (mode) {
    case FS_TMPFS:
      return "FS_TMPFS";
    case FS_HOST:
      return "FS_HOST";
    case FS_ROOTFS:
      return "FS_ROOTFS";
    default:
      return "FS_UNKNOWN";
  }
}

/**
 * A function to pretty-print the network mode.
 * @param mode The network mode to convert to a string.
 * @return A string representation of the network mode.
 */
const char* net_mode_to_string(net_mode_t mode) {
  switch (mode) {
    case NET_NONE:
      return "NET_NONE";
    case NET_HOST:
      return "NET_HOST";
    case NET_PRIVATE:
      return "NET_PRIVATE";
    case NET_BRIDGE:
      return "NET_BRIDGE";
    case NET_INVALID:
      return "NET_INVALID";
    default:
      return "NET_UNKNOWN";
  }
}

/**
 * A function to pretty-print the filesystem mount mode.
 * @param mode The filesystem mount mode to convert to a string.
 * @return A string representation of the filesystem mount mode.
 */
const char* fs_mount_mode_to_string(mnt_mode_t mode) {
  switch (mode) {
    case MNT_RO:
      return "MNT_RO";
    case MNT_RW:
      return "MNT_RW";
    default:
      return "MNT_UNKNOWN";
  }
}

int find_first_double_dash(int argc, char* argv[]) {
  for (int i = 0; i < argc; ++i) {
    if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == '\0') {
      return (i);
    }
  }
  return (-1);
}

/**
 * Parse command-line options.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return A structure containing the parsed options.
 */
sandbox_options_t cli_parse_options(int argc, char* argv[]) {
  sandbox_options_t o = {0};

  // Default values.
  o.hostname = xstrdup("microbox");
  o.fs_mode  = FS_TMPFS;
  o.net_mode = NET_NONE;

  /* CLI schema */
  struct arg_str *opt_fs            = arg_str0(NULL, "fs",    "host|DIR", "Host filesystem or rootfs DIR");
  struct arg_str *opt_net           = arg_str0(NULL, "net",   "MODE",     "Network: none|host|private|bridge");
  struct arg_lit *opt_proc          = arg_lit0(NULL, "proc",  "Mount /proc in the sandbox");
  struct arg_lit *opt_dev           = arg_lit0(NULL, "dev",   "Mount /dev in the sandbox");
  struct arg_str *opt_mro           = arg_strn(NULL, "mount-ro", "HOST:DEST", 0, 128, "Read-only bind mount");
  struct arg_str *opt_mrw           = arg_strn(NULL, "mount-rw", "HOST:DEST", 0, 128, "Read-write bind mount");
  struct arg_str *opt_env           = arg_strn(NULL, "env", "KEY=VALUE", 0, 128, "Set environment variable");
  struct arg_str *opt_syscall_allow = arg_strn(NULL, "allow-syscall", "SYSCALL", 0, 128, "Allow syscall");
  struct arg_str *opt_syscall_deny  = arg_strn(NULL, "deny-syscall", "SYSCALL", 0, 128, "Deny syscall");
  struct arg_str *opt_hostnm        = arg_str0(NULL, "hostname", "NAME",   "Set container hostname");
  struct arg_dbl *opt_cpus          = arg_dbl0(NULL, "cpus",  "N",         "CPU limit (e.g. 0.5, 2)");
  struct arg_str *opt_mem           = arg_str0(NULL, "memory","SIZE",      "Memory limit (e.g. 10M, 2G)");
  struct arg_lit *opt_help          = arg_lit0(NULL, "help",  "Show help and exit");
  struct arg_end *end               = arg_end(64);

  void *argtable[] = { opt_fs, opt_net, opt_proc, opt_dev, opt_mro, opt_mrw,
                        opt_hostnm, opt_cpus, opt_mem, opt_env, opt_help,
                        opt_syscall_allow, opt_syscall_deny, end };

  // No arguments should be left uninitialized.
  if (arg_nullcheck(argtable) != 0) {
    fprintf(stderr, "failed to parse arguments\n");
    exit(EXIT_FAILURE);
  }

  // Find `--` delimiter.
  int delim_idx = find_first_double_dash(argc, argv);
  if (delim_idx < 0) {
    fprintf(stderr, "Error: missing \"--\" to specify a command to execute.\n");
    print_usage(argtable);
    exit(EXIT_FAILURE);
  }

  // Parse the arguments.
  int nerrors = arg_parse(delim_idx, argv, argtable);
  if (opt_help->count) {
    print_usage(argtable);
    exit(EXIT_SUCCESS);
  }

  // If there are errors, exit.
  if (nerrors) {
    arg_print_errors(stderr, end, "microbox");
    print_usage(argtable);
    exit(EXIT_FAILURE);
  }

  /* --fs parsing: host | DIR */
  if (opt_fs->count) {
    const char *val = opt_fs->sval[0];

    o.fs_mode = parse_fs_mode(val);
    if (o.fs_mode == FS_ROOTFS) {
      o.rootfs = xstrdup(val);
    }
  }

  /* --net parsing */
  if (opt_net->count) {
    o.net_mode = parse_net_mode(opt_net->sval[0]);
    if (o.net_mode == NET_INVALID) {
      fprintf(stderr, "Invalid --net value '%s' (use: none|host|private|bridge)\n", opt_net->sval[0]);
      exit(EXIT_FAILURE);
    }
  }

  // Hostname parsing.
  if (opt_hostnm->count) {
    o.hostname = xstrdup(opt_hostnm->sval[0]);
  }

  // CPU cgroup limits.
  if (opt_cpus->count) {
    o.cpus = opt_cpus->dval[0];
  }

  // Memory cgroup limits.
  if (opt_mem->count) {
    o.memory = parse_memory(opt_mem->sval[0]);
    if (o.memory == 0) {
      fprintf(stderr, "Invalid --mem value '%s'\n", opt_mem->sval[0]);
      exit(EXIT_FAILURE);
    }
  }

  // Mount /proc in the sandbox.
  if (opt_proc->count) {
    o.mount_proc = 1;
  }

  // Mount /dev in the sandbox.
  if (opt_dev->count) {
    o.mount_dev = 1;
  }

  // Set environment variables.
  for (int i = 0; i < opt_env->count; i++) {
    const char *spec = opt_env->sval[i];
    const char *c = strchr(spec, '=');
    if (!c || c==spec || !c[1]) {
      fprintf(stderr, "bad --env: %s\n", spec);
      exit(EXIT_FAILURE);
    }
    char *key = xstrndup(spec, (size_t)(c - spec));
    char *value = xstrdup(c + 1);
    o.env = xrealloc(o.env, sizeof(env_var_t) * (o.env_len + 1));
    o.env[o.env_len++] = (env_var_t){ key, value };
  }

  // Read-only mounts.
  for (int i = 0; i < opt_mro->count; i++) {
    const char *spec = opt_mro->sval[i];
    const char *c = strchr(spec, ':');
    if (!c || c==spec || !c[1]) {
      fprintf(stderr, "bad --mount-ro: %s\n", spec);
      exit(EXIT_FAILURE);
    }
    char *host = xstrndup(spec, (size_t)(c - spec));
    char *dest = xstrdup(c + 1);
    o.mounts = xrealloc(o.mounts, sizeof(mount_spec_t) * (o.mounts_len + 1));
    o.mounts[o.mounts_len++] = (mount_spec_t){ host, dest, MNT_RO };
  }

  // Read-write mounts.
  for (int i = 0; i < opt_mrw->count; i++) {
    const char *spec = opt_mrw->sval[i];
    const char *c = strchr(spec, ':');
    if (!c || c==spec || !c[1]) {
      fprintf(stderr, "bad --mount-rw: %s\n", spec);
      exit(EXIT_FAILURE);
    }
    char *host = xstrndup(spec, (size_t)(c - spec));
    char *dest = xstrdup(c + 1);
    if (dest[0] != '/') {
      fprintf(stderr, "bad --mount: %s, dest must be an absolute path\n", spec);
      exit(EXIT_FAILURE);
    }
    o.mounts = xrealloc(o.mounts, sizeof(mount_spec_t) * (o.mounts_len + 1));
    o.mounts[o.mounts_len++] = (mount_spec_t){ host, dest, MNT_RW };
  }

  /* positionals → argv */
  if (delim_idx < argc - 1) {
    int cmd_start = delim_idx + 1;
    int cmdc = argc - cmd_start;
    o.cmd = xcalloc((size_t)cmdc + 1, sizeof(char*));
    for (int i = 0; i < cmdc; ++i) {
      o.cmd[i] = xstrdup(argv[cmd_start + i]);
    }
    o.cmdc = cmdc;
  } else {
    fprintf(stderr, "Error: missing command\n");
    fprintf(stderr, "Usage: %s [options] <command> [args...]\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // Allowed syscalls.
  if (opt_syscall_allow->count > 0) {
    o.syscalls_allow = xcalloc((size_t) opt_syscall_allow->count + 1, sizeof(*o.syscalls_allow));
    for (int i = 0; i < opt_syscall_allow->count; ++i) {
      const char *syscall = opt_syscall_allow->sval[i];
      o.syscalls_allow[o.syscalls_allow_len++] = xstrdup(syscall);
    }
  }

  // Denied syscalls.
  if (opt_syscall_deny->count > 0) {
    o.syscalls_deny = xcalloc((size_t) opt_syscall_deny->count + 1, sizeof(*o.syscalls_deny));
    for (int i = 0; i < opt_syscall_deny->count; ++i) {
      const char *syscall = opt_syscall_deny->sval[i];
      o.syscalls_deny[o.syscalls_deny_len++] = xstrdup(syscall);
    }
  }

  /* Cross-option policy checks */
  if (o.fs_mode == FS_HOST && (o.mounts_len > 0)) {
    // optional: allow binds on host root, but simplest is to reject
    fprintf(stderr, "--fs host conflicts with --mount-* (requires private mount ns)\n");
    exit(EXIT_FAILURE);
  }
  if (o.fs_mode == FS_HOST && o.net_mode == NET_PRIVATE) {
    fprintf(stderr, "--net private has no effect with --fs host unless you also isolate mounts\n");
  }

  arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
  return (o);
}

/**
 * Free memory allocated for command-line options.
 * @param o Pointer to the options structure.
 * @return 0 on success, -1 on failure.
 */
int cli_free_options(sandbox_options_t* o) {
  if (!o) {
    errno = EINVAL;
    return (-1);
  }

  // Free hostname.
  if (o->hostname) {
    free(o->hostname);
    o->hostname = NULL;
  }

  // Free rootfs
  if (o->rootfs) {
    free(o->rootfs);
    o->rootfs = NULL;
  }

  // Free mounts
  if (o->mounts) {
    for (size_t i = 0; i < o->mounts_len; i++) {
      free(o->mounts[i].host);
      free(o->mounts[i].dest);
      o->mounts[i].host = NULL;
      o->mounts[i].dest = NULL;
    }
    free(o->mounts);
    o->mounts = NULL;
    o->mounts_len = 0;
  }

  // Free cmd array (just the array, not the strings)
  if (o->cmd) {
    for (int i = 0; i < o->cmdc; i++) {
      free(o->cmd[i]);
      o->cmd[i] = NULL;
    }
    free(o->cmd);
    o->cmd = NULL;
    o->cmdc = 0;
  }

  // Special mounts.
  o->mount_proc = 0;
  o->mount_dev = 0;

  return (0);
}

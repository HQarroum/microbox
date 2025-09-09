#include <inttypes.h>
#include "parse.h"
#include "sandbox.h"

static void dump_parameters(sandbox_options_t* opts) {
  printf("Filesystem: %s\n", fs_mode_to_string(opts->fs_mode));
  printf("Rootfs: %s\n", opts->rootfs);
  printf("Network: %s\n", net_mode_to_string(opts->net_mode));
  printf("Hostname: %s\n", opts->hostname);
  printf("CPU allocation: %f\n", opts->cpus);
  printf("Memory allocation:%" PRIu64 "\n", opts->memory);

  // List the mounts.
  for (size_t i = 0; i < opts->mounts_len; i++) {
    printf("Source: %s, Destination: %s, Mode: %s\n",
      opts->mounts[i].host,
      opts->mounts[i].dest,
      fs_mount_mode_to_string(opts->mounts[i].mode)
    );
  }

  // List the environment variables.
  for (size_t i = 0; i < opts->env_len; i++) {
    printf("Env Variable: %s, Value: %s\n",
      opts->env[i].name,
      opts->env[i].value
    );
  }

  // List the allowed syscalls.
  for (size_t i = 0; i < opts->syscalls_allow_len; i++) {
    printf("Allowed Syscall: %s\n", opts->syscalls_allow[i]);
  }
}

/**
 * Application entry point.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return Exit status.
 */
int main(int argc, char* argv[]) {
  sandbox_options_t opts = cli_parse_options(argc, argv);
  sandbox_process_t proc = {
    .pidfd = -1,
    .pid = -1
  };

  dump_parameters(&opts);

  // Create a new sandbox.
  int code = microbox_sandbox_spawn(&opts, &proc);
  if (code < 0) {
      fprintf(stderr, "Failed to spawn sandbox process: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }

  // Wait for the sandbox process to finish.
  int status = microbox_sandbox_wait(&proc);

  // Free memory allocated for options.
  cli_free_options(&opts);

  return (status);
}

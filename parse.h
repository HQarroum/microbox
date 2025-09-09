#ifndef ARG_PARSE_H
#define ARG_PARSE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#include "argtable3.h"
#include "sandbox.h"

/**
 * Parse command-line options.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return A structure containing the sandbox options.
 */
sandbox_options_t cli_parse_options(int argc, char* argv[]);

/**
 * Free memory allocated for command-line options.
 * @param o Pointer to the options structure.
 * @return 0 on success, -1 on failure.
 */
 int cli_free_options(sandbox_options_t* o);

/**
 * A function to pretty-print the filesystem mode.
 * @param mode The filesystem mode to convert to a string.
 * @return A string representation of the filesystem mode.
 */
const char* fs_mode_to_string(fs_mode_t mode);

/**
 * A function to pretty-print the network mode.
 * @param mode The network mode to convert to a string.
 * @return A string representation of the network mode.
 */
const char* net_mode_to_string(net_mode_t mode);

/**
 * A function to pretty-print the network mode.
 * @param mode The network mode to convert to a string.
 * @return A string representation of the network mode.
 */
const char* fs_mount_mode_to_string(mnt_mode_t mode);

#endif

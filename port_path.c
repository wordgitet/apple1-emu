#include "port.h"

/*
 * port_path.c -- Default data-path resolution for port backends.
 *
 * Hosted ports pass paths through unchanged.
 */

char *
port_resolve_data_path(const char *path)
{
	if (path == NULL) {
		return (NULL);
	}
	return (port_strdup(path));
}

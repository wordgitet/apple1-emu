#include "port.h"

/*
 * port_path.c -- Default data-path resolution for port backends.
 *
 * Hosted ports pass paths through unchanged.  TI-Nspire overrides this in
 * port_nspire.c (Ndless documents dir + locate() fallback).
 */

#ifndef APPLE1_PORT_NSPIRE

char *
port_resolve_data_path(const char *path)
{
	if (path == NULL) {
		return (NULL);
	}
	return (port_strdup(path));
}

#endif /* !APPLE1_PORT_NSPIRE */

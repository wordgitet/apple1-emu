#ifndef PORT_STDARG_H
#define PORT_STDARG_H

#ifdef APPLE1_PORT_PLAN9
#ifdef APPLE1_PORT_PLAN9_APE
#include <stdarg.h>
#else
#include "port_stdarg_plan9.h"
#endif
#else
#include "port_stdarg_host.h"
#endif

#endif /* PORT_STDARG_H */

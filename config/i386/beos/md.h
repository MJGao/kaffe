/*
 * i386/beos/md.h
 * BeOS i386 configuration information.
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * Copyright (c) 2003
 *      Kaffe.org contributors. See ChangeLog for details.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#ifndef __i386_beos_md_h
#define __i386_beos_md_h

#include "i386/common.h"
#include "i386/sysdepCallMethod.h"
#include "i386/threads.h"

/*
 * Redefine stack pointer offset.
 */
#undef	SP_OFFSET
#define	SP_OFFSET	4

/*
 * BeOS limits each thread stack to "about 256K".  Let's be conservative
 * and assume it's much less.
 */
#define MAINSTACKSIZE	(220*1024)

#define	SIGNAL_ARGS(sig, sc) int sig, void* userdata, struct vregs* sc
#define SIGNAL_CONTEXT_POINTER(scp) struct vregs * scp
#define GET_SIGNAL_CONTEXT_POINTER(sc) (sc)
#define SIGNAL_PC(scp) ((scp)->eip)

#if defined(TRANSLATOR)
#include "jit-md.h"
#endif

#endif

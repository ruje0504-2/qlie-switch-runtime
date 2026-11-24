// SPDX-License-Identifier: GPL-2.0-or-later
// Mesa Fossilize uses the POSIX stdio locking entry points. devkitA64's
// newlib exposes each FILE lock, but does not export these two wrappers.
#include <stdio.h>
#include <sys/lock.h>
void flockfile(FILE *stream) { __lock_acquire_recursive(stream->_lock); }
void funlockfile(FILE *stream) { __lock_release_recursive(stream->_lock); }

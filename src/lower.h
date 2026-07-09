#ifndef C99M_LOWER_H
#define C99M_LOWER_H

#include "sema.h"
#include <mtlc/build.h>
#include <mtlc/mtlc.h>

/* Lower a type-checked program to a libmtlc module. Returns NULL on failure. */
MtlcModule *lower_program(Sema *S, Program *prog, Diag *diag);

#endif /* C99M_LOWER_H */

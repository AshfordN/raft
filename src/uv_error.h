/* Error codes and utilities. */

#ifndef UV_ERROR_H_
#define UV_ERROR_H_

#include <uv.h>

#include "err.h"

/* Error codes. */
enum {
    UV__ERROR = 1, /* Generic system error. */
};

/* Format an error message caused by a failed system call or stdlib function. */
#define UvErrMsgSys(ERRMSG, SYSCALL, ERRNUM)             \
    {                                                    \
        ErrMsgPrintf(ERRMSG, "%s", uv_strerror(ERRNUM)); \
        ErrMsgWrapf(ERRMSG, SYSCALL);                    \
    }

#endif /* UV_ERROR_H_ */

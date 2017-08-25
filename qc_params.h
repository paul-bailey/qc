/*
 * Tunable QC parameters
 */
#ifndef QC_PARAMS_H
#define QC_PARAMS_H

#ifndef QC_H
#error "Do not call qc_params.h directly. Use qc.h instead"
#endif

#ifndef FALSE
# define FALSE 0
#endif
#ifndef TRUE
# define TRUE (!FALSE)
#endif
#if __STDC_VERSION__ < 199901L
# ifndef restrict
#  define restrict __restrict
# endif
#endif

/* TODO: Rename these something cleaner */

/*
 * Size of global-function hash table. Prime number is hash-optimal.
 * The actual number of permissible global functions depends on memory
 * available.
 */
#define NUM_FUNC         71

/* What I just said about NUM_FUNC, say again for NUM_GLOBAL_VARS */
#define NUM_GLOBAL_VARS  71

/*
 * Size of the local variable stack. This affect the number of function
 * calls.
 */
#define NUM_LOCAL_VARS   1024

/* Max number of internal function parameters */
#define NUM_INTL_ARGS    10

/*
 * Max number of characters per identifier, not counting terminating
 * NUL char.
 */
#define ID_LEN           31

#define FUNC_CALLS       31

#define NUM_PARAMS       31

/* Number of bytes per loadable program */
#define PROG_SIZE        10000

/* Max number of nested `for' loops */
#define FOR_NEST         31

/* Max length of a token */
#define TOKEN_LEN        80

/* Max number of string literals */
#define QC_N_STRINGS     100

/* Max length of string literals */
#define QC_STRING_LEN    512

/* Max number of open files. This may be further limited by the
 * system. */
#define QC_NFILES        20

/* Max number of file-scope variables. Prime number is hash-optimal */
#define NUM_STATIC_VARS  71

/* Max number of file-scope functions, Prime number is hash-optimal */
#define NUM_STATIC_FUNC  71

/* Max size of an array declared inside a function */
#define QC_LARRAY_MAX    NUM_LOCAL_VARS

/* Max size of an array declared outside a function */
#define QC_GARRAY_MAX    1000



#endif /* QC_PARAMS_H */

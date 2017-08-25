#ifndef QC_H
#define QC_H

#include <setjmp.h>
#include <stdio.h>
#include "qc_params.h"

/*
 * These are the constants used to call qcsyntax() when a syntax error
 * occurs.
 */
enum QC_ERROR_T {
        QCE_SYNTAX = 1, /* for generic errors */
        QCE_UNBAL_PARENS,
        QCE_NO_EXP,
        QCE_EQUALS_EXPECTED,
        QCE_NOT_VAR,
        QCE_PARAM_ERR,
        QCE_SEMI_EXPECTED,
        QCE_UNBAL_BRACES,
        QCE_FUNC_UNDEF,
        QCE_TYPE_EXPECTED,
        QCE_NEST_FUNC,
        QCE_RET_NOCALL,
        QCE_PAREN_EXPECTED,
        QCE_WHILE_EXPECTED,
        QCE_QUOTE_EXPECTED,
        QCE_NOT_TEMP,
        QCE_TOO_MANY_LVARS,
        QCE_TOO_MANY_GVARS,
        QCE_ARG_EXPECTED,
        QCE_COMMA_EXPECTED,
        QCE_FILE_NOT_P,
        QCE_TOO_MANY_ARGS,
        QCE_OVERSIZE_STRING,
        QCE_TOO_MANY_FILES,
        QCE_TOO_MANY_STRINGS,
        QCE_NOMEM,
        QCE_FATAL,
        QCE_NOFILE,
        QCE_UNBAL_COMMENT,
        QCE_UNK_TYPE,
        QCE_TYPE_MISMATCH,
        QCE_TYPE_INVAL,
        QCE_IDENTIFIER_EXPECTED,
        QCE_UNINIT,
        QCE_NAMES_MATCH,
        QCE_NOMAIN,
        QCE_DEREF,
        QCE_BOUND_ERR,
        QCE_PTR_REF_ERR,
        QCE_ARRAYSIZE_NOT_LIT,
        QCE_DBL_PTR,
        QCE_ARRAY_TOO_BIG,
        QCE_SQUBRACE_EXPECTED,
        QCE_ARRAY_INITIALIZER,
        QCE_INSANE_SHIFT,
        QCE_ARRAY_BOUNDS,
        QCE_NERRS,
};

enum QC_TOKENS {
#define QC_PTR          0x0100
#define QC_TYPE         0X0200
#define QC_ARG          0x0400
#define QC_UNSIGNED     0x0800
#define QC_FLTFLG       0x1000
#define QC_STATIC       0x2000
#define QC_VDFLG        0x4000 /* Additional flag for void, for faster checks */

        /* Types */
        QC_CHAR = 1,
        QC_INT,
        QC_FILE,
        QC_DBL,
        QC_FLT,
        QC_VOID,

        /* Keywords */
        QC_IF,
        QC_ELSE,
        QC_FOR,
        QC_DO,
        QC_WHILE,
        QC_SWITCH,
        QC_RETURN,
        QC_EOL,
        QC_EMPTY,
        QC_FINISHED,
        QC_NULL,
        QC_BREAK,

        /* Variables, functions */
        QC_IDENTIFIER,

        /* Literals */
        QC_NUMBER,
        QC_STRING,

        QC_LT,          /* `<' */
        QC_LE,          /* `<=' */
        QC_GT,          /* `>' */
        QC_GE,          /* `>=' */
        QC_EQ,          /* `==' */
        QC_NE,          /* `!=' */

        QC_LAND,        /* `&&' */
        QC_LOR,         /* `||' */

        QC_LSL,         /* `<<' */
        QC_LSR,         /* `>>' */

        QC_PLUSPLUS,    /* `++' */
        QC_MINUSMINUS,  /* `--' */
        QC_ANDEQ,       /* `&=' */
        QC_OREQ,        /* `|=' */
        QC_PLUSEQ,      /* `+=' */
        QC_MINUSEQ,     /* `-=' */
        QC_XOREQ,       /* `^=' */
        QC_DIVEQ,       /* `/=' */
        QC_MULEQ,       /* `*=' */
        QC_MODEQ,       /* `%=' */
        QC_LSLEQ,       /* `<<=' */
        QC_LSREQ,       /* `>>=' */
        QC_EQEQ,        /* `=' */

        QC_OPENSQU,     /* `[' */
        QC_CLOSESQU,    /* `]' */
        QC_OPENBR,      /* `{' */
        QC_CLOSEBR,     /* `}' */
        QC_OPENPAREN,   /* `(' */
        QC_CLOSEPAREN,  /* `)' */

        QC_SEMI,        /* `;' */
        QC_COMMA,       /* `,' */
        QC_COLON,       /* ':' */

        QC_MULTOK,      /* `*' */
        QC_DIVTOK,      /* `/' */
        QC_MODTOK,      /* `%' */
        QC_PLUSTOK,     /* `+' */
        QC_MINUSTOK,    /* `-' */
        QC_ANDTOK,      /* `&' */
        QC_ORTOK,       /* `|' */
        QC_XORTOK,      /* `^' */
        QC_LNOTTOK,     /* `!' */
        QC_ANOTTOK,     /* `~' */
};

#define QC_UINT         (QC_UNSIGNED | QC_INT)
#define QC_UCHAR        (QC_UNSIGNED | QC_CHAR)
#define QC_UINTPTR      (QC_PTR | QC_UINT)
#define QC_UCHARPTR     (QC_PTR | QC_UCHAR)
#define QC_INTPTR       (QC_PTR | QC_INT)
#define QC_CHARPTR      (QC_PTR | QC_CHAR)
#define QC_FILEPTR      (QC_PTR | QC_FILE)
#define QC_FLTPTR       (QC_PTR | QC_FLT)
#define QC_DBLPTR       (QC_PTR | QC_DBL)
#define QC_VOIDPTR      (QC_PTR | QC_VOID | QC_VDFLG)

/**
 * typedef qctoken_t - Token data type.
 *
 * This token is encoded as follows:
 *
 * Bits 0 to 7 contain the `enum QC_TOKEN' description of the token,
 * or zero if it is not a keyword.
 *
 * Bits 16 to 24 contain the `enum TOK_TYPES' description of the
 * token. use the QC_TOKEN_TYPE() macro to decode this, QC_TOKEN_CLRTYPE()
 * macro to clear this in a variable, and the QC_TOKEN_MKTYPE() macro to
 * encode this.
 *
 * Bits 8 to 15 contain additional flags for determining if the token
 * is a data type, a pointer, and argument, etc. Use QC_ISPTR(),
 * QC_ISARG(), QC_ISTYPE() macros to determine this.
 *
 * The flags are also used with the other enumerations. Use QC_TYPEOF()
 * to get the data type of a token; macros QC_INTPTR, etc., can be used
 * in the same was as QC_INT for checking data types.
 */
typedef unsigned short qctoken_t;

#define QC_TOK_MASK             (0x7FU)

/*
 * Macros to encode/decode an qctoken_t variable. See comments to
 * qctoken_t above.
 */

/* Data type of token `tk', or zero if token is not a data type */
#define QC_TYPEOF(tk) \
        ((tk) & (QC_TOK_MASK | QC_PTR | QC_VDFLG))

/* True if token `tk' is a data type */
#define QC_ISTYPE(tk) \
        (((tk) & QC_TYPE) != 0)

/* True if token `tk' is a pointer */
#define QC_ISPTR(tk) \
        (((tk) & QC_PTR) != 0)

/* True if token type is CHAR and PTR */
/* XXX: Not very safe: is string literal, or pointer into char array? */
#define QC_ISSTRING(tk) \
        (QC_TYPEOF(tk) == QC_CHARPTR)

/* True if token `tk' is a floating point number */
#define QC_ISFLT(tk) \
        (((tk) & QC_FLTFLG) != 0)

/* True if token `tk' is neither floating point nor pointer */
#define QC_ISINT(tk) \
        (((tk) & (QC_FLTFLG | QC_PTR | QC_VDFLG)) == 0)

/* True if token `tk' is void and not a pointer */
#define QC_ISVOID(tk) \
        (((tk) & (QC_VDFLG | QC_PTR)) == QC_VDFLG)

#define QC_ISSIGNED(tk) \
        (((tk) & QC_UNSIGNED) == 0)

/* True if token `tk' is an argument */
#define QC_ISARG(tx) \
        (((tk) & QC_ARG) != 0)

/* Get `enum QC_TOKENS' as decoded from token `tk' */
#define QC_TOK(tk) \
        ((tk) & QC_TOK_MASK)

/* Determine if a function or variable was declared `static'. */
#define QC_ISSTATIC(tk) (((tk) & QC_STATIC) != 0)

/**
 * @brief Type for calculated has values.
 */
typedef long int hash_t;

/**
 * typedef Atom - Atomic datum type for li'l c.
 *
 * This is the core interface type when passing parameters,
 * either between one user-defined function and another, or
 * between the user's program and the internal `library'
 * function calls.
 *
 * The a_type field is only meant for type checking, but it
 * should be decoded with the same macros that are used with
 * the variable qc_token.
 *
 * qcexpression() initializes a Atom struct, including the
 * type. That is, a Atom struct's value and type depend on
 * how an expression is evaluated (eg during the recursive
 * descent of an evaluation there may be a float initially
 * that is then operated on by comparing it to another float,
 * thus turning it into an int.) This weak typing is only
 * via the qcexpression() node. (See comments on the Variable
 * struct and assign_var() for more on that.)
 */
typedef struct Atom {
        qctoken_t a_type;
        union {
                char c;
                unsigned char uc;
                int i;
                unsigned int ui;
                long li;
                unsigned long int uli;
                long long lli;
                unsigned long long ulli;
                float f;
                double d;
                void *p;
        } a_value;
} Atom;

/* XXX: Lots of this can be private data */
/**
 * typedef Variable - Variable descriptor.
 * @v_aidx: Index in its array (usu. 0)
 * @v_asize: Array size (usu. 1)
 * @v_array: Array pointer for global variables. Local variable arrays
 *         will be pushed onto the stack (to spare a flood of malloc and
 *         free calls during program run time); this will point to NULL
 *         if the variable is local, or if the variable is not an
 *         array.
 * @v_hash: Hash number
 * @v_next: Next in hash table collision list.
 *
 * The variable's Atom struct is different in that once its
 * type is declared, it will not change. assign_var() will
 * cast the value of its Atom parameter (without side effects) before
 * assigning that value to a variable.
 */
typedef struct Variable {
        char v_name[ID_LEN + 1];
        unsigned char v_flag;
        unsigned char v_aidx;
        unsigned char v_asize;
        Atom v_datum;
        Atom *v_array;
        hash_t v_hash;
        struct Variable *v_next;
} Variable;
#define v_value v_datum.a_value
#define v_type  v_datum.a_type

#define QC_VFLAG_INITIALIZED 0X01
#define QC_VFLAG_CONST       0X02
#define QC_VFLAG_ARRAY       0X04

#define QC_ISINIT(v)  (((v)->v_flag & QC_VFLAG_INITIALIZED) != 0)
#define QC_ISARRAY(v) (((v)->v_flag & QC_VFLAG_ARRAY) != 0)

struct Namespace;

/**
 * union function_ptr_t - QC function pointer
 * @u:  For user-defined function, pointer to the start of the function's
 *      execution block in the program counter.
 * @i:  Pointer to an internal function
 */
union function_cb_t {
        char *u;
        void (*i)(Atom *);
};

/**
 * typedef Function - Function descriptor
 * @fname: This is in its own array (copied during prescan) to avoid
 *      using pointers into the program counter, or to avoid (the
 *      other option) a bewildering amount of calls to malloc()
 *      for short strings.
 * @f_fn: Function pointer
 * @f_call: Call method.  Should be one of qc_ufunc_call() (for user
 *      functions) or qc_ifunc_call() (for internal functions).
 * @f_reg: Data type of the return value.  During the expression parsing,
 *      expression types will be changed to this.
 * @f_minargs:
 * @f_maxargs:  Until we support varargs, keep @f_minargs and @f_maxargs
 *      equal.
 * @f_namespace: Handle to the function's private functions, variables,
 *      etc.
 * @f_next: Next function in the hash table collision list.
 * @f_hash: Hash number
 */

typedef struct Function {
        char f_name[ID_LEN + 1];
        union function_cb_t f_fn;
        void (*f_call)(Atom *ret, struct Function *fn);
        qctoken_t f_ret;
        unsigned char f_minargs;
        unsigned char f_maxargs;
        struct Namespace *f_namespace;
        struct Function *f_next;
        hash_t f_hash;
} Function;

struct qc_ustring_t {
        char *s;
        char *p;
        char *e;
};

typedef struct Namespace {
        const char *filepath;
        struct qc_ustring_t ustrings[QC_N_STRINGS];
        int n_ustrings;
        Function fn_hashtbl[NUM_STATIC_FUNC];
        Variable var_hashtbl[NUM_STATIC_VARS];
        char program_buffer[PROG_SIZE];
        struct Namespace *list;
} Namespace;

/* Pointer to current function's namespace */
extern Namespace *qc_namespace;

/*
 * Dummy the va_list for now. This will mean somthing (And be cleaner)
 * in the future.
 */
struct printf_reent_t {
    void (*fn)(int, struct printf_reent_t *);
    union {
        char *ptr;
        FILE *stream;
    } out;
    int count;
    int limit;
};

/*
 * va_list/va_arg support. This will expand later.
 */
typedef void * qc_va_list;
#define qc_va_arg(ap, type)            \
      (sizeof(type) == sizeof(int)     \
        ? qc_va_iarg_int(ap)           \
        : sizeof(type) == sizeof(long) \
          ? qc_va_iarg_long(ap)        \
          : qc_va_iarg_longlong(ap))

/* XXX: These should be denoted some kind of `private' */
extern int qc_va_iarg_int(qc_va_list args);
extern long qc_va_iarg_long(qc_va_list args);
extern long long qc_va_iarg_longlong(qc_va_list args);
extern void qc_va_start(qc_va_list args, Variable *vstart);

/* qcfunction.c */
extern Atom *iarg_pop(void);

/* qcread.c */
extern void qc_exit(int ret);
extern int qc_main(char *fname);

/* qclib.c (Our library functions) */
extern void qccall_fopen(Atom *ret);
extern void qccall_fclose(Atom *ret);
extern void qccall_fputs(Atom *ret);
extern void qccall_puts(Atom *ret);
extern void qccall_printf(Atom *ret);
extern void qccall_getchar(Atom *ret);
extern void qccall_exit(Atom *ret);

extern void qclib_exit(void);
extern void qclib_init(void);


/* qcerr.c */
extern void qcsyntax(int error);

/* qcread.c (?) */
int qc_execute(const char *funcname, Atom *ret, Atom args[], int nargs);
int qc_load_file(const char *fname);
int qc_init(void);

/* qcprint.c */
extern void qcprint_r(const char *restrict format, qc_va_list args,
                      struct printf_reent_t *restrict r);

#endif /* QC_H */

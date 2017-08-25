/* QC function call/return and variable assign/lookup handler */

#include "qc.h"
#include "qc_private.h"
#include <string.h>
#include <stdlib.h>

/* For minibuf stuff */
#include <ctype.h>


static Atom qc_return_val;

/* Hash table for public functions */
static Function qc_function_hashtbl[NUM_FUNC];
static Variable qc_gvar_hashtbl[NUM_GLOBAL_VARS];

/*
 * Stack for user-defined local (ie inside a function) variables.
 */
static Variable qc_lvar_stack[NUM_LOCAL_VARS];
static int qc_lvar_tos = 0;

/*
 * Stack of function calls (kind of like a stack of link reg.
 * values)
 */
static int qc_func_stack[NUM_FUNC];
static int qc_func_tos = 0;

/*
 * Stack of arguments into internal one function call
 */
static Atom qc_iarg_stack[NUM_INTL_ARGS];
static int qc_iarg_tos = 0;


static int qc_get_iargs_from_ufunc(void);
static void qc_ifunc_call(Atom *ret, struct Function *fn);
static void qc_ufunc_call(Atom *ret, struct Function *fn);
static void qc_push_uargs(void);
static void qc_get_uparams(void);
static int qc_ufunc_pop(void);
static void qc_ufunc_push(int i);
static void local_push(Variable *v);
static Variable *qc_local_uvar_lookup(const char *s);
static Variable *qc_global_uvar_lookup(const char *s);
static qctoken_t qc_get_type(void);
static void qc_fn_collision_insert(Function *new, Function *root);
static void qc_gvar_collision_insert(Variable *new, Variable *root);
static void qc_push_uargs_from_minibuf(void);

/*
 * Table of internal functions. They will be copied and hased
 * during initialization.
 */
#define IFUNC_PARAMS(nm, min, max, typ)  \
        { .f_name    = { #nm },            \
          .f_fn.i    = qccall_##nm,        \
          .f_minargs = (min),            \
          .f_maxargs = (max),            \
          .f_ret     = (typ),            \
          .f_next    = NULL,               \
          .f_hash    = -1,                 \
          .f_call    = qc_ifunc_call, }
#define IFUNC_END \
        { .f_name = { '\0' }, .f_fn.i = NULL, .f_ret = 0 }
static Function qc_ifunc_tbl[] = {
        IFUNC_PARAMS(fopen,  2, 2, QC_TYPE | QC_FILE | QC_PTR),
        IFUNC_PARAMS(fclose, 1, 1, QC_TYPE | QC_INT),
        IFUNC_PARAMS(fputs,  2, 2, QC_TYPE | QC_INT),
        IFUNC_PARAMS(exit,   1, 1, QC_TYPE | QC_INT),
        IFUNC_PARAMS(puts,   1, 1, QC_TYPE | QC_CHAR | QC_PTR),
        IFUNC_PARAMS(printf, 1, 9, QC_TYPE | QC_INT),
        IFUNC_PARAMS(getchar,0, 0, QC_TYPE | QC_INT),
        IFUNC_END,
};

/* Bottom of local var stack for current function only */
static inline Variable *qc_lvar_stack_bottom(void)
{
        int i = qc_func_stack[qc_func_tos - 1];
        return &qc_lvar_stack[i];
}

static inline Variable *qc_lvar_stack_top(void)
{
        return &qc_lvar_stack[qc_lvar_tos - 1];
}

/* Helpers to clear out entries in hash tables */
static void qc_function_hashinit1(Function *t)
{
        t->f_hash = -1;
        t->f_name[0] = '\0';
        t->f_next = NULL;
}

static void qc_gvar_hashinit1(Variable *v)
{
        v->v_hash = -1;
        v->v_name[0] = '\0';
        v->v_next = NULL;
}

/* TODO: Get rid of this when porting into vmebr, and put
 * token_next_atom() in token.c */
#define token_next() NULL

int token_next_atom(Atom *a)
{
        char *s;
        char *endptr;
        long long int intval;

        s = token_next();
        if (s == NULL)
                return 0;

        if (isdigit(*s) || *s == '.') {
                intval = strtoll(s, &endptr, 0);
                switch (toupper(*endptr)) {
                case 'U':
                        a->a_value.ui = intval;
                        a->a_type = QC_UINT;
                        break;
                case 'F':
                        a->a_value.f = strtof(s, NULL);
                        a->a_type = QC_FLT | QC_FLTFLG;
                        break;
                case '.':
                case 'E':
                        a->a_value.d = strtod(s, NULL);
                        a->a_type = QC_DBL | QC_FLTFLG;
                        break;
                default:
                        a->a_value.i = intval;
                        a->a_type = QC_INT;
                        break;
                }
        } else if (*s == '"') {
                /* FIXME: This needs to be added to ustrings */
                a->a_type = QC_CHAR | QC_PTR;
                a->a_value.p = s;
        } else {
                /* TODO: Invalid arg warning */
                return 0;
        }
        return 1;
}

/*
 * Common to qc_get_iargs_from_minibuf() and
 * qc_get_iargs_from_ufunc(). `top' is the top of
 * a temporary array in those functions and count
 * is the array length.
 */
static void qc_iarg_push(Atom *top, int count)
{
        while (count > 0) {
                /* Push onto qc_iarg_stack */
                top->a_type |= QC_ARG;
                if (qc_iarg_tos >= NUM_INTL_ARGS)
                        qcsyntax(QCE_TOO_MANY_LVARS);
                memcpy(&qc_iarg_stack[qc_iarg_tos],
                        top,
                        sizeof(*top));
                qc_iarg_tos++;
                --count;
                --top;
        }
}

/*
 * Get arguments from the command interpreter (either from a
 * file or minibuffer -- the token_next() interface will do for
 * both before calling an internal function.
 *
 * Return:
 *  Number of arguments.
 */
static int qc_get_iargs_from_minibuf(void)
{
        Atom params[NUM_PARAMS];
        Atom *v;
        int count = 0;
        int res;

        v = &params[0];

        while ((res = token_next_atom(v)) != 0) {
                ++count;
                ++v;
                if (count == NUM_PARAMS)
                        break;
        }

        /* now, push on qc_lvar_stack in reverse order */
        --v;
        qc_iarg_push(v, count);
        return count;
}

/*
 * Get internal function parameters.
 * Since there is no user declarartion of the function, we get the
 * parameters in a way similar to get_uparams()
 */
static int qc_get_iargs_from_ufunc(void)
{
        Atom var;
        int count;
        Atom params[NUM_PARAMS];
        Atom *v;

        qc_lex();
        if (QC_TOK(qc_token) != QC_OPENPAREN)
                qcsyntax(QCE_PAREN_EXPECTED);

        /* proces a comma-separacted list of values */
        v = &params[0];
        count = 0;
        do {
                qc_lex();

                if (QC_TOK(qc_token) == QC_STRING) {
                        /* Get string, push on stack */
                        v->a_type = qc_token | QC_PTR;
                        v->a_value.p = qc_token_string;
                } else if (QC_TOK(qc_token) == QC_CLOSEPAREN) {
                        /* Break before incrementing count. We're done */
                        break;
                } else {
                        qcputback();
                        qcexpression(&var);
                        /* save temporarily */
                        memcpy(v, &var, sizeof(Atom));
                }

                qc_lex();
                ++count;
                ++v;
                if (count == NUM_PARAMS)
                        qcsyntax(QCE_SYNTAX);
        } while (QC_TOK(qc_token) == QC_COMMA);

        if (QC_TOK(qc_token) != QC_CLOSEPAREN)
                qcsyntax(QCE_UNBAL_PARENS);

        /* now, push on qc_lvar_stack in reverse order */
        --v;
        qc_iarg_push(v, count);
        return count;
}

/*
 * Call routine for internal functions. This does not save stack
 * pointers, because we will not permit library calls that call
 * back to user-defined calls.
 */
static void qc_ifunc_call(Atom *ret, struct Function *fn)
{
        int nargs;

        if (qc_namespace == NULL)
                nargs = qc_get_iargs_from_minibuf();
        else
                nargs = qc_get_iargs_from_ufunc();

        if (nargs < fn->f_minargs || nargs > fn->f_maxargs)
                qcsyntax(QCE_ARG_EXPECTED);
        ret->a_type = fn->f_ret;
        fn->f_fn.i(ret);

        /* Clear args for next call */
        qc_iarg_tos = 0;
}

/*
 * Call routine for external functions.
 */
static void qc_ufunc_call(Atom *ret, Function *fn)
{
        char *loc, *progsave;
        int lvartemp;
        Namespace *nssave;

        /* Save the namespace because the new function might be
         * from a different loaded program. */
        nssave = qc_namespace;
        qc_namespace = fn->f_namespace;

        loc = fn->f_fn.u;

        /*
         * Temporary stack pointer lvartemp is used, because
         * of the possibility of recursive function calls to this
         * function in the argument evaluation (from qc_push_uargs())
         * or function processing (from qc_interpret_block())
         *
         *   lvartemp is the saved local variable stack pointer
         *   progsave is the saved program counter (IE the link
         * register for returning from a function).
         *   loc is the start of the function (the bransh address)
         */
        lvartemp = qc_lvar_tos;

        /* Check environment by checking if old namespace was NULL.
         * If it was, then the call was from qc_execute -- a call
         * from the command interpreter, not from QC. */
        if (nssave == NULL)
                qc_push_uargs_from_minibuf();
        else
                qc_push_uargs();

        progsave = qc_program_counter;
        qc_ufunc_push(lvartemp);
        qc_program_counter = loc;
        qc_get_uparams();

        /* XXX: User could possibly `break' from this rather than
         * return */
        qc_interpret_block();

        qc_namespace = nssave;
        qc_program_counter = progsave;
        qc_lvar_tos = qc_ufunc_pop();

        /* XXX: Is this the place to assign type? */
        ret->a_value = qc_return_val.a_value;
        ret->a_type  = qc_return_val.a_type;
}


/*
 *                qc_push_uargs() and qc_get_uparams()
 *
 * qc_push_uargs() makes a local_push() call like qc_decl_local(), except
 * that the arguments pushed onto the stack have (or should have) been
 * initialized already.
 *
 * qc_get_uparams() links to those arguments before the call to
 * qc_interpret_block(). Since qc_push_uargs() simply copied them onto
 * the stack, there is no need for the stack pointer to move, until
 * local variables are declared; this also means that qc_get_uparams()
 * method of linking the arguments into the new function is to simply
 * write over the variable names; this is also the reason that stack
 * unwinding is not needed. Instead, the local variable stack is reset
 * each time a function returns.
 */

/* XXX: these functions should check number of arguments. */

/*
 * Push, uargs, push!
 *
 * Finish up pushing arguments onto the user function stack.
 * common to qc_push_uargs_from_minibuf() and qc_push_uargs()
 */
static void qc_push_uargs_push(Variable *top, int count)
{
        while (count > 0) {
                local_push(top);
                --count;
                --top;
        }
}

/*
 * Make sure a variable pushed onto argument stack has all its fields
 * in order, not counting its name, Atom or hash-tabke fields.
 */
static void stackvarinit(Variable *v)
{
        v->v_flag = 0;
        v->v_aidx = 0;
        v->v_asize = 1;
        v->v_type |= QC_ARG;
}

/*
 * Push a user-defined function's arguments onto the local variable
 * stack. These will be retrieved and renamed by the called user function
 * via qc_get_uparams(). The program counter is at the function call
 * (somewhere in the calling function), pointing to the opening
 * parenthesis, of the argument list.
 */
static void qc_push_uargs(void)
{
        Variable params[NUM_PARAMS];
        register int count;
        register Variable *pv;

        qc_lex();
        if (QC_TOK(qc_token) != QC_OPENPAREN)
                qcsyntax(QCE_PAREN_EXPECTED);

        /* proces a comma-separacted list of value */
        count = 0;
        pv = &params[0];
        do {
                /* Note: name of variable is handled by
                 * qc_get_uparams() below */
                qcexpression(&pv->v_datum);
                stackvarinit(pv);
                qc_lex();
                ++count;
                ++pv;
        } while (QC_TOK(qc_token) == QC_COMMA);

        /* now, push on qc_lvar_stack in reverse order */
        --pv;
        qc_push_uargs_push(pv, count);
}

/*
 * Similar to qc_push_args, except this gets its arguments from the
 * command interpreter instead of a user program.
 */
static void qc_push_uargs_from_minibuf(void)
{
        Variable params[NUM_PARAMS];
        int res;
        int count;
        Variable *pv;

        pv = &params[0];
        count = 0;
        while ((res = token_next_atom(&pv->v_datum)) != 0
               && count < NUM_PARAMS) {
                stackvarinit(pv);
                ++pv;
                ++count;
        }

        /* now, push on qc_lvar_stack in reverse order */
        --pv;
        qc_push_uargs_push(pv, count);
}

/*
 * Get a user-defined function's paramters.
 *
 * The program counter is in the function prototype, at the type
 * declaration of the first parameter, or at the closing parenthesis
 * if there are no arguments. (This may change when the `void' key-
 * word gets enabled.
 */
static void qc_get_uparams(void)
{
        Variable *p;
        qctoken_t type;
        int i;

        i = qc_lvar_tos - 1;
        /* Some of the syntax checks are skipped here because they
         * should have been trapped when the function was declared */
        do {
                p = &qc_lvar_stack[i];
                type = qc_get_type();
                if (QC_ISVOID(type)) {
                        while (QC_TOK(qc_token) != QC_CLOSEPAREN)
                                qc_lex();
                        break;
                }
                p->v_type = QC_TYPEOF(qc_token);

                /* Argument was evaluated in qc_push_uargs */
                p->v_flag |= QC_VFLAG_INITIALIZED;
                qc_lex();

                /* link parameter name with argument on local var stack,
                 * by simply renaming the varaibles on the stack. This is
                 * okay because: they are newly copied already by
                 * qc_push_uargs(); and qc_get_uparams() is called before
                 * the block is interpreted, so there is no fear of
                 * unwinding args at the wrong place. */
                strcpy(p->v_name, qc_token_string);
                qc_lex();
                --i;
        } while (QC_TOK(qc_token) == QC_COMMA);

        if (QC_TOK(qc_token) != QC_CLOSEPAREN)
                qcsyntax(QCE_PAREN_EXPECTED);
}

/*
 * Pop a user-defined function's local variables from the
 * local variable stack.
 * Returns index of local variable stack.
 */
static int qc_ufunc_pop(void)
{
        --qc_func_tos;
        if (qc_func_tos < 0)
                qcsyntax(QCE_RET_NOCALL);

        return qc_func_stack[qc_func_tos];
}

/*
 * Push a user-defined function's local variables onto the
 * local variable stack. This needs to be called just once
 * for each function push, because all it really does is save
 * the argument stack index of the calling function by pushing
 * that index onto qc_func_stack (a stack of stack pointers).
 *
 * Param: i Local variable index to push
 */
static void qc_ufunc_push(int i)
{
        if (qc_func_tos > NUM_FUNC)
                qcsyntax(QCE_NEST_FUNC);

        qc_func_stack[qc_func_tos] = i;
        ++qc_func_tos;
}

/* Helper to qc_func_lookup() below */
static Function *qc_func_lookup1(const char *name, Function *t, hash_t hash)
{
        while (t != NULL) {
                if (t->f_hash == hash) {
                        if (!strcmp(t->f_name, name))
                                return t;
                }
                t = t->f_next;
        }
        return t;
}

/**
 * qc_func_lookup - Find a function with a matching name.
 *
 * Return: Pointer to the function's descriptor struct, or NULL
 * if the function was not declared.
 */
Function *qc_func_lookup(const char *name)
{
        hash_t hash;
        int hidx;
        Function *t;

        hash = qc_symbol_hash(name);

        /* Static functions have namespace priority */
        if (qc_namespace != NULL) {
                hidx = hash % NUM_STATIC_FUNC;
                t = &qc_namespace->fn_hashtbl[hidx];
                t = qc_func_lookup1(name, t, hash);
                if (t != NULL)
                        return t;
        }
        hidx = hash % NUM_FUNC;
        t = &qc_function_hashtbl[hidx];
        return qc_func_lookup1(name, t, hash);
}


/**
 * qc_function_init - Initialize everything in qcfunction.c that needs to
 * be initialized.
 *
 * Return: zero if everything initialized, or the negative of an error
 * code if not. This function will return early on the first
 * failure.
 */
int qc_function_init(void)
{
        /* initialize the hash table while we're in here */
        int ret = 0;
        Function *t;
        Variable *v;

        for (t = &qc_function_hashtbl[0];
             t < &qc_function_hashtbl[NUM_FUNC]; ++t) {
                qc_function_hashinit1(t);
        }
        for (v = &qc_gvar_hashtbl[0];
             v < &qc_gvar_hashtbl[NUM_GLOBAL_VARS]; ++v) {
                qc_gvar_hashinit1(v);
        }

        /* Install internal functions while we're at it */
        for (t = qc_ifunc_tbl; *t->f_name != '\0'; ++t) {
                ret = qc_insert_fn(t);
                if (ret)
                        goto done;
        }
done:
        return ret;
}

void qc_function_namespace_init(Namespace *ns)
{
        Function *t;
        Variable *v;
        for (t = &ns->fn_hashtbl[0];
             t < &ns->fn_hashtbl[NUM_STATIC_FUNC]; ++t) {
                qc_function_hashinit1(t);
        }
        for (v = &ns->var_hashtbl[0];
             v < &ns->var_hashtbl[NUM_STATIC_VARS]; ++v) {
                qc_gvar_hashinit1(v);
        }
}

/**
 * qc_function_namespace_exit -  Clean up everything in qcfunction.c that
 * needs to be cleaned up before destroying a Namespace.
 * @ns: Pointer to the Namespace to clean up.
 */
void qc_function_namespace_exit(Namespace *ns)
{
        Function *t, *p, *q;
        Variable *x, *y, *z;

        for (t = &ns->fn_hashtbl[0];
             t < &ns->fn_hashtbl[NUM_STATIC_FUNC]; ++t) {
                p = t->f_next;
                while (p != NULL) {
                        q = p;
                        p = p->f_next;
                        free(q);
                }
                qc_function_hashinit1(t);
        }

        for (x = &ns->var_hashtbl[0];
             x < &ns->var_hashtbl[NUM_STATIC_VARS]; ++x) {
                /* TODO: Same issues as with qc_function exit here */
                y = x->v_next;
                while (y != NULL) {
                        z = y;
                        y = y->v_next;
                        free(z);
                }
                qc_gvar_hashinit1(x);
        }
}


/**
 * qc_function_exit - Clean up everything in qcfunction.c that needs to
 * be cleaned up before exiting vmebr.
 */
void qc_function_exit(void)
{
        Function *t, *p, *q;
        Variable *x, *y, *z;

        for (t = &qc_function_hashtbl[0];
             t < &qc_function_hashtbl[NUM_FUNC]; ++t) {
                p = t->f_next;
                while (p != NULL) {
                        q = p;
                        p = p->f_next;
                        free(q);
                }
                qc_function_hashinit1(t);
        }

        for (x = &qc_gvar_hashtbl[0];
             x < &qc_gvar_hashtbl[NUM_GLOBAL_VARS]; ++x) {
                /* TODO: More than this: every variable needs to be
                 * checked if it has a data array, and cleared if it
                 * does */
                y = x->v_next;
                while (y != NULL) {
                        z = y;
                        y = y->v_next;
                        free(z);
                }
                qc_gvar_hashinit1(x);
        }
}

/**
 * qc_insert_fn - Insert a function into a hash table.
 * @f: Pointer to the function's descriptor struct. If `f' is
 *      static, then the function will be inserted into the current
 *      namespace's hash table; otherwise it will be inserted into the
 *      global hash table.
 *
 * This is called during prescan.
 *
 * Return: Zero if the function was inserted; a negative number if
 * a symbol with the same name has already been installed.
 */
int qc_insert_fn(Function *f)
{
        Function *t, *new;
        hash_t hash = qc_symbol_hash(f->f_name);
        int hidx;

        if (QC_ISSTATIC(f->f_ret)) {
                hidx = hash % NUM_STATIC_FUNC;
                t = &qc_namespace->fn_hashtbl[hidx];
        } else {
                hidx = hash % NUM_FUNC;
                t = &qc_function_hashtbl[hidx];
        }

        if (t->f_hash == -1 || t->f_hash == hash) {
                /* XXX: If t->f_hash == hash, then
                 * there is a duplicate-symbol error */
                memcpy(t, f, sizeof(Function));
                t->f_hash = hash;
                t->f_next = NULL;
                return 0;
        }
        new = malloc(sizeof(Function));
        if (new == NULL)
                return -QCE_NOMEM;
        memcpy(new, f, sizeof(Function));
        new->f_hash = hash;
        qc_fn_collision_insert(new, t);
        return 0;
}

static int qc_gvar_insert(Variable *v)
{
        Variable *t, *new;
        hash_t hash = qc_symbol_hash(v->v_name);
        int hidx = hash;
        if (QC_ISSTATIC(v->v_type)) {
                hidx = hash % NUM_STATIC_VARS;
                t = &qc_namespace->var_hashtbl[hidx];
        } else {
                hidx = hash % NUM_GLOBAL_VARS;
                t = &qc_gvar_hashtbl[hidx];
        }
        if (t->v_hash == -1 || t->v_hash == hash) {
                memcpy(t, v, sizeof(Variable));
                t->v_hash = hash;
                t->v_next = NULL;
                return 0;
        }
        new = malloc(sizeof(Variable));
        if (new == NULL)
                return -QCE_NOMEM;
        memcpy(new, v, sizeof(Variable));
        new->v_hash = hash;
        qc_gvar_collision_insert(new, t);
        return 0;
}

static void qc_gvar_collision_insert(Variable *new, Variable *root)
{
        Variable *old;

        old = root;
        while (root != NULL) {
                old = root;
                root = root->v_next;
        }
        old->v_next = new;
        new->v_next = NULL;
}

static void qc_fn_collision_insert(Function *new, Function *root)
{
        Function *old;

        old = root;
        while (root != NULL) {
                old = root;
                root = root->f_next;
        }
        old->f_next = new;
        new->f_next = NULL;
}

/**
 * iarg_pop - Get next argument from the internal argument stack.
 *
 * Note: This is for the internal `library' functions to use.
 *
 * If a string is expected, the argument should be a pointer. Use the
 * .p field of a_value.
 *
 * Return: Pointer to the argument, or NULL if there are no more
 * parameters. This is a bug: either qc_ifunc_tbl in qcparse.c is wrong,
 * or you are asking for too many arguments.
 *
 * Warning: Retun value is valid only until you exit the function.
 */
Atom *iarg_pop(void)
{
        --qc_iarg_tos;
        if (qc_iarg_tos < 0)
                qcsyntax(QCE_TOO_MANY_ARGS);
        return &qc_iarg_stack[qc_iarg_tos];
}

/**
 * Return from a user-defined function, saving the function's return
 * value in qc_return_val.
 */
void qc_ufunc_ret(void)
{
        /* Local variable is used instead of qc_return_val, because
         * this may get called recursively from qcexpression() */
        Atom a;

        a.a_value.i = 0;

        /* get return value, if any */
        qcexpression(&a);

        /* TODO: Check type against expected type. If void,
         * type is QC_EMPTY. */
        memcpy(&qc_return_val, &a, sizeof(Atom));
}

/* WRONG WRONG WRONG WRONG WRONG */

/**
 * qc_ufunc_declare - Declare a function.
 *
 * Part of prescan; function table will not change afteward.
 * The program counter should be at the start of the function type.
 */
void qc_ufunc_declare(void)
{
        Function new;
        Function *f = &new;
        int args = 0;
        qctoken_t type;
        int ret;

        f->f_call = qc_ufunc_call;
        f->f_namespace = qc_namespace;

        type = qc_get_type();
        if (type == -1)
                qcsyntax(QCE_TYPE_EXPECTED);

        if (QC_TOK(qc_token) == QC_MULTOK) {
                type |= QC_PTR;
                qc_lex();
        }
        f->f_ret = type;

        qc_lex();

        /* TODO: strncpy */
        strcpy(f->f_name, qc_token_string);

        qc_lex();
        if (QC_TOK(qc_token) != QC_OPENPAREN)
                qcsyntax(QCE_PAREN_EXPECTED);
        f->f_fn.u = qc_program_counter;

        ret = qc_insert_fn(f);
        if (ret)
                qcsyntax(-ret);

        /* Count args */
        args = 0;
        do {
                type = qc_get_type();
                if (QC_ISVOID(type)) {
                        /* No args, just skip to closing parenthesis */
                        while (QC_TOK(qc_token) != QC_CLOSEPAREN)
                                qc_lex();
                        break;
                } else if (!QC_ISTYPE(type)) {
                        qcsyntax(QCE_TYPE_EXPECTED);
                }
                ++args;
                qc_lex();
                if (QC_TOK(qc_token) != QC_IDENTIFIER)
                        qcsyntax(QCE_IDENTIFIER_EXPECTED);

                qc_lex();
        } while (QC_TOK(qc_token) == QC_COMMA);

        if (QC_TOK(qc_token) != QC_CLOSEPAREN)
                qcsyntax(QCE_PAREN_EXPECTED);

        f->f_maxargs = args;
        f->f_minargs = args;
}

/**
 * qc_decl_global - declare a global variable. Part of prescan.
 *
 * The program counter is at the start of the variable type.
 */
void qc_decl_global(void)
{
        qctoken_t type;
        Atom *varray;
        Variable var;
        Variable *v = &var;
        int ret;
        int size;
        int i;

        type = qc_get_type();
        if (type == -1)
                qcsyntax(QCE_SYNTAX);
        /* TODO: end of common node for static and non-static
         * variables with file scope */

        do {
                qc_lex();
                if (QC_TOK(qc_token) == QC_MULTOK) {
                        type |= QC_PTR;
                        qc_lex();
                } else {
                        type &= ~QC_PTR;
                }
                if (QC_TOK(qc_token) != QC_IDENTIFIER)
                        qcsyntax(QCE_IDENTIFIER_EXPECTED);

                strcpy(v->v_name, qc_token_string);
                v->v_type    = type;
                v->v_value.i = 0;

                qc_lex();

                if (QC_TOK(qc_token) == QC_OPENSQU) {
                        v->v_flag |= QC_VFLAG_ARRAY;

                        qc_lex();
                        if (QC_TOK(qc_token) != QC_NUMBER)
                                qcsyntax(QCE_ARRAYSIZE_NOT_LIT);
                        size = strtoul(qc_token_string, NULL, 0);
                        if (size > QC_GARRAY_MAX)
                                qcsyntax(QCE_ARRAY_TOO_BIG);
                        qc_lex();
                        if (QC_TOK(qc_token) != QC_CLOSESQU)
                                qcsyntax(QCE_SQUBRACE_EXPECTED);
                        qc_lex();
                        varray = malloc((size) * sizeof(Atom));
                        if (varray == NULL)
                                qcsyntax(QCE_NOMEM);
                        for (i = 0; i < size; ++i)
                                varray[i].a_type = type;
                        v->v_array = varray;
                } else {
                        v->v_array = NULL;
                        size = 1;
                }

                v->v_asize = size;
                v->v_aidx  = 0;

                ret = qc_gvar_insert(v);
                if (ret)
                        qcsyntax(ret);

                /* Maybe initialization. If other vars are used,
                 * they must be declared already. */
                if (QC_TOK(qc_token) == QC_EQEQ) {
                        Atom a;
                        if (QC_ISARRAY(v))
                                qcsyntax(QCE_ARRAY_INITIALIZER);

                        qcexpression(&a);
                        assign_var(v->v_name, &a);
                        qc_lex();
                }

        } while (QC_TOK(qc_token) == QC_COMMA);

        if (QC_TOK(qc_token) != QC_SEMI)
                qcsyntax(QCE_SEMI_EXPECTED);
}

/*
 * Get a type.
 *
 * The program counter is before the start of the `type'.
 *
 * note: This does not check if pointer
 *
 * return: type, in qctoken_t format, or -1 cast to qctoken_t
 * width if not a valid type declaration.
 */
static qctoken_t qc_get_type(void)
{
        int uns = 0;
        int st  = 0;
        qctoken_t ret;

        qc_lex();
        if (QC_ISSTATIC(qc_token)) {
                st = QC_STATIC;
                qc_lex();
        }

        if (!QC_ISSIGNED(qc_token)) {
                uns = QC_UNSIGNED;
                qc_lex();
        }

        if (!QC_ISTYPE(qc_token)) {
                if (uns)
                        ret = QC_UINT | st | QC_TYPE;
                else
                        ret = -1;
        } else {
                ret = QC_TYPEOF(qc_token) | uns | st | QC_TYPE;
        }
        return ret;
}

/**
 * qc_decl_local - Declare a local variable.
 *
 * Unlike qc_decl_global, this occurs during runtime, not during prescan.
 * The program counter is at the start of the variable's type.
 */
void qc_decl_local(void)
{
        Variable v;
        qctoken_t type;
        int size, idx;

        type = qc_get_type();
        if (type == -1)
                qcsyntax(QCE_TYPE_EXPECTED);
        v.v_type = type;
        v.v_array = NULL; /* always, for local vars */

        /* No wasting time initializing the variable; require
         * the user to do that, like in ordinary C */

        do {
                /* process comma-separated list */
                v.v_flag = 0;
                qc_lex();
                if (QC_TOK(qc_token) == QC_MULTOK) {
                        v.v_type |= QC_PTR;
                        qc_lex();
                } else {
                        v.v_type &= ~QC_PTR;
                }

                if (QC_TOK(qc_token) != QC_IDENTIFIER)
                        qcsyntax(QCE_IDENTIFIER_EXPECTED);

                strcpy(v.v_name, qc_token_string);
                qc_lex();

                if (QC_TOK(qc_token) == QC_OPENSQU) {
                        v.v_flag |= QC_VFLAG_ARRAY;

                        qc_lex();
                        if (QC_TOK(qc_token) != QC_NUMBER)
                                qcsyntax(QCE_ARRAYSIZE_NOT_LIT);

                        size = strtoul(qc_token_string, NULL, 0);
                        if (size > QC_LARRAY_MAX)
                                qcsyntax(QCE_ARRAY_TOO_BIG);

                        qc_lex();
                        if (QC_TOK(qc_token) != QC_CLOSESQU)
                                qcsyntax(QCE_SQUBRACE_EXPECTED);
                        qc_lex();
                } else {
                        size = 1;
                }

                v.v_asize = size;
                idx = 0;
                while (size > 0) {
                        v.v_aidx = idx;
                        local_push(&v);
                        --size;
                        ++idx;
                }

                /* Maybe initialization. If other vars are used,
                 * they must be declared already. */
                if (QC_TOK(qc_token) == QC_EQEQ) {
                        Atom a;
                        if (QC_ISARRAY(&v))
                                qcsyntax(QCE_ARRAY_INITIALIZER);
                        qcexpression(&a);
                        assign_var(v.v_name, &a);
                        qc_lex();
                }

        } while (QC_TOK(qc_token) == QC_COMMA);

        if (QC_TOK(qc_token) != QC_SEMI)
                qcsyntax(QCE_SEMI_EXPECTED);
}


/*
 * Push a user-defined function's local variables onto the
 * local variable stack. This is called once for each variable, and
 * it only applies to arguments. There is no corresponding pop
 * operation, because the stack is simply reset when returning from
 * a function. See comments to qc_ufunc_pop and qc_ufunc_push.
 */
static void local_push(Variable *v)
{
        if (qc_lvar_tos > NUM_LOCAL_VARS)
                qcsyntax(QCE_TOO_MANY_LVARS);
        /* TODO: Args should be checked for initialization */
        memcpy(&qc_lvar_stack[qc_lvar_tos], v, sizeof(Variable));

        qc_lvar_tos++;
}

/*
 * Helpers for qc_uvar_lookup below
 */
static Variable *qc_local_uvar_lookup(const char *s)
{
        register Variable *p, *top, *bottom;

        top = qc_lvar_stack_top();
        bottom = qc_lvar_stack_bottom();

        /* sequential search. User shouldn't have so many
         * local variables anyway. */
        for (p = top; p >= bottom; --p) {
                if (!strcmp(p->v_name, s)) {
                        while (p->v_aidx != 0)
                                --p;
                        return p;
                }
        }
        return NULL;
}

static Variable *qc_global_uvar_lookup1(const char *s, Variable *v, hash_t hash)
{
        while (v != NULL) {
                if (v->v_hash == hash) {
                        if (!strcmp(v->v_name, s))
                                return v;
                }
                v = v->v_next;
        }
        return v;
}

static Variable *qc_global_uvar_lookup(const char *s)
{
        hash_t hash;
        int hidx;
        Variable *v;

        hash = qc_symbol_hash(s);

        /* First check the static variables, since we know
         * without any ambiguity that they are file-scope. */
        hidx = hash % NUM_STATIC_VARS;
        v = &qc_namespace->var_hashtbl[hidx];
        v = qc_global_uvar_lookup1(s, v, hash);
        if (v != NULL)
                return v;

        hidx = hash % NUM_GLOBAL_VARS;
        v = &qc_gvar_hashtbl[hidx];
        v = qc_global_uvar_lookup1(s, v, hash);
        return v;
}

/**
 * qc_uvar_lookup - Find a previously-declared variable.
 * @s: Variable name
 *
 * Note: In the case of duplicate names, local variables have precedence
 *      over global variables (but if there are duplicate variable names,
 *      you are a bad programmer and a bad person).
 * Return: Pointer to the variable's Variable struct, or to NULL
 * if the variable is not found.
 */
Variable *qc_uvar_lookup(const char *s)
{
        register Variable *p;

        p = qc_local_uvar_lookup(s);
        if (p == NULL)
                p = qc_global_uvar_lookup(s);

        return p;
}

/**
 * assign_var - Assign a value to a variable.
 * @s: Name of variable. Later this will be a hash code.
 * @v: Value to set the variable.
 *
 * Note: The right-hand side of the expression will be cast to the
 * same type as `v', if the type is valid.
 */
void assign_var(const char *s, Atom *v)
{
        register Variable *p;

        p = qc_uvar_lookup(s);
        if (p == NULL)
                qcsyntax(QCE_NOT_VAR);

        qc_mov(&p->v_datum, v);
        p->v_flag |= QC_VFLAG_INITIALIZED;
}

/**
 * assign_var_deref - Assign a variable if you know the pointer already.
 * @p:  Pointer to the variable to assign. Although it seems like a
 *      friendlier interface to pass the pointer variable instead of the
 *      target variable, this is not always possible, because the pointer
 *      may be an evaluated value, rather than a direct variable. For
 *      example we could get `*(p+1)=x' rather than `p=p+1;*p=x'.
 *
 * This is for assigning values to variables whose addresses have been
 * evaluated by the parser.
 *
 * For so many reasons, this is only possible for pointers to
 * user-defined variables. Internal variables (FILE pointers, etc.,
 * must be accessed via internal function calls
 */
void assign_var_deref(Variable *p, Atom *v)
{
        if (qc_uvar_bound_check(p))
                qcsyntax(QCE_BOUND_ERR);

        qc_mov(&p->v_datum, v);
        p->v_flag |= QC_VFLAG_INITIALIZED;
}


/*
 * Redirection of the qc_va_arg() macro
 */
int qc_va_iarg_int(qc_va_list args)
{
        Atom *a = iarg_pop();
        return a->a_value.i;
}

long qc_va_iarg_long(qc_va_list args)
{
        Atom *a = iarg_pop();
        return a->a_value.li;
}

long long qc_va_iarg_longlong(qc_va_list args)
{
        Atom *a = iarg_pop();
        return a->a_value.lli;
}

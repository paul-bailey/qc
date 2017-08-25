/*
 * The QC-parser.
 *
 * This uses a recursive descent method, rather than a state-stack
 * approach. qcexpression() is analogous to yacc's yyparse(); ditto
 * for qc_lex() and yylex().
 */
#include "qc.h"
#include "qc_private.h"
#include <setjmp.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Table of built-in C keywords currently supported by
 * this interpreter.
 */
#define IKEY_PARAMS(ky, tk) { .k_cmd = ky, .k_tok = (tk) }
#define IKEY_END              IKEY_PARAMS(NULL, 0)
static struct qc_ikeyword_t {
        const char *k_cmd;
        const qctoken_t k_tok;
        hash_t k_hash;
} qc_ikeyword_tbl[] = {
        IKEY_PARAMS("if",          QC_IF   ),
        IKEY_PARAMS("else",        QC_ELSE ),
        IKEY_PARAMS("for",         QC_FOR  ),
        IKEY_PARAMS("do",          QC_DO   ),
        IKEY_PARAMS("while",       QC_WHILE),
        IKEY_PARAMS("char",        QC_CHAR | QC_TYPE),
        IKEY_PARAMS("V120_HANDLE", QC_FILE | QC_TYPE),
        IKEY_PARAMS("Vme",         QC_FILE | QC_TYPE),
        IKEY_PARAMS("int",         QC_INT  | QC_TYPE),
        IKEY_PARAMS("FILE",        QC_FILE | QC_TYPE),
        IKEY_PARAMS("return",      QC_RETURN),
        IKEY_PARAMS("float",       QC_FLT | QC_TYPE | QC_FLTFLG),
        IKEY_PARAMS("double",      QC_DBL | QC_TYPE | QC_FLTFLG),
        IKEY_PARAMS("unsigned",    QC_UNSIGNED | QC_TYPE),
        IKEY_PARAMS("static",      QC_STATIC | QC_TYPE),
        IKEY_PARAMS("void",        QC_EMPTY | QC_TYPE | QC_VDFLG),
        IKEY_PARAMS("NULL",        QC_NULL),
        IKEY_PARAMS("break",       QC_BREAK),
        IKEY_END,
};

static void evalexp0(Atom *a);
static void evalexp1(Atom *a);
static void evalexp2(Atom *a);
static void evalexp3(Atom *a);
static void evalexp4(Atom *a);
static void evalexp5(Atom *a);
static void evalexp6(Atom *a);
static void evalexp7(Atom *a);
static void evalexp8(Atom *a);
static void qc_atom(Atom *a);
static void qc_slide(void);
static qctoken_t qc_ikeyword_lookup(char *s);

char *qc_program_counter_save = NULL;


/**
 * array_offset_maybe - Dereference an array, if there is a `[' following
 * a variable name
 * @v: Variable to check if array offset
 *
 * Return: v, or v[`offset'] if the user code required an offset.
 */
static Variable *array_offset_maybe(Variable *v)
{
        if (QC_TOK(qc_token) == QC_OPENSQU) {
                Atom arrayidx;
                if (!QC_ISARRAY(v))
                        qcsyntax(QCE_TYPE_INVAL);

                qc_lex();
                evalexp0(&arrayidx);
                if (QC_TOK(qc_token) != QC_CLOSESQU)
                        qcsyntax(QCE_SQUBRACE_EXPECTED);
                qc_lex();

                if (arrayidx.a_value.i >= v->v_asize)
                        qcsyntax(QCE_ARRAY_BOUNDS);
                /* Closing brace should have been
                 * passed when evaluating arrayidx
                 */
                /*
                 * FIXME: This only works for local variables.
                 * It should check v->v_array for global variables.
                 */
                v += arrayidx.a_value.i;
        }
        return v;
}


/* **********************************************************************
 *            Section: Helpers to evalexp0, the assignment part
 ***********************************************************************/

/*
 * Helper to evalexp0 below. This processes an assignment operator `asgn'
 * on the lval `dst' and rval `operand'.
 *
 * EG for `a &= b', `dst' is the Atom holding `a's data, `operand' is
 * the Atom holding `b's data, and `asgn' is QC_ANDEQ.
 *
 * The result is stored in `dst'.
 */
static void qcparse_assign(Atom *dst, Atom *operand, int asgn)
{
        /* TODO: Table this, prevent possible if-else-if block */
        switch (asgn) {
        case QC_ANDEQ:
                qc_and(dst, operand);
                break;
        case QC_OREQ:
                qc_or(dst, operand);
                break;
        case QC_PLUSEQ:
                qc_add(dst, operand);
                break;
        case QC_MINUSEQ:
                qc_sub(dst, operand);
                break;
        case QC_XOREQ:
                qc_xor(dst, operand);
                break;
        case QC_DIVEQ:
                qc_div(dst, operand);
                break;
        case QC_MULEQ:
                qc_mul(dst, operand);
                break;
        case QC_MODEQ:
                qc_mod(dst, operand);
                break;
        case QC_LSLEQ:
                qc_asl(dst, operand);
                break;
        case QC_LSREQ:
                qc_asr(dst, operand);
                break;
        case QC_EQEQ:
                qc_mov(dst, operand);
                break;
        }
}

/**
 * qcparse_assign_maybe - helper to evalexp0
 * @a: the same Atom that is passed down through the recursive
 * decsent parser. If the return value is true, `a' will hold the final
 * result (eg for `x += y;' `a' will hold the value of x + y).
 *  @var: variable to assign the result.
 *
 * Assign a variable, code that is common to both pointer de-referencing
 * and to name-identified variable dereferencing. This also processes
 * possible post incrementers.
 *
 * Side effects:
 * The program state should have been saved in a struct qc_program_t
 * before calling this function, and restored if it returns FALSE. If
 * the assignment is not a simple `=' (EG it is something like `+=') and
 * `var' has not been initialized, `var' will be assigned an undefined
 * value and flagged as `initialized', without warning.
 *
 * Return: true (1) if an assignment was made; false (0) if not.
 */
static int qcparse_assign_maybe(Atom *a, Variable *var)
{
        Atom ta;
        int assignment;
        /*
         * If an array, de-reference it. The downside of this is that
         * we have to undo and redo all of this if it is not an
         * assignment.
         *
         * XXX: DRY alert.
         */
        var = array_offset_maybe(var);

        if (QC_ISASGN_OP(qc_token)) {
                memcpy(a, &var->v_datum, sizeof(Atom));
                switch (QC_TOK(qc_token)) {
                case QC_PLUSPLUS:
                        ta.a_value.lli = 1;
                        ta.a_type      = QC_INT;
                        assignment     = QC_PLUSEQ;
                        qc_lex();
                        break;
                case QC_MINUSMINUS:
                        ta.a_value.lli = 1;
                        ta.a_type      = QC_INT;
                        assignment     = QC_MINUSEQ;
                        qc_lex();
                        break;
                default:
                        assignment = QC_TOK(qc_token);
                        qc_lex();
                        evalexp0(&ta);
                        break;
                }

                /*
                 * Two-step process: Perform the necessary MOV operation
                 * to Atom `a', getting the type right, etc., and then
                 * assign it to the actual variable `var'.
                 */
                qcparse_assign(a, &ta, assignment);
                assign_var_deref(var, a);
                return 1;
        }

        return 0;
}

/*
 * Evaluate a pointer expression (minus the opening asterisk, which we
 * already know about), and return the Variable being pointed at. This
 * has undefined behavior if the pointer is not to a user-defined
 * variable.
 */
static Variable *ptr2var(void)
{
        Atom varptr;

        qc_lex();

        /*
         * DIRTY ALERT!! Skip down to evalexp8 (as atomic as, or more
         * atomic than, a parenthetic expression), because 1) if we have
         * an array of pointers and no parentheses are used, we wish to
         * evaluate `(*x[i]) + b', not `*(x[i]+b)', and 2) we still want
         * parentheses to be permissible, and 3) the above will hold for
         * parenthetic evaluation as well as atomic evaluations.
         */
        evalexp8(&varptr);

        if (!QC_ISPTR(varptr.a_type))
                qcsyntax(QCE_SYNTAX);

        return (Variable *)(varptr.a_value.p);
}

/*
 * Pre-increment or -decrement a variable.
 */
static void plusplusvar(Atom *a, Variable *var, int plusminus)
{
        Atom ta;

        memcpy(a, &var->v_datum, sizeof(Atom));
        ta.a_value.lli = 1;
        ta.a_type      = QC_INT;
        qc_lex();
        qcparse_assign(a, &ta, plusminus);
        assign_var_deref(var, a);
}

/*
 * Preincrament or decrement a variable.
 * `a' is the Atom holding the return value for qcexpression,
 * and `plusminus' is either QC_PLUSEQ or QC_MINUSEQ, depending
 * whether or not the operation is increment or decrement.
 */
static void preincrement(Atom *a, int plusminus)
{
        Variable *var;

        qc_lex();
        if (QC_TOK(qc_token) == QC_MULTOK) {
                var = ptr2var();
                plusplusvar(a, var, plusminus);
        } else if (QC_TOK(qc_token) == QC_IDENTIFIER) {
                var = qc_uvar_lookup(qc_token_string);
                if (var == NULL)
                        qcsyntax(QCE_SYNTAX);
                plusplusvar(a, var, plusminus);
        } else {
                qcsyntax(QCE_SYNTAX);
        }
}

/*
 * Process an assignment expression. right to left. If there is an
 * assignment, the L-value `a' will contain the value assigned, so that
 * `a' may continue to be evaluated if we were recursively called from
 * inside a parenthesized expression.
 */
static void evalexp0(Atom *a)
{
        struct qc_program_t buf;
        Variable *var;
        qctoken_t type;

        type = QC_TOK(qc_token);

        /*
         * Pointers have their address evaluated; variable names do not.
         * For all other tokens, assignments are invalid, and we do not
         * look for them here.
         */
        if (type == QC_IDENTIFIER) {
                var = qc_uvar_lookup(qc_token_string);
                if (var != NULL) {
                        /* token is a variable name */
                        a->a_type = var->v_type;

                        qc_program_save(&buf);
                        qc_lex();

                        if (qcparse_assign_maybe(a, var))
                                return;
                        qc_program_restore(&buf);
                }
        } else if (type == QC_MULTOK) {
                qc_program_save(&buf);
                var = ptr2var();
                if (qcparse_assign_maybe(a, var))
                        return;
                qc_program_restore(&buf);
        } else if (type == QC_PLUSPLUS) {
                preincrement(a, QC_PLUSEQ);
                return;
        } else if (type == QC_MINUSMINUS) {
                preincrement(a, QC_MINUSEQ);
                return;
        }

        evalexp1(a);
}

/* TODO: Process whether an array is being de-referenced or not */

/* Process logical AND, OR. left to right */
static void evalexp1(Atom *a)
{
        Atom partial;
        char c;

        evalexp2(a);

        while (QC_ISLOG_OP(c = QC_TOK(qc_token))) {
                qc_lex();
                evalexp2(&partial);

                if (!QC_ISINT(a->a_type) || !QC_ISINT(partial.a_type))
                        qcsyntax(QCE_TYPE_INVAL);

                if (c == QC_LAND) {
                        a->a_value.i = (a->a_value.lli != 0)
                                    && (partial.a_value.lli != 0);
                        a->a_type = QC_INT;
                } else {
                        /* c == QC_LOR */
                        a->a_value.i = (a->a_value.lli != 0)
                                    || (partial.a_value.lli != 0);
                        a->a_type = QC_INT;
                }
                /* TODO: this should break on first FALSE, then
                 * find the end of the expression */
        }
}

/* Process binary operators. left to right. */
static void evalexp2(Atom *a)
{
        Atom partial;
        char c;

        evalexp3(a);

        while (QCTOK_ISBINARY(c = QC_TOK(qc_token))) {
                qc_lex();
                evalexp3(&partial);

                switch (c) {
                case QC_ANDTOK:
                        qc_and(a, &partial);
                        break;
                case QC_ORTOK:
                        qc_or(a, &partial);
                        break;
                case QC_XORTOK:
                        qc_xor(a, &partial);
                        break;
                }
        }
}

/*
 * Process relational operators.
 * The expression `a' will have its data type changed to `int'.
 */
static void evalexp3(Atom *a)
{
        Atom partial;
        register char op;
        int result;

        /* Here, `a' is the xepression on the left side of the
         * relational operator and `partial' is the expression on the
         * right side. */
        evalexp4(a);

        while (QC_ISCMP_OP(qc_token)) {
                op = QC_TOK(qc_token);

                qc_lex();
                evalexp4(&partial);

                result = qc_cmp(a, &partial, op);

                a->a_value.i = result;
                /* This recasts expression to `int', now. We should only
                 * be here if the order of operations allow the type
                 * change. */
                a->a_type = QC_INT;
                qc_int_crop(a);
        }
}

/*
 * Process shift operations. left to right.
 */
static void evalexp4(Atom *a)
{
        Atom partial;
        register char c;

        evalexp5(a);

        while (QC_ISSHIFT_OP(c = QC_TOK(qc_token))) {
                qc_lex();
                evalexp5(&partial);

                if (c == QC_LSL)
                        qc_asl(a, &partial);
                else
                        qc_asr(a, &partial);
        }
}

/*
 * Add or subtract two terms. left to right.
 */
static void evalexp5(Atom *a)
{
        register char op;
        Atom partial;

        evalexp6(a);

        while ((op = QC_TOK(qc_token)) == QC_PLUSTOK || op == QC_MINUSTOK) {
                qc_lex();
                evalexp6(&partial);

                if (op == QC_PLUSTOK)
                        qc_add(a, &partial);
                else /* op == QC_MINUSTOK */
                        qc_sub(a, &partial);
        }
}

/* Process multiply, divide, modulo operators. left to right. */
static void evalexp6(Atom *a)
{
        register char op;
        Atom partial;

        evalexp7(a);

        while (QCTOK_ISMULDIVMOD(op = QC_TOK(qc_token))) {
                qc_lex();
                evalexp7(&partial);

                switch (op) {
                case QC_MULTOK:
                        qc_mul(a, &partial);
                        break;
                case QC_DIVTOK:
                        qc_div(a, &partial);
                        break;
                case QC_MODTOK:
                        qc_mod(a, &partial);
                        break;
                }
        }
}

/* Process unary operators: right to left */
static void evalexp7(Atom *a)
{
        register char op;
        Atom tmp;
        Variable *p;

        op = QC_TOK(qc_token);

        if (QCTOK_ISUNARY(op)) {
                qc_lex();
                switch (op) {
                case QC_MINUSTOK:
                        evalexp8(a);
                        tmp.a_type = a->a_type;
                        if (QC_ISFLT(a->a_type))
                                tmp.a_value.d = -1.0;
                        else
                                tmp.a_value.lli = -1LL;
                        /* Multiply by -1 */
                        qc_mul(a, &tmp);
                        break;
                case QC_MULTOK:
                        /*
                         * Recursively calling evalexp7() rather than
                         * descending down to evalexp8() permits
                         * dereferencing of pointers.
                         */
                        evalexp7(a);

                        /* a <= dereference(a) */
                        p = (Variable *)a->a_value.p;

                        if (!QC_ISPTR(a->a_type))
                                qcsyntax(QCE_DEREF);

                        if (qc_uvar_bound_check(p))
                                qcsyntax(QCE_BOUND_ERR);

                        if (!QC_ISINIT(p))
                                qcsyntax(QCE_UNINIT);

                        /* This casts a copy of p into a-type (minus the
                         * pointer) before copying it into a */
                        a->a_type = p->v_type;
                        qc_mov(a, &p->v_datum);
                        break;
                case QC_LNOTTOK:
                        evalexp8(a);
                        qc_lnot(a);
                        break;
                case QC_ANOTTOK:
                        evalexp8(a);
                        qc_anot(a);
                        break;
                case QC_ANDTOK:
                        /* Do not evaluate next token (which must be an
                         * identifier) because we do not care about its
                         * value, or if it was initialized. */
                        if (QC_TOK(qc_token) != QC_IDENTIFIER)
                                qcsyntax(QCE_IDENTIFIER_EXPECTED);
                        p = qc_uvar_lookup(qc_token_string);
                        if (p == NULL)
                                qcsyntax(QCE_SYNTAX);
                        qc_lex();
                        p = array_offset_maybe(p);

                        a->a_type = p->v_type | QC_PTR;
                        a->a_value.p = p;
                        break;
                default:
                        /* Unary `+', other ignores */
                        break;
                }
        } else {
                evalexp8(a);
        }

}

/* Process parenthesized expression */
static void evalexp8(Atom *a)
{
        int c = QC_TOK(qc_token);
        switch (c) {
        case QC_OPENPAREN:
                c = QC_CLOSEPAREN;
                break;
        case QC_OPENSQU:
                c = QC_CLOSESQU;
                break;
        default:
                /* Not a parenthsized expression, continue parsing */
                qc_atom(a);
                return;
        }

        /* evalexp0 instead of qc_atom, because an assignment may
         * nest itself snugly inside the parenthetic expression. */
        qc_lex();
        evalexp0(a);
        if (QC_TOK(qc_token) != c) {
                /* Redo checks; we don't care about speed here. */
                switch (c) {
                case QC_CLOSEPAREN:
                        qcsyntax(QCE_PAREN_EXPECTED);
                        break;
                case QC_CLOSESQU:
                        qcsyntax(QCE_SQUBRACE_EXPECTED);
                        break;
                default:
                        qcsyntax(QCE_SYNTAX);
                        break;
                }
        }
        qc_lex();
}

/* Find value of number, variable, or function */
static void qc_atom(Atom *a)
{
        Function *f;
        Variable *v;
        char *endptr = NULL;

        switch (QC_TOK(qc_token)) {
        case QC_IDENTIFIER:
                f = qc_func_lookup(qc_token_string);
                if (f != NULL) {
                        /* a will be type-changed into
                         * f's type. */
                        qc_func_call(a, f);
                        qc_lex();
                } else {
                        /* Atom is the value of a variable or its
                         * array de-reference. */
                        v = qc_uvar_lookup(qc_token_string);
                        if (v == NULL)
                                qcsyntax(QCE_SYNTAX);
                        qc_lex();
                        v = array_offset_maybe(v);
                        if (!QC_ISINIT(v)) {
                                /* Trying to get the value of an
                                 * uninitialized variable */
                                qcsyntax(QCE_UNINIT);
                        }
                        /* get a's value */
                        a->a_type = v->v_type;
                        qc_mov(a, &v->v_datum);
                }
                return;

        case QC_NULL:
                a->a_value.p = NULL;
                a->a_type    = QC_PTR | QC_CHAR;
                return;

        case QC_NUMBER:
                a->a_value.lli = strtoll(qc_token_string, &endptr, 0);
                /* XXX: This should be taken care of during qc_lex, or better,
                 * during prescan */
                if (*endptr == '.' || toupper(*endptr) == 'F') {
                        a->a_value.d = strtod(qc_token_string, NULL);
                        a->a_type = QC_DBL | QC_FLTFLG;
                } else if (toupper(*endptr) == 'U') {
                        a->a_type = QC_INT | QC_UNSIGNED;
                } else {
                        /* Default `int' */
                        a->a_type = QC_INT;
                }
                qc_lex();
                return;

        case QC_STRING:
                a->a_type = qc_token;
                a->a_value.p = qc_token_string;
                /* XXX: No new qc_lex()? */
                return;

        default:
                if (QC_TOK(qc_token) == QC_CLOSEPAREN
                 || QC_TOK(qc_token) == QC_CLOSESQU) {
                        return;
                } else {
                        qcsyntax(QCE_SYNTAX);
                }
        }
}

/* TODO: use global when ready */
static void qc_slide(void)
{
        while (isspace(*qc_program_counter) && *qc_program_counter != '\0')
                ++qc_program_counter;
}

/**
 * qc_ikeyword_lookup - Search the table of internal keywords.
 * @s: the token string.
 *
 * Return value: The keyword token (enum QC_TOKENS) and pertinent flags
 * (QC_PTR et al.) defining the token, encoded as an qctoken_t, or
 * or zero if `s' was not found in the keyword table.
 */
static qctoken_t qc_ikeyword_lookup(char *s)
{
        register struct qc_ikeyword_t *t;
        hash_t h = qc_symbol_hash(s);

        /* see if token is in table */
        for (t = &qc_ikeyword_tbl[0]; t->k_cmd != NULL; ++t) {
                if (h == t->k_hash) {
                        if (!strcmp(t->k_cmd, s))
                                return t->k_tok;
                }
        }
        return 0;
}

/**
 * qcexpression - Evaluate the expression at the program counter.
 * @a: Atom to store the expression's L-value.
 */
void qcexpression(Atom *a)
{
        qc_lex();

        if (QC_TOK(qc_token) == QC_SEMI) {
                a->a_value.li = 0;
                a->a_type = QC_EMPTY;
        } else {
                evalexp0(a);
                qcputback(); /* return last token read to input stream */
        }
}

/**
 * qcputback - Return a token to input stream
 */
void qcputback(void)
{
        /* TODO: Determine if this breaks in multi-program case */
        qc_program_counter = qc_program_counter_save;
}

/**
 * qc_init_parser - Do everything for the parser that needs to be done at
 * program boot time.
 */
void qc_init_parser(void)
{
        struct qc_ikeyword_t *k;
        for (k = qc_ikeyword_tbl; k->k_cmd != NULL; ++k)
                k->k_hash = qc_symbol_hash(k->k_cmd);
}

/**
 * qc_lex - Get a token
 *
 * Note: Comments are not checked here, because they should have been
 * filtered out when the file was loaded. String literals are not fully
 * evaluated here, because the program load should have already done
 * this.
 */
qctoken_t qc_lex(void)
{
        register char *s;
        int c;
        qc_token = 0;

        s = qc_token_string_buffer;
        qc_token_string = &qc_token_string_buffer[0];

        /* In case of any doubt ahead, make sure we have a string
         * terminator in qc_token_buffer
         */
        *s = '\0';

        qc_slide();

        qc_program_counter_save = qc_program_counter;

        if (*qc_program_counter == '\0') {
                qc_token = QC_FINISHED;
                goto done;
        }

        /*
         * Delimiters: EOL and space already taken care of.
         * The rest we process
         */
        if (QCCHAR_ISDELIM(*qc_program_counter)) {
                c = *qc_program_counter++;

                /* XXX: State table would be quicker */
                switch (c) {
                case '=':
                        if (*qc_program_counter == '=') {
                                /* Comparison */
                                ++qc_program_counter;
                                qc_token = QC_EQ;
                                goto done;
                        }
                        goto single;
                case '!':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_NE;
                                goto done;
                        }
                        goto single;
                case '<':
                        if (*qc_program_counter == '<') {
                                ++qc_program_counter;
                                if (*qc_program_counter == '=') {
                                        ++qc_program_counter;
                                        qc_token = QC_LSLEQ;
                                } else {
                                        qc_token = QC_LSL;
                                }
                                goto done;
                        } else if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_LE;
                                goto done;
                        }
                        goto single;
                case '>':
                        if (*qc_program_counter == '>') {
                                ++qc_program_counter;
                                if (*qc_program_counter == '=') {
                                        ++qc_program_counter;
                                        qc_token = QC_LSREQ;
                                } else {
                                        qc_token = QC_LSR;
                                }
                                goto done;
                        } else if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_GE;
                                goto done;
                        }
                        goto single;
                case '&':
                        if (*qc_program_counter == '&') {
                                ++qc_program_counter;
                                qc_token = QC_LAND;
                                goto done;
                        } else if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                *s++ = QC_ANDEQ;
                                goto done;
                        }
                        goto single;
                case '|':
                        if (*qc_program_counter == '|') {
                                ++qc_program_counter;
                                qc_token = QC_LOR;
                                goto done;
                        } else if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_OREQ;
                                goto done;
                        }
                        goto single;
                case '+':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_PLUSEQ;
                                goto done;
                        } else if (*qc_program_counter == '+') {
                                ++qc_program_counter;
                                qc_token = QC_PLUSPLUS;
                                goto done;
                        }
                        goto single;
                case '-':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_MINUSEQ;
                                goto done;
                        } else if (*qc_program_counter == '-') {
                                ++qc_program_counter;
                                qc_token = QC_MINUSMINUS;
                                goto done;
                        }
                        goto single;
                case '*':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_MULEQ;
                                goto done;
                        }
                        goto single;
                case '/':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_DIVEQ;
                                goto done;
                        }
                        goto single;
                case '%':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_MODEQ;
                                goto done;
                        }
                        goto single;
                case '^':
                        if (*qc_program_counter == '=') {
                                ++qc_program_counter;
                                qc_token = QC_XOREQ;
                                goto done;
                        }
                        goto single;
                default:
                        goto single;
                }

        single:
                qc_token = qc_ch2tok[c & 0x7FU];
                if (qc_token == 0)
                        qcsyntax(QCE_SYNTAX);
                goto done;
        }

        if (*qc_program_counter == '"') {
                int stringi;

                /* quoted string */
                ++qc_program_counter;
                *s = '\0';

                /*
                 * This ought to be faster for all but the
                 * very short, unescaped, strings.
                 */
                stringi = qc_find_ustring(qc_program_counter);

                /*
                 * Saved string index in token (we have room for
                 * this, since we are not copying the string), so
                 * when we need it, we have the direct index.
                 *
                 * Saved *after* a nul char, in case we
                 * accidentally try to read it somewhere.
                 */
                qc_token_string = qc_namespace->ustrings[stringi].s;
                qc_program_counter = qc_namespace->ustrings[stringi].e;
                qc_token = QC_PTR | QC_STRING;
                goto done;
        }

        if (isdigit(*qc_program_counter)) {
                /* XXX: If we support structs, need way of sliding over
                 * decimal `.' character.
                 * Need way of sliding over `-' in case of exponent */
                while (!QCCHAR_ISDELIM(*qc_program_counter))
                        *s++ = *qc_program_counter++;
                *s = '\0';
                qc_token = QC_NUMBER;
                goto done;
        }

        if (isalpha(*qc_program_counter) || *qc_program_counter == '_') {
                /* var or command */
                while (!QCCHAR_ISDELIM(*qc_program_counter))
                        *s++ = *qc_program_counter++;
                *s = '\0';

                /* No `OR', clear out QC_TEMP */
                qc_token = qc_ikeyword_lookup(qc_token_string);

                if (qc_token == 0)
                        qc_token = QC_IDENTIFIER;
                goto done;
        }

        /* Invalid token */
        qcsyntax(QCE_SYNTAX);
done:
        return qc_token;
}

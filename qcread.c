#include "qc.h"
#include "qc_private.h"
#include <stdio.h>
#include <setjmp.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_EOFL(c_, s_, ret_, cnt_, exitsym_)  \
do {                                              \
        if ((c_) == EOF) {                        \
                (ret_) = -QCE_UNBAL_COMMENT;      \
                goto exitsym_;                    \
        } else if ((c_) == '\n') {                \
                *(s_) = (c_);                     \
                ++(s_);                           \
                ++(cnt_);                         \
        }                                         \
} while (0)

#define QCREAD_CHECK_LEN(cnt_, limit_)          \
do {                                            \
        if ((cnt_) == (limit_))                 \
                return -QCE_OVERSIZE_STRING;    \
} while (0)

#define QCREAD_CHECK_EOF(c_, fp_)                     \
do {                                                  \
        if ((c_) == EOF || feof(fp_) || (c_) == '\0') \
                return -QCE_QUOTE_EXPECTED;           \
} while (0)

#define QCREAD_GETC(c_, fp_, s_, cnt_, lmt_)  \
({                                            \
        (c_) = getc(fp_);                     \
        *(s_) = (c_);                         \
        ++(s_);                               \
        ++(cnt_);                             \
        QCREAD_CHECK_LEN(cnt_, lmt_);         \
        (c_);                                 \
})


char *qc_program_counter;
jmp_buf qc_jmp_buf;
Namespace *qc_namespace = NULL;
Namespace *qc_namespace_list = NULL;

/* Current token, modified by qc_lex */
qctoken_t qc_token;

/*
 * Points to either qc_token_string_buffer or a string literal.
 * It is for dereferencing (as a string, not any other kind of
 * array); the pointer should not be modified by any function
 * besides qc_main() and qc_lex().
 */
char *qc_token_string;

/*
 * Storage buffer for qc_token_string, if it is not a string literal.
 * For the most part, qc_token_string should be referenced, not this.
 */
char qc_token_string_buffer[TOKEN_LEN];

static int load_program(FILE *fp, Namespace *ns);
static int prescan(void);
static int exec_if(void);
static int exec_while(void);
static int exec_do(void);
static void find_eob(void);
static int exec_for(void);
static void qc_cleanup(void);
static int qc_hash_string(Namespace *ns, FILE *fp, char *s, int n);

/**
 * qc_interpret_block - Interpret a single statement or block of code
 *
 * When qc_interpret_block() returns from its initial call, the final
 * brace (or a return) in main() has been encounterd.
 *
 * Return value:
 *  0 on normal end of block.
 *  Greater than zero if block ended with `break' statement.
 *  Less than zero if block ended with `return' statement.
 */
int qc_interpret_block(void)
{
        Atom value;
        int block = 0;
        int tok;
        int ret;

        do {
                qc_lex();
                tok  = QC_TOK(qc_token);

                /* TODO: Implement `break', `goto'. */
                switch (tok) {
                case QC_IDENTIFIER:
                        /* Fall through, same as QC_MULTOK */
                case QC_MULTOK:
                        /* Not a keyword, so process expression. */
                        qcputback();
                        qcexpression(&value);
                        if (QC_TOK(qc_token) != QC_SEMI)
                                qcsyntax(QCE_SEMI_EXPECTED);
                        break;
                case QC_OPENBR:
                        ++block;
                        break;
                case QC_CLOSEBR:
                        --block;
                        break;
                case QC_RETURN:
                        qc_ufunc_ret();
                        goto returnfromblock;
                case QC_IF:
                        /* This is the only kind of block in which
                         * `break' gets us out of the higher-level
                         * block, so check if `break' is there. */
                        ret = exec_if();
                        if (ret > 0)
                                goto breakfromblock;
                        else if (ret < 0)
                                goto returnfromblock;
                        break;
                case QC_ELSE:
                        /* We already processed this when we encountered
                         * Mr. `if', so skip past it to next statement. */
                        find_eob();
                        break;
                case QC_WHILE:
                        if (exec_while() < 0)
                                goto returnfromblock;
                        break;
                case QC_DO:
                        if (exec_do() < 0)
                                goto returnfromblock;
                        break;
                case QC_FOR:
                        if (exec_for() < 0)
                                goto returnfromblock;
                        break;
                case QC_BREAK:
                        goto breakfromblock;
                default:
                        if (QC_ISTYPE(qc_token)) {
                                qcputback();
                                qc_decl_local();
                        }
                        break;
                }
        } while (QC_TOK(qc_token) != QC_FINISHED && block != 0);
        return 0;
breakfromblock:
        return 1;
returnfromblock:
        return -1;
}

/**
 * load_program - Load a program.
 * @fp: File to load
 * @ns: Namespace to load @fp into
 *
 * This is called during initialization, before the prescan. It filters
 * out comments and hashes string literals, converting them into C
 * strings (eg `\' `n' is turned into `\n').
 * In the future this will hash the entire program, writing it out to a
 * byte-code file. The comments and extra whitespece will not be skipped.
 * Rather, the location in the file will be reference by the byte-code
 * tokens, and the file will only be used for traceback messages.
 *
 * But for now we load as it is (with above exceptions), and run it like
 * a pure interpreter.
 *
 * Return value:
 *    zero or a negative enum QC_ERROR_T.
 */
static int load_program(FILE *fp, Namespace *ns)
{
        int i = 0;
        int c;
        int ret;
        char *p = &ns->program_buffer[0];

        /*
         * Read in entire file, skipping over comments and extra
         * white space (not the EOLs). This requires care, because
         * we want to include extra white space in our quotes. We also
         * may have what seem like comments which are really inside
         * quotes.
         */
        i = 0;
        do {
                c = getc(fp);
                if (c == '/') {
                        c = getc(fp);
                        if (c != '*') {
                                ungetc(c, fp);
                                c = '/';
                                goto notacomment;
                        }

                        do {
                                while ((c = getc(fp)) != '*')
                                        CHECK_EOFL(c, p, ret, i, err);

                                c = getc(fp);
                                CHECK_EOFL(c, p, ret, i, err);

                        } while (c != '/');
                }
        notacomment:

                if (c == '"') {
                        *p = c;
                        ++p;
                        ++i;
                        ret = qc_hash_string(ns, fp, p, PROG_SIZE - i);
                        if (ret < 0)
                                goto err;
                        p += ret;
                        i += ret;
                        continue;
                }
                if (isblank(c)) {
                        /* Tabs and spaces only. We keep EOL for line
                         * counts when reporting errors */
                        while (isblank(c))
                                c = getc(fp);
                        ungetc(c, fp);
                        c = ' ';
                        goto putitin;
                }
        putitin:
                *p = c;
                ++p;
                ++i;
        } while (!feof(fp) && i < PROG_SIZE);

        if (*(p - 2) == 0x1A)
                *(p - 2) = '\0';
        else
                *(p - 1) = '\0';
        fclose(fp);
        return 0;
err:
        fclose(fp);
        qc_program_counter = p;
        return ret;
}

/*
 * Find the location of all functions in the program and store global
 * variables
 */
static int prescan(void)
{
        char *p, *p2;
        /* When 0, this var tells us that current source position is
         * outsize of any function */
        int brace = 0;

        switch (setjmp(qc_jmp_buf)) {
        case 0:
                break;
        default:
        case 1:
                /* call to qcsyntax() */
                qc_cleanup();
                return -1;
        }

        p = qc_program_counter;
        do {
                while (brace) {
                        qc_lex();
                        if (QC_TOK(qc_token) == QC_OPENBR)
                                ++brace;
                        else if (QC_TOK(qc_token) == QC_CLOSEBR)
                                --brace;
                }

                qc_lex();

                if (QC_ISTYPE(qc_token)) {
                        qcputback();

                        p2 = qc_program_counter;
                        while (QC_ISTYPE(qc_token)
                            || QC_TOK(qc_token) == QC_MULTOK) {
                                qc_lex();
                        }

                        if (QC_TOK(qc_token) != QC_IDENTIFIER)
                                qcsyntax(QCE_SYNTAX);

                        qc_lex();
                        qc_program_counter = p2;

                        switch (QC_TOK(qc_token)) {
                        case QC_SEMI:
                        case QC_EQEQ:
                                qc_decl_global();
                                break;
                        case QC_OPENPAREN:
                                qc_ufunc_declare();
                                break;
                        default:
                                qcsyntax(QCE_SYNTAX);
                        }
                } else if (QC_TOK(qc_token) == QC_IDENTIFIER) {
                        qcsyntax(QCE_SYNTAX);
                } else if (QC_TOK(qc_token) == QC_OPENBR) {
                        ++brace;
                }
        } while (QC_TOK(qc_token) != QC_FINISHED);

        qc_program_counter = p;
        return 0;
}

int qc_find_ustring(const char *s)
{
        int i;
        struct qc_ustring_t *p;

        i = 0;
        p = qc_namespace->ustrings;

        while (i < qc_namespace->n_ustrings) {
                if (p->p == s)
                        return i;
                ++i;
                ++p;
        }
        qcsyntax(QCE_FATAL);
        return -1;
}

/**
 * qc_hash_string - Put the strings from an input file into a lookup table.
 * @ns: Namespace
 * @fp: Input stream
 * @s: A pointer to the program buffer to store as-is string
 * @n: Maximum number of characters available in s.  This only applies
 *      when n is less then the default maximum string length.
 *
 * This function both:
 *
 * 1. Continues what load_program() was doing (this is just a helper
 * function for load_program), putting each character from the .c or
 * .qc file into the program buffer. This is so the user can see an
 * unaltered line printed if there is a traceback error.
 *
 * 2. converts the string into a C string, and creating a table lookup
 * for it, so that qc_lex does not need to convert the string
 * every time, or even look for the end of it in the program buffer.
 *
 * The table entry contains three pointers: one for the converted
 * C string, one for the start of the string in the program buffer
 * (immediately after the opening quote), and one for the end of the
 * string in the program buffer (immediately after the closing quote)
 *
 * Return value:
 *   Size of the non-converted string in the program buffer, or a
 * enum QC_ERROR_T times -1, if the string is too long or if a converted
 * string cannot be allocated.
 */
static int qc_hash_string(Namespace *ns, FILE *fp, char *s, int n)
{
        #define QCHS_GETC() QCREAD_GETC(c, fp, s, count, n)
        #define QCHS_CHECK_EOF() QCREAD_CHECK_EOF(c, fp);

        char buf[QC_STRING_LEN];
        int c, count = 0;
        char *dst = buf;
        char *ssave = s;
        int octal, octcount;

        if (n > (QC_STRING_LEN - 1))
                n = QC_STRING_LEN - 1;
        if (ns->n_ustrings == QC_N_STRINGS)
                return -QCE_TOO_MANY_STRINGS;

        c = QCHS_GETC();

loop:
        while (c != '"') {
                if (c == '\\') {
                        c = QCHS_GETC();
                        switch (c) {
                        case 'n':
                                *dst++ = '\n';
                                c = QCHS_GETC();
                                break;
                        case 't':
                                *dst++ = '\t';
                                c = QCHS_GETC();
                                break;
                        case '0':
                                /* Octal escape, or perhaps a nul char.
                                 * We cannot actually support the nul
                                 * char, but that's one check too many. */
                                octal = 0;
                                octcount = 2;
                                while ((c = getc(fp)) >= '0'
                                       && c <= '7' && octcount > 0) {
                                        octal *= 8;
                                        octal += c - '0';
                                        *s++ = c;
                                        ++count;
                                        --octcount;
                                        QCREAD_CHECK_LEN(count, n);
                                }
                                QCHS_CHECK_EOF();
                                *dst++ = octal;
                                /* c is either not octal or after limit,
                                 * so we need to try again with this new
                                 * character. */
                                goto loop;
                        case '\\':
                                *dst++ = '\\';
                                c = QCHS_GETC();
                                break;
                        case '"':
                                /* This also prevents early finish in
                                 * case of `\"'. */
                                *dst++ = '"';
                                c = QCHS_GETC();
                                break;
                        case '\r':
                                /* Oh, the hell with it */
                                *dst++ = '\r';
                                c = QCHS_GETC();
                                break;
                        case EOF:
                                goto erreof;
                        default:
                                /* TODO: Warn for this */
                                c = QCHS_GETC();
                                break;
                        }
                } else if (c == '\0' || c == EOF) {
                        goto erreof;
                } else {
                        *dst++ = c;
                        c = QCHS_GETC();
                }
        }

        if (c != '"')
                return -QCE_OVERSIZE_STRING;
        *dst = '\0';

        /* Finished with dst, use it for creating the string */
        dst = strdup(buf);
        if (dst == NULL)
                return -QCE_NOMEM;
        ns->ustrings[ns->n_ustrings].p = ssave;
        ns->ustrings[ns->n_ustrings].s = dst;
        ns->ustrings[ns->n_ustrings].e = s;
        ++ns->n_ustrings;
        return count;
erreof:
        return -QCE_QUOTE_EXPECTED;
}

/*
 * Execute an `if' statement
 *
 * Return Value:
 *   Non-zero (True) if control reached a `break' in the statement,
 * requiring the parent loop to break; zero otherwise.
 */
static int exec_if(void)
{
        Atom cond;
        int ret = 0;
        qcexpression(&cond);
        if (cond.a_value.i) {
                /* get left expression */
                ret = qc_interpret_block();
        } else {
                /* skip around QC_IF block and process QC_ELSE, if
                 * present */
                find_eob();
                qc_lex();

                if (QC_TOK(qc_token) != QC_ELSE) {
                        qcputback();
                        /* End of `if' */
                } else {
                        ret = qc_interpret_block();
                }
        }
        return ret;
}

/* Execute a while loop */
static int exec_while(void)
{
        Atom cond;
        char *progsave;
        int brk = 0;

        qcputback();
        progsave = qc_program_counter;
        qc_lex();
        qcexpression(&cond);
        if (cond.a_value.i) {
                brk = qc_interpret_block();
                if (brk) {
                        qc_program_counter = progsave;
                        goto breakfromloop;
                }
        } else {
                goto breakfromloop;
        }
        /* loop back to top */
        qc_program_counter = progsave;
        return brk;

breakfromloop:
        find_eob();
        return brk;
}

/* Execute a do loop */
static int exec_do(void)
{
        Atom cond;
        char *progsave;
        int brk;

        qcputback();
        progsave = qc_program_counter;

        qc_lex();
        brk = qc_interpret_block();
        if (brk) {
                qc_program_counter = progsave;
                find_eob();
                return brk;
        } else {
                qc_lex();
                if (QC_TOK(qc_token) != QC_WHILE)
                        qcsyntax(QCE_WHILE_EXPECTED);
                qcexpression(&cond);
                if (cond.a_value.i)
                        qc_program_counter = progsave;
        }
        return 0;
}

/*
 * Move qc_program_counter past the end of a parentheses block.
 * qc_program_counter is at start of first parentheses
 *
 * Params:
 *   `open' is the token for the opening parentheses (`(` or `{').
 *   `close' is the token for the closing parentheses (`)' or `}')
 */
static void find_eop(int open, int close)
{
        int blk;

        qc_lex();

        blk = 1;
        while (blk) {
                qc_lex();
                if (QC_TOK(qc_token) == (close))
                        --blk;
                else if (QC_TOK(qc_token) == (open))
                        ++blk;
        }
}

#define find_closing_paren()  find_eop(QC_OPENPAREN, QC_CLOSEPAREN)
#define find_closing_curly()  find_eop(QC_OPENBR, QC_CLOSEBR)

/* find the end of a block */
static void find_eob(void)
{
        qc_lex();

        if (QC_TOK(qc_token) != QC_OPENBR) {
                /* Single statement. This may have nested blocks, all
                 * with or without braces. */
                switch (QC_TOK(qc_token)) {
                case QC_IF:
                case QC_FOR:
                case QC_WHILE:
                        find_closing_paren();
                        /* Fall through */
                case QC_ELSE:
                        find_eob();
                        break;
                /* `do', of course, has to be special. */
                case QC_DO:
                        find_eob();
                        if (QC_TOK(qc_token) != QC_WHILE)
                                qcsyntax(QCE_SYNTAX);
                        find_closing_paren();
                        if (QC_TOK(qc_token) != QC_SEMI)
                                qcsyntax(QCE_SYNTAX);
                        qc_lex();
                        break;
                default:
                        /* Single-line statement. Look just for
                         * closing `;'. Using `while' instead of
                         * `do' because we may have an empty
                         * statement. */
                        while (QC_TOK(qc_token) != QC_SEMI)
                                qc_lex();
                        break;
                }
        } else {
                find_closing_curly();
        }
}

#define GET_SEMI_EXPRESSION(a, s)            \
do {                                         \
        qcexpression(a);                     \
        if (QC_TOK(qc_token) != QC_SEMI)     \
                qcsyntax(QCE_SEMI_EXPECTED); \
        qc_lex();                            \
        qc_program_save(s);                  \
} while (0)

/* Execute a for loop */
static int exec_for(void)
{
        /* XXX: The heavy-duty struct program_save_t may not be
         * necessary here. Try using `char *' instead. */
        Atom cond;
        struct qc_program_t iterator, truthstmt;
        int ret = 0;
        char *progsave;

        qc_lex();

        GET_SEMI_EXPRESSION(&cond, &iterator);

        for (;;) {
                GET_SEMI_EXPRESSION(&cond, &truthstmt);

                /* find the start of the for block */
                /* XXX: Double-work, just to reuse find_closing_paren() */
                qcputback();
                find_closing_paren();

                if (cond.a_value.i) {
                        progsave = qc_program_counter;
                        ret = qc_interpret_block();
                        if (ret) {
                                /* Get the PC back to a reference point
                                 * from which it can find end of block. */
                                qc_program_counter = progsave;
                                goto breakfromloop;
                        }
                } else {
                        goto breakfromloop;
                }
                qc_program_restore(&truthstmt);
                qcexpression(&cond);
                qc_program_restore(&iterator);
        }
        return 0;

breakfromloop:
        find_eob();
        return ret;
}

/*
 * Entry point for initializing a namespace.
 * Calls qc_*_namespace_init() in other QC files.
 */
static int qc_namespace_init(Namespace *namespace)
{
        qc_function_namespace_init(namespace);
        namespace->list = qc_namespace_list;
        qc_namespace_list = namespace;
        return 0;
}

/*
 * Exit point for killing a single namespace.
 */
static void qc_namespace_exit(Namespace *namespace)
{
        /* ustrings are managed in this file. */
        int i;
        for (i = 0; i < namespace->n_ustrings; ++i) {
                if (namespace->ustrings[i].s != NULL)
                        free(namespace->ustrings[i].s);
        }
        memset(namespace->ustrings, 0, sizeof(namespace->ustrings));
        namespace->n_ustrings = 0;

        if (namespace->filepath != NULL)
                free((void *)namespace->filepath);
        qc_function_namespace_exit(namespace);
        free(namespace);
}

/*
 * Do all the cleanup necessary for ending the user's  program.
 */
static void qc_cleanup(void)
{
        Namespace *ns, *ns2;
        for (ns = qc_namespace_list; ns != NULL; ns = ns2) {
                ns2 = ns->list;
                qc_namespace_exit(ns);
        }
        qclib_exit();
        qc_function_exit();
}

/**
 * @brief Get out of a running QC program safely.
 */
void qc_exit(int ret)
{
        if (ret)
                longjmp(qc_jmp_buf, 1);
        else
                longjmp(qc_jmp_buf, 2);
}

/**
 * @brief Initialize QC variables. Called at start of vmebr.
 */
int qc_init(void)
{
        qc_init_parser();
        qclib_init();
        return qc_function_init();
}

/**
 * @brief Load and prescan a file.
 * If the file has a function named `__init__', that will be executed.
 * @param fname Full path name of the file to load.
 * @return zero if the file loaded, a negative integer if not.
 */
int qc_load_file(const char *fname)
{
        Namespace *ns;
        FILE *fp;
        int ret;
        Atom initret;

        ns = malloc(sizeof(Namespace));
        if (ns == NULL) {
                ret = -1;
                goto errmalloc;
        }

        qc_namespace_init(ns);

        /* TODO: It may be better to use only the namespace name */
        ns->filepath = strdup(fname);

        fp = fopen(fname, "rb");
        if (fp == NULL) {
                ret = -1;
                goto errfopen;
        }

        ret = load_program(fp, ns);
        if (ret)
                goto errload;

        qc_program_counter = ns->program_buffer;

        /* Global namespace needs to be set before call to prescan */
        qc_namespace = ns;
        ret = prescan();
        if (ret)
                goto errprescan;

        qc_execute("__init__", &initret, &initret, 0);
        goto done;

errprescan:
        /* TODO: namespace cleanup of ustrings */
errload:
        fclose(fp);
errfopen:
        free(ns);
errmalloc:
done:
        qc_namespace = NULL;
        return ret;
}

int qc_execute(const char *funcname, Atom *ret, Atom args[], int nargs)
{
        Function *f;
        int status;

        /* TODO: Initialize FILE pointers */
        f = qc_func_lookup(funcname);
        if (f == NULL)
                return -1;

        qc_namespace = f->f_namespace;

        switch (setjmp(qc_jmp_buf)) {
        case 0:
                qc_program_counter = f->f_fn.u - 1;
                strcpy(qc_token_string_buffer, funcname);
                qc_token_string = &qc_token_string_buffer[0];
                /* Now we got f, re-NULL namespace; this way
                 * the function call knows that the call is not
                 * from a user-defined function. */
                qc_namespace = NULL;
                qc_func_call(ret, f);
                status = 0;
                break;
        default:
        case 1:
                /* call to qcsyntax() */
                qc_cleanup();
                status = -1;
                break;
        case 2:
                /* Normal return */
                status = 0;
                break;
        }

        /* TODO: Close all opened files */
        qc_namespace = NULL;
        return status;
}

int main(int argc, char **argv)
{
        int ret;
        Atom mainret;

        if (argc != 2) {
                fprintf(stderr, "Usage: %s filename\n", argv[0]);
                return 1;
        }

        qc_init();
        ret = qc_load_file(argv[1]);
        if (ret)
                return ret;
        ret = qc_execute("main", &mainret, &mainret, 0);
        if (!ret)
                qc_cleanup();
        return ret;
}

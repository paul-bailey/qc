#include <stdio.h>
#include "qc.h"
#include "qc_private.h"

/**
 * qc_sterror - Get a string pointer describing an error enumeration.
 * @error: A %QC_ERROR_* error
 */
const char *qc_strerror(int error)
{
        static const char *e[] = {
                "unknown error",
                "syntax error",
                "unbalanced parentheses",
                "no expression present",
                "equals sign expected",
                "not a variable",
                "parameter error",
                "semicolon expected",
                "unbalanced braces",
                "function undefined",
                "type specifier expected",
                "too many nested function calls",
                "return without call",
                "parentheses expected",
                "while expected",
                "closing quote expected",
                "not a string",
                "too many local variables",
                "too many global variables",
                "incorrect number of arguments",
                "comma expected",
                "only pointers supported for FILE structs",
                "nul char/octal escape in literal not supported",
                "string literal is too long",
                "too many files open",
                "string literal limit exceeded",
                "not enough memory available",
                "fatal program bug trap. Contact programmer.",
                "cannot open file",
                "unbalanced comment",
                "unknown type",
                "type mismatch",
                "invalid operation for type",
                "identifier expected",
                "using uninitialized variable",
                "declarations with matching names",
                "entry point `main' not found",
                "cannot dereference non-pointer",
                "accessing pointer out of bounds",
                "user may only make pointers of user-defined variables",
                "arrays may only be declared with numerical literals",
                "double pointers not yet supported",
                "array too big",
                "closing square brace expected",
                "array initialization-at-declaration not supported",
                "insane left/right shifting",
                "array out of bounds",
        };
        const char *s;

        /* In case I forgot */
        if (error < 0)
                error = -error;

        if (error >= QCE_NERRS)
                s = "Unknown error";
        else
                s = e[error];

        return s;
}

/**
 * qc_printerr - Display error but do not attempt a long jump.
 * @error: `enum QC_ERROR_T' error
 * @pc: program counter. In most cases this is the variable
 * qc_program_counter
 */
void qc_printerr(int error, char *pc)
{
        char *p;
        char *s;
        int i;
        int linecount = 0;

        if (pc != NULL) {
                p = qc_namespace->program_buffer;
                while (p != pc) {
                        ++p;
                        if (*p == '\n')
                                ++linecount;
                }

                s = p;
                for (i = 0;
                     i < 20 && p > qc_namespace->program_buffer && *p != '\n';
                     ++i, --p)
                        ;
                fprintf(stderr,
                         "Error, near line %d: ",
                         linecount);
        } else {
                fprintf(stderr, "Error");
        }

        fprintf(stderr, "%s", qc_strerror(error));
        if (*p != '\n')
                putc('\n', stderr);

        for (i = 0; i < 30 && p <= s ; ++i, ++p)
                putc(*p, stderr);

        putc('\n', stderr);
}

/**
 * qcsyntax - Display error message and exit user program.
 * @error: `enum QC_ERROR_T' error
 *
 * This may be called after main() has initialized qc_jmp_buf
 * (after load_program() and before prescan()
 */
void qcsyntax(int error)
{
        qc_printerr(error, qc_program_counter);
        longjmp(qc_jmp_buf, 1);
}

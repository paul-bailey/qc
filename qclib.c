/* Routines that can be called from a little-c script */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qc.h"

FILE *fplist[QC_NFILES];

/*
 * Add a file pointer to the program's list.
 * Return 0 if added to list, -1 if list is full
 */
static int qcaddfp(FILE *fp)
{
        FILE **pfp;
        for (pfp = &fplist[0]; pfp < &fplist[QC_NFILES]; ++pfp) {
                if (*pfp == NULL) {
                        *pfp = fp;
                        return 0;
                }
        }
        return -1;
}

static int qcremovefp(FILE *fp)
{
        FILE **pfp;
        int ret, found = 0;

        for (pfp = &fplist[0]; pfp < &fplist[QC_NFILES]; ++pfp) {
                if (*pfp == fp) {
                        found = 1;
                        break;
                }
        }

        if (!found) {
                ret = -1;
        } else {
                ret = fclose(fp);
                *pfp = NULL;
        }

        return ret;
}

/* Callback for printf module */
static void qcputc(int c, struct printf_reent_t *r)
{
        putchar(c);
        r->count++;
}

/*
 * Equivalent to standard C fopen in most cases.
 * WARNING: If the namespace (IE QC file) calling this function
 * exits early due to an error, the file will remain open.
 */
void qccall_fopen(Atom *ret)
{
        FILE *fp;
        Atom *params[2];

        if (!QC_ISPTR(ret->a_type))
                qcsyntax(QCE_SYNTAX);

        params[0] = iarg_pop();
        params[1] = iarg_pop();

        fp = fopen((char *)params[0]->a_value.p,
                    (char *)params[1]->a_value.p);
        if (fp != NULL) {
                if (qcaddfp(fp)) {
                        fclose(fp);
                        fp = NULL;
                }
        }
        ret->a_value.p = fp;
}

/* Equivalent to fclose in most cases */
void qccall_fclose(Atom *ret)
{
        Atom *value;
        FILE *fp;

        value = iarg_pop();
        if (!QC_ISPTR(value->a_type))
                qcsyntax(QCE_SYNTAX);

        fp = (FILE *)value->a_value.p;

        ret->a_value.i = qcremovefp(fp);
}

/* Equivalent to fputs in most cases */
void qccall_fputs(Atom *ret)
{
        Atom *params[2];
        FILE *fp;
        char *s;

        params[0] = iarg_pop();
        params[1] = iarg_pop();

        if (!QC_ISPTR(params[0]->a_type))
                qcsyntax(QCE_SYNTAX);

        if (!QC_ISPTR(params[1]->a_type))
                qcsyntax(QCE_SYNTAX);

        s = (char *)params[0]->a_value.p;
        fp = (FILE *)params[1]->a_value.p;

        ret->a_value.i = fputs(s, fp);
}

/* Equivalent to getchar */
void qccall_getchar(Atom *ret)
{
        ret->a_value.i = getchar();
}

/* Similar to printf, buf with limitations. Floating point and the
%p conversion are not supported */
void qccall_printf(Atom *ret)
{
        Atom *fmtparam;
        char *fmt;
        qc_va_list args = NULL; /* This will change when we mean it */
        struct printf_reent_t pcall;

        fmtparam = iarg_pop();
        if (!QC_ISPTR(fmtparam->a_type))
                qcsyntax(QCE_SYNTAX);

        fmt = (char *)fmtparam->a_value.p;

        pcall.fn         = qcputc;
        pcall.out.stream = stdout;
        pcall.count      = 0;
        pcall.limit      = -1; /* Needed if we make qcputc check this */

        qcprint_r(fmt, args, &pcall);
        ret->a_value.i = pcall.count;
}

/* Equivalent to puts */
void qccall_puts(Atom *ret)
{
        Atom *params[1];
        char *s;

        params[0] = iarg_pop();
        if (!QC_ISPTR(params[0]->a_type))
                qcsyntax(QCE_SYNTAX);

        s = (char *)params[0]->a_value.p;
        puts(s);
        ret->a_value.i = 0;
}

/*
 * TODO: Replace below implementation with one that exits from all
 */

/*
 * Exit from QC function and all parent functions. This has limited
 * cleanup. If any files were opened by the namespace, they will still be
 * open.
 */
void qccall_exit(Atom *ret)
{
        Atom *param;

        param = iarg_pop();
        ret->a_value.i = param->a_value.i;

        qc_exit(param->a_value.i);
}

/**
 * @brief Initialize private data in qclib.c
 */
void qclib_init(void)
{
        FILE **pfp;
        for (pfp = &fplist[0]; pfp < &fplist[QC_NFILES]; ++pfp)
                *pfp = NULL;
}

/**
 * @brief Clean up private data in qclib.c
 */
void qclib_exit(void)
{
        FILE **pfp;
        for (pfp = &fplist[0]; pfp < &fplist[QC_NFILES]; ++pfp) {
                if (*pfp != NULL)
                        fclose(*pfp);
        }
        qclib_init();
}

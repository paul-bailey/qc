
#include "qc_private.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define INVALID_CONVERSION_SPEC     (-1)

#define PRINT_BUFSIZE               60

#define PRINT_PRECISION_MAX         (PRINT_BUFSIZE - 17)
#define PRINT_WIDTH_MAX             (PRINT_BUFSIZE - 5)

enum {
        LENMOD_CHAR,
        LENMOD_SHORT,
        LENMOD_INT,
        LENMOD_LONG,
        LENMOD_LONGLONG,
};

/**
 * struct print_info_t - Info for a single conversion after a '%'; most
 * of these structs are named 'm' in this file.
 * @lenmod: A %LENMOD_* enum
 * @flag: a %PRINTFLAG_* value
 * @width:
 * @letbase: Character to distinguish capital and lowercase hex output.
 * @base:
 * @precision:
 * @sout: Pointer to a string to print if %s or %c
 * @scr: Small buffer for a single %c conversion
 */
struct print_info_t {
        int  lenmod;
        int  flag;
        int  width;
        int  letbase;
        int  base;
        int  precision;
        char *sout;
        char scr[2];
};

#define PRINTFLAG_PAD_RIGHT         0x001u
#define PRINTFLAG_PAD_ZERO          0x002u
#define PRINTFLAG_FORCE_SIGN        0x004u
#define PRINTFLAG_HASH              0x008u
#define PRINTFLAG_SIGNED            0x010u
#define PRINTFLAG_NEG               0x020u
#define PRINTFLAG_ZERO              0x040u
#define PRINTFLAG_PRECISION         0x080U
#define PRINTFLAG_ISLONGLONG        0x100U
#define PRINTFLAG_ISFLOAT           0x200U

/*
 * Numerical value to print.
 */
union printf_type_t {
        long long i64;
        long      i32;
        int       i;
        short     i16;
        char      i8;
};

/*
 * Convenience function for printing a single character to stream or
 * string.
 */
static void outchar(int c, struct printf_reent_t *r)
{
        r->fn(c, r);
}

/*
 * Sign is included for non-negative numbers if flag set and number
 * is also not zero.
 */
#define issigned(flag)    \
        (((flag) & (PRINTFLAG_FORCE_SIGN | PRINTFLAG_ZERO)) == PRINTFLAG_FORCE_SIGN)

#define ispadr(flag)     \
        (((flag) & PRINTFLAG_PAD_RIGHT) != 0)

#define ispadz(flag)      \
        (((flag) & PRINTFLAG_PAD_ZERO) != 0)

static void qcprints(const char *s, int width, int flag, struct printf_reent_t *r)
{
        register int c;
        int padchar = ' ';

        if (width > 0) {
                int len = strlen(s);

                width = (width < len) ? 0 : width - len;
                if (ispadz(flag))
                        padchar = '0';
        }

        if (!ispadr(flag)) {
                while (width-- > 0)
                        outchar(padchar, r);
        }

        while ((c = *s++) != '\0')
                outchar(c, r);

        while (width-- > 0) {
                /* End-pad null chars if requested */
                outchar(padchar, r);
        }
}

/*
 * Not your typical itoa: s == end of string; put value into string backwards
 * and return pointer to front of new string. Also no null char at end; that's
 * taken care of by the calling function.
 */
static char *qcutoa(unsigned int unsval, char *s, struct print_info_t *m)
{
        while (unsval != 0) {
                // div function for this instead
                unsigned int tval = unsval % m->base;
                if (tval >= 10)
                        tval += m->letbase - '0' - 10;

                *--s = tval + '0';
                if (m->precision != 0)
                        m->precision--;

                unsval /= m->base;
        }

        /* If integer and precision added, fill extra zeros if needed. */
        if ((m->flag & PRINTFLAG_PRECISION) != 0) {
                if ((m->flag & PRINTFLAG_HASH) != 0) {
                        /* If we'll print '0x', etc. include them in the precision */
                        if (m->base == 16)
                                m->precision -= 2;
                        else if (m->base == 8)
                                --m->precision;

                        if (m->precision < 0)
                                m->precision = 0;
                }

                while (m->precision != 0) {
                        *--s = '0';
                        m->precision--;
                }
        }

        return s;
}

/* Long-long version of above */
static char *qculltoa(unsigned long long unsval, char *s, struct print_info_t *m)
{
        while (unsval != 0) {
                // lldiv function for this
                unsigned long long tval = unsval % m->base;
                if (tval >= 10)
                        tval += m->letbase - '0' - 10;

                *--s = tval + '0';
                if (m->precision != 0)
                        m->precision--;

                unsval /= m->base;
        }

        /* If integer and precision added, fill extra zeros if needed. */
        if ((m->flag & PRINTFLAG_PRECISION) != 0) {
                if ((m->flag & PRINTFLAG_HASH) != 0) {
                        /* If we'll print '0x', etc. include them in the precision */
                        if (m->base == 16)
                                m->precision -= 2;
                        else if (m->base == 8)
                                --m->precision;

                        if (m->precision < 0)
                                m->precision = 0;
                }

                while (m->precision != 0) {
                        *--s = '0';
                        m->precision--;
                }
        }

        return s;
}

/* Return true if value is negative */
static int v2sign(union printf_type_t v, struct print_info_t *m)
{
        if ((m->flag & PRINTFLAG_SIGNED) != 0 && m->base == 10) {
                switch (m->lenmod) {
                case LENMOD_CHAR:
                        return v.i8 < 0;
                case LENMOD_SHORT:
                        return v.i16 < 0;
                case LENMOD_INT:
                        return v.i < 0;
                case LENMOD_LONG:
                        return v.i32 < 0;
                case LENMOD_LONGLONG:
                        return v.i64 < 0;
                }
        }
        return FALSE;
}

/* Nest these. Life is simpler like that */
static unsigned long v2u32(union printf_type_t v, struct print_info_t *m, int isneg)
{
        switch (m->lenmod) {
        case LENMOD_CHAR:
                return isneg ? -v.i8 : (v.i8 & 0xFFU);
        case LENMOD_SHORT:
                return isneg ? -v.i16 : (v.i16 & 0xFFFFU);
        case LENMOD_INT:
                return isneg ? -v.i : v.i;
        case LENMOD_LONG:
                return isneg ? -v.i32 : v.i32;
        default:
                assert(FALSE);
                return 0;
        }
}

static unsigned long long v2u64(union printf_type_t v, struct print_info_t *m, int isneg)
{
        assert(m->lenmod == LENMOD_LONGLONG);
        return isneg ? -v.i64 : v.i64;
}

static void qcprintn(union printf_type_t value, struct print_info_t *restrict m, struct printf_reent_t *restrict r)
{
        char buf[PRINT_BUFSIZE]; /* Safe for 64-bit octal */
        char *s;
        int neg;

        s = buf + (PRINT_BUFSIZE - 1);
        *s = '\0';

        /* Get sign */
        neg = v2sign(value, m);

        /* Get unsigned value */
        if ((m->flag & PRINTFLAG_ISLONGLONG) == 0) {
                /* 32-bit-or-less int: most common */
                unsigned long u;

                u = v2u32(value, m, neg);
                if (u == 0) {
                        *--s = '0';
                        m->flag |= PRINTFLAG_ZERO;
                } else {
                        s = qcutoa(u, s, m);
                }
        } else {
                /* 64 bit int: less common */
                unsigned long long u64;

                u64 = v2u64(value, m, neg);
                if (u64 == 0) {
                        *--s = '0';
                        m->flag |= PRINTFLAG_ZERO;
                } else {
                        s = qculltoa(u64, s, m);
                }
        }

        /* Hash and sign options XXX This should not need two whole methods */
        if (m->width != 0 && ispadz(m->flag)) {
                int c;

                /* Send out the sign and hash option (in that order)
                 * first, before call to qcprints */
                if (neg || issigned(m->flag)) {
                        /* Sign */
                        c = neg ? '-' : '+';
                        outchar(c, r);
                        --m->width;
                }

                if ((m->flag & PRINTFLAG_HASH) != 0) {
                        /* "0x" or "0" -- hash option */
                        if (m->base == 16) {
                                outchar('0', r);
                                c = ((m->letbase == 'A') ? 'X' : 'x');
                                outchar(c, r);
                                m->width -= 2;
                        } else if (m->base == 8) {
                                outchar('0', r);
                                --m->width;
                        }
                }
        } else {
                /* Put hash options and sign in string (in that order,
                 * backwards), before call to qcprints */
                if ((m->flag & PRINTFLAG_HASH) != 0) {
                        /* "0x" or "0" -- hash option */
                        if (m->base == 16) {
                                *--s = ((m->letbase == 'A') ? 'X' : 'x');
                                *--s = '0';
                        } else if (m->base == 8) {
                                *--s = '0';
                        }
                }

                if (neg || issigned(m->flag))
                        *--s = neg ? '-' : '+';
        }

        qcprints(s, m->width, m->flag, r);
}

static void mdatainit(struct print_info_t *m)
{
        m->width     = 0;
        m->flag      = 0;
        m->lenmod    = LENMOD_INT;
        m->base      = 0;
        m->precision = 6;
        m->sout      = 0;
}

/*
 * Our converted values are put into a string on the stack before they
 * are printed. This way they can be converted backwards without using
 * power functions, etc., but it also means we have a limit to the field
 * width of our numbers. Check for those limits here.
 */
static void checklength(struct print_info_t *m)
{
        if (m->width > PRINT_WIDTH_MAX) {
                m->width = PRINT_WIDTH_MAX;
                assert(FALSE);
        }
        if (m->precision > PRINT_PRECISION_MAX) {
                m->precision = PRINT_PRECISION_MAX;
                assert(FALSE);
        }
}

/*
 * Precision check -- integers may have zero paddings
 * only if no precision was used. Try to catch it if
 * debugging, and save some embarrassment.
 */
static void  checkprecision(struct print_info_t *m)
{
        if ((m->flag & (PRINTFLAG_PRECISION | PRINTFLAG_PAD_ZERO))
            == (PRINTFLAG_PRECISION | PRINTFLAG_PAD_ZERO)) {
                m->flag &= ~PRINTFLAG_PAD_ZERO;
        }
}

static union printf_type_t getvalue(struct print_info_t *m, union printf_type_t v, qc_va_list args)
{
        v.i64 = 0;
        switch (m->lenmod) {
        case LENMOD_CHAR:
                v.i8 = (char)qc_va_arg(args, int);
                break;
        case LENMOD_SHORT:
                v.i16 = (short)qc_va_arg(args, int);
                break;
        case LENMOD_INT:
                v.i = qc_va_arg(args, int);
                break;
        case LENMOD_LONG:
                v.i32 = qc_va_arg(args, long);
                break;
        case LENMOD_LONGLONG:
                v.i64 = qc_va_arg(args, long long);
                break;
        }
        return v;
}

/*
 * Parse the flags between % and (number)(dusxX). Default the
 * precision for now.
 */
const char *getflags(const char *s, struct print_info_t *m)
{
        register int c;
        for (;;) {
                c = *s;
                ++s;
                switch (c) {
                case 'h':
                        if (*s == 'h') {
                                m->lenmod = LENMOD_CHAR;
                                ++s;
                        } else {
                                m->lenmod = LENMOD_SHORT;
                        }
                        break;
                case 'l':
                        if (*s == 'l') {
                                m->lenmod = LENMOD_LONGLONG;
                                m->flag |= PRINTFLAG_ISLONGLONG;
                                ++s;
                        } else {
                                m->lenmod = LENMOD_LONG;
                        }
                        break;
                case '0':
                        m->flag |= PRINTFLAG_PAD_ZERO;
                        break;
                case '+':
                        m->flag |= PRINTFLAG_FORCE_SIGN;
                        break;
                case '-':
                        m->flag |= PRINTFLAG_PAD_RIGHT;
                        break;
                case '#':
                        m->flag |= PRINTFLAG_HASH;
                        break;
                case '\0':
                        s = NULL;
                        goto done;
                default:
                        --s;
                        goto done;
                }
        }
done:
        return s;
}

static const char *getwritewidth(const char *restrict s, struct print_info_t *restrict m)
{
        char *snew;
        m->width = strtoul(s, &snew, 10);
        return snew;
}

static const char *getprecision(const char *restrict s, struct print_info_t *restrict m)
{
        char *snew;
        int c;

        c = *s;
        if (c == '\0') {
                return NULL;
        } else if (c == '.') {
                ++s;
                m->flag |= PRINTFLAG_PRECISION;
                m->precision = strtoul(s, &snew, 10);
        }
        return s;
}

static const char *getconversion(const char *restrict s, struct print_info_t *restrict m, qc_va_list args)
{
        register int c = *s;
        ++s;
        switch (c) {
        case 's':
                m->sout = (char *)qc_va_arg(args, char *);
                break;
        case 'i':
        case 'd':
                m->flag   |= PRINTFLAG_SIGNED;
                m->base    = 10;
                m->letbase = 'a';
                break;
        case 'x':
                m->base    = 16;
                m->letbase = 'a';
                break;
        case 'X':
                m->base    = 16;
                m->letbase = 'A';
                break;
        case 'u':
                m->base    = 10;
                m->letbase = 'a';
                break;
        case 'c':
                m->scr[0] = (char)qc_va_arg(args, int);
                m->scr[1] = '\0';
                m->sout = m->scr;
                break;
        case 'o':
                m->base    = 8;
                m->letbase = 'a';
                break;
        case '\0':
                assert(FALSE);
                return NULL;
        default:
                /* Invalid conversion char means ignore whole
                 * thing and print this char */
                --s;
                m->base = INVALID_CONVERSION_SPEC;
                break;
        }
        return s;
}

void qcprint_r(const char *restrict format, qc_va_list args, struct printf_reent_t *restrict r)
{
        register int c;
        struct print_info_t m;

        while (*format != '\0') {
                c = *format;
                ++format;
                if (c != '%') {
                        outchar(c, r);
                } else {
                        mdatainit(&m);

                        if (*format == '\0') {
                                /* Got a string that ended with "%", a
                                 * string literal error. */
                                assert(FALSE);
                                break;
                        } else if (*format == '%') {
                                /* Got "%%", means "print '%'" */
                                outchar(c, r);
                                ++format;
                                continue;
                        }

                        format = getflags(format, &m);
                        if (format == NULL)
                                return;

                        format = getwritewidth(format, &m);
                        if (format == NULL)
                                return;

                        format = getprecision(format, &m);
                        if (format == NULL)
                                return;

                        format = getconversion(format, &m, args);
                        if (format == NULL)
                                return;

                        checkprecision(&m);
                        checklength(&m);

                        if (m.base > 0) {
                                union printf_type_t value;
                                value = getvalue(&m, value, args);
                                qcprintn(value, &m, r);
                        } else if (m.base == 0) {
                                qcprints(m.sout ? m.sout : "(null)",
                                          m.width, m.flag, r);
                        }
                        /* m.base < 0 means invalid parameters */
                        assert(m.base >= 0);
                }
        }
}

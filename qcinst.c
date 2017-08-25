#include "qc.h"
#include "qc_private.h"
#include <string.h>
#include <math.h>

/*
 * True if binary operations are permitted for type of atom
 * `a'. `a' may not be a pointer or a floating point atom.
 */
#define QCINST_ISBIN(a)       QC_ISINT((a)->a_type)

static void qc_swap_data(Atom *left, Atom *right)
{
        Atom tmp;
        memcpy(&tmp, left, sizeof(Atom));
        memcpy(left, right, sizeof(Atom));
        memcpy(right, &tmp, sizeof(Atom));
}

/**
 * qc_ptr_add - Perform addition between two data, in which the
 *              operand is a pointer.
 * @left: the operand
 * @right: the value to operate
 * @operation: the opcode, either '+' or '-'
 */
static void qc_ptr_add(Atom *left, Atom *right)
{
        long int size = sizeof(Variable);

        switch (QC_TYPEOF(right->a_type)) {
        case QC_CHAR:
                left->a_value.p += right->a_value.c * size;
                break;
        case QC_INT:
                left->a_value.p += right->a_value.i * size;
                break;
        default:
                qcsyntax(QCE_TYPE_INVAL);
        }
}

/* Like qc_ptr_addition, but subtraction instead. */
static void qc_ptr_sub(Atom *left, Atom *right)
{
        long int size = sizeof(Variable);

        switch (QC_TYPEOF(right->a_type)) {
        case QC_CHAR:
                left->a_value.p -= right->a_value.c * size;
                break;
        case QC_INT:
                left->a_value.p -= right->a_value.i * size;
                break;
        default:
                qcsyntax(QCE_TYPE_INVAL);
        }
}


/**
 * qc_get_int_operand - Get the operand for an integer type operation.
 * @v: the Atom struct containing the operand info.
 *
 * In the interest of sanity, all integer-type operands are
 * signed long long integers.
 *
 * Warning: This means that unsigned 64-bit multiplication/
 * division are unsafe for unsigned numbers higher than
 * 2^63 - 1.
 *
 * Return: Value of v cast to a signed long long int. If v is
 * unsigned or positive, the bits of the return value that
 * exceed the bit length of v's value will be empty. If v
 * is signed and negative, then the return value will be
 * negative.
 */
static long long qc_get_int_operand(Atom *v)
{
        long long operand;
        switch (QC_TYPEOF(v->a_type)) {
        case QC_CHAR:
                operand = (long long)v->a_value.c;
                break;
        case QC_INT:
                operand = (long long)v->a_value.i;
                break;
        case QC_UINT:
                operand = (unsigned long long)v->a_value.ui;
                break;
        case QC_UCHAR:
                operand = (unsigned long long)v->a_value.uc & 0xFFULL;
                break;
        default:
                qcsyntax(QCE_TYPE_INVAL);
                break;
        }
        return operand;
}

/*
 * Crop an integer to its width. This should happen after every
 * arithmetic operation or cast, and before every logical operation.
 */
void qc_int_crop(Atom *v)
{
        unsigned long long llv = v->a_value.ulli;

        v->a_value.ulli = 0ULL;

        switch (QC_TYPEOF(v->a_type)) {
        case QC_CHAR:
                v->a_value.c = (char)llv;
                break;
        case QC_INT:
                v->a_value.i = (int)llv;
                break;
        case QC_UCHAR:
                v->a_value.uc = (unsigned char)llv;
                break;
        case QC_UINT:
                v->a_value.ui = (unsigned int)llv;
                break;
        default:
                qcsyntax(QCE_TYPE_INVAL);
        }
}

static double qc_get_flt_operand(Atom *v)
{
        switch (QC_TYPEOF(v->a_type)) {
        case QC_CHAR:
                return (double)v->a_value.c;
        case QC_INT:
                return (double)v->a_value.i;
        case QC_UCHAR:
                return (double)v->a_value.uc;
        case QC_UINT:
                return (double)v->a_value.ui;
        case QC_FLT:
                return (double)v->a_value.f;
        case QC_DBL:
                return v->a_value.d;
        default:
                qcsyntax(QCE_TYPE_INVAL);
        }
        return 0.;
}

static void qc_put_flt_operand(Atom *v, double d)
{
        switch (QC_TYPEOF(v->a_type)) {
        case QC_FLT:
                v->a_value.f = (float)d;
                break;
        case QC_DBL:
                v->a_value.d = d;
                break;
        default:
                /* Programmer only means to call this for
                 * value that is known to be floating point */
                qcsyntax(QCE_FATAL);
        }
}

static void qc_fmul(Atom *left, Atom *right)
{
        double dl = qc_get_flt_operand(left);
        double dr = qc_get_flt_operand(right);
        qc_put_flt_operand(left, dl * dr);
}

static void qc_fdiv(Atom *left, Atom *right)
{
        double dl = qc_get_flt_operand(left);
        double dr = qc_get_flt_operand(right);

        /* If dr is zero, result should cause isinf() to
         * return true on result. */
        qc_put_flt_operand(left, dl / dr);
}

static void qc_fadd(Atom *left, Atom *right)
{
        double dl = qc_get_flt_operand(left);
        double dr = qc_get_flt_operand(right);
        qc_put_flt_operand(left, dl + dr);
}

static void qc_fsub(Atom *left, Atom *right)
{
        double dl = qc_get_flt_operand(left);
        double dr = qc_get_flt_operand(right);
        qc_put_flt_operand(left, dl - dr);
}

/*
 * Multiplication for both signed and unsigned.
 * More care is needed here than with addition or subtraction.
 */
static void qc_int_mul(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);

        lval *= rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_div(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);

        if (rval != 0) {
                lval /= rval;
        } else {
                /* TODO: Warn user */
                lval = 0;
        }
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_mod(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);

        lval %= rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_add(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);

        lval += rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_sub(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);

        lval -= rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_or(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);
        lval |= rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_and(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);
        lval &= rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_xor(Atom *left, Atom *right)
{
        long long lval, rval;
        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);
        lval ^= rval;
        left->a_value.lli = lval;
        qc_int_crop(left);
}

static void qc_int_anot(Atom *v)
{
        long long val;
        val = qc_get_int_operand(v);
        val = ~val;
        v->a_value.lli = val;
        qc_int_crop(v);
}

static void qc_int_lnot(Atom *v)
{
        long long val;
        val = qc_get_int_operand(v);
        if (val != 0LL)
                val = 0LL;
        else
                val = 1ULL;
        v->a_value.lli = val;
        qc_int_crop(v);
}

static void qc_int_asl(Atom *v, Atom *amt)
{
        unsigned long long lval;
        long long rval;
        lval = qc_get_int_operand(v);
        rval = qc_get_int_operand(amt);
        if (rval > 1000 || rval < -1000)
                qcsyntax(QCE_SYNTAX);
        lval <<= rval;
        v->a_value.lli = lval;
        qc_int_crop(v);
}

static void qc_int_asr(Atom *v, Atom *amt)
{
        unsigned long long lval;
        long long rval;
        lval = qc_get_int_operand(v);
        rval = qc_get_int_operand(amt);
        if (rval > 1000 || rval < -1000)
                qcsyntax(QCE_SYNTAX);
        lval >>= rval;
        v->a_value.lli = lval;
        qc_int_crop(v);
}

static int qc_int_cmp(Atom *left, Atom *right, int op)
{
        unsigned long long lval, rval;
        int result;

        lval = qc_get_int_operand(left);
        rval = qc_get_int_operand(right);
        switch (op) {
        case QC_LT:
                result = lval < rval;
                break;
        case QC_LE:
                result = lval <= rval;
                break;
        case QC_GT:
                result = lval > rval;
                break;
        case QC_GE:
                result = lval >= rval;
                break;
        case QC_EQ:
                result = lval == rval;
                break;
        case QC_NE:
                result = lval != rval;
                break;
        default:
                result = 0;
                qcsyntax(QCE_FATAL);
                break;
        }
        return result;
}

static int qc_fcmp(Atom *left, Atom *right, int op)
{
        double lval, rval;
        int result;

        lval = qc_get_flt_operand(left);
        rval = qc_get_flt_operand(right);
        switch (op) {
        case QC_LT:
                result = isless(lval, rval);
                break;
        case QC_LE:
                result = islessequal(lval, rval);
                break;
        case QC_GT:
                result = isgreater(lval, rval);
                break;
        case QC_GE:
                result = isgreaterequal(lval, rval);
                break;

        /* Permit these, but user shouldn't use them */
        case QC_EQ:
                result = lval == rval;
                break;
        case QC_NE:
                result = lval != rval;
                break;
        default:
                result = 0;
                qcsyntax(QCE_FATAL);
                break;
        }
        return result;
}

static int qc_ptr_cmp(Atom *left, Atom *right, int op)
{
        void *lp, *rp;
        int result;

        if (!QC_ISPTR(right->a_type)) {
                if (QC_TOK(right->a_type) == QC_NUMBER) {
                        /* Spooky. Don't dereference this
                         * anywhere. */
                        rp = (void *)right->a_value.lli;
                } else {
                        qcsyntax(QCE_TYPE_MISMATCH);
                }
        } else {
                rp = right->a_value.p;
        }
        lp = left->a_value.p;
        switch (op) {
        case QC_LT:
                result = lp < rp;
                break;
        case QC_LE:
                result = lp <= rp;
                break;
        case QC_GT:
                result = lp > rp;
                break;
        case QC_GE:
                result = lp >= rp;
                break;
        case QC_EQ:
                result = lp == rp;
                break;
        case QC_NE:
                result = lp != rp;
                break;
        default:
                result = 0;
                qcsyntax(QCE_FATAL);
                break;
        }
        return result;
}

static void qc_fmov(Atom *to, Atom *from)
{
        double d = qc_get_flt_operand(from);
        qc_put_flt_operand(to, d);
}

static void qc_ptr_mov(Atom *to, Atom *from)
{
        if (!QC_ISPTR(from->a_type)) {
                if (QC_TOK(from->a_type) == QC_NUMBER)
                        to->a_value.p = (void *)from->a_value.lli;
                else
                        qcsyntax(QCE_TYPE_INVAL);
        } else {
                to->a_value.p = from->a_value.p;
        }

}

static void qc_int_mov(Atom *to, Atom *from)
{
        long long val;
        val = qc_get_int_operand(from);

        to->a_value.lli = val;
        qc_int_crop(to);
}


void qc_mov(Atom *to, Atom *from)
{
        if (QC_ISPTR((to)->a_type))
                qc_ptr_mov(to, from);
        else if (QC_ISFLT((to)->a_type))
                qc_fmov(to, from);
        else
                qc_int_mov(to, from);
}

void qc_add(Atom *to, Atom *from)
{
        if (QC_ISPTR(from->a_type)) {
                if (QC_ISPTR(to->a_type)) {
                        /* Do not permit adding
                         * two pointer types */
                        qcsyntax(QCE_TYPE_INVAL);
                }
                /*
                 * Cast result to pointer, even if
                 * it is on the right hand side.
                 */
                qc_ptr_add(from, to);
                qc_swap_data(from, to);
        } else if (QC_ISPTR(to->a_type)) {
                /* No need to check again if RHS is pointer.
                 * LHS is pointer and RHS is something else.
                 * If it is floating point, oh well. It will
                 * be cast to an integer type. */
                qc_ptr_add(to, from);
        } else if (QC_ISFLT((to)->a_type)) {
                /* LHS and RHS will both be cast to double for the
                 * addition. Result will be re-cast to type of `to'. */
                qc_fadd(to, from);
        } else {
                /* LHS and RHS will be cast to signed long long int
                 * for the operation. This should be safe for all
                 * but `unsigned long long int' operations -- a
                 * rare occasion for programs that deal with lots
                 * of 16-bit VME registers. */
                qc_int_add(to, from);
        }
}

void qc_sub(Atom *to, Atom *from)
{
        /* See comments in qc_add about the casting */
        if (QC_ISPTR(from->a_type)) {
                /* Cannot subtract a pointer from
                 * an integer type. */
                qcsyntax(QCE_TYPE_INVAL);
        } else if (QC_ISPTR(to->a_type)) {
                qc_ptr_sub(to, from);
        } else if (QC_ISFLT((to)->a_type)) {
                qc_fsub(to, from);
        } else {
                qc_int_sub(to, from);
        }
}

void qc_mul(Atom *to, Atom *from)
{
        if (QC_ISPTR(to->a_type) || QC_ISPTR(from->a_type))
                qcsyntax(QCE_TYPE_INVAL);
        else if (QC_ISFLT(to->a_type))
                qc_fmul(to, from);
        else
                qc_int_mul(to, from);
}

void qc_div(Atom *to, Atom *from)
{
        if (QC_ISPTR(to->a_type) || QC_ISPTR(from->a_type))
                qcsyntax(QCE_TYPE_INVAL);
        else if (QC_ISFLT(to->a_type))
                qc_fdiv(to, from);
        else
                qc_int_div(to, from);
}

void qc_mod(Atom *to, Atom *from)
{
        if (QC_ISPTR(to->a_type) || QC_ISPTR(from->a_type)
         || QC_ISFLT(to->a_type)) {
                qcsyntax(QCE_TYPE_INVAL);
        } else {
                /* Modulus on two integer types only */
                qc_int_mod(to, from);
        }
}

/**
 * qc_cmp - Compare two QC atoms.
 * @v1: Item to compare. This parameter determines type for
 *      comparison.
 * @v2: Item to compare. If this is a different type than v1,
 *      then a copy of its value will be cast to the type of `v1' for
 *      the comparison.
 * @op: A relational operations enumeration, one of: QC_LT, QC_LE,
 *      QC_GT, QC_GE, QC_EQ, or QC_NE.
 *
 * Return: true if the test determined by `op' passed, false if not.
 */
int qc_cmp(Atom *v1, Atom *v2, int op)
{
        int result;

        if (QC_ISPTR(v1->a_type))
                result = qc_ptr_cmp(v1, v2, op);
        else if (QC_ISFLT(v1->a_type))
                result = qc_fcmp(v1, v2, op);
        else
                result = qc_int_cmp(v1, v2, op);
        return result;
}

/**
 * qc_or - Perform an arithmetic OR ('|', not '||') operation.
 * @to: First value, which will also receive the result. (This
 *      also dominates the type cast, as with the other operations in this
 *      file.)
 * @from: Second value.
 */
void qc_or(Atom *to, Atom *from)
{
        if (!QCINST_ISBIN(to))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_or(to, from);
}

/**
 * qc_and - Perform an arithmetic AND ('&' -- one, not two) operation.
 * @to: First value, which will also receive the result. (This
 *      also dominates the type cast, as with the other operations in this
 *      file.)
 * @from: Second value.
 */
void qc_and(Atom *to, Atom *from)
{
        if (!QCINST_ISBIN(to))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_and(to, from);
}

/**
 * qc_xor - Perform an arithmetic XOR ('^') operation.
 * @to: First value, which will also receive the result. (This
 *      also dominates the type cast, as with the other operations in this
 *      file.)
 * @from: Second value.
 */
void qc_xor(Atom *to, Atom *from)
{
        if (!QCINST_ISBIN(to))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_xor(to, from);
}

/**
 * qc_anot - Perform an arithmetic NOT ('~') operation.
 * @v: Value to operate upon. This is also the output.
 */
void qc_anot(Atom *v)
{
        if (!QCINST_ISBIN(v))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_anot(v);
}

/**
 * qc_lnot - Perform a login NOT ('!') operation.
 * @v: Value to operate upon. This is also the output.
 */
void qc_lnot(Atom *v)
{
        if (!QCINST_ISBIN(v))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_lnot(v);
}

/**
 * qc_asl - Perform an arithmetic left shift ('<<') operation.
 * @v: Value to shift. This is also the output.
 * @amt: Atom holding the amount.
 */
void qc_asl(Atom *v, Atom *amt)
{
        if (!QCINST_ISBIN(v))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_asl(v, amt);
}

/**
 * qc_asr - Perform an arithmetic right shift ('>>') operation.
 * @v: Value to shift. This is also the output.
 * @amt: Atom holding the amout.
 *
 * The shifted bits will be replaced with zeros, not the MSB
 */
void qc_asr(Atom *v, Atom *amt)
{
        if (!QCINST_ISBIN(v))
                qcsyntax(QCE_TYPE_INVAL);
        qc_int_asr(v, amt);
}

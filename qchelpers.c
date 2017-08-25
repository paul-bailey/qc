#include "qc.h"
#include "qc_private.h"
#include <string.h>

/*
 * Functions for saving/restoring user program state. These heavier-duty
 * functions are used where it isn't safe to save just one or two of the
 * variables.
 */

void qc_program_save(struct qc_program_t *penv)
{
        strcpy(penv->pb_tksbuf, qc_token_string);
        penv->pb_tok  = qc_token;
        penv->pb_tks  = qc_token_string;
        penv->pb_pc   = qc_program_counter;
        penv->pb_pcsv = qc_program_counter_save;
}

void qc_program_restore(struct qc_program_t *penv)
{
        strcpy(qc_token_string, penv->pb_tksbuf);
        qc_token                = penv->pb_tok;
        qc_token_string         = penv->pb_tks;
        qc_program_counter      = penv->pb_pc;
        qc_program_counter_save = penv->pb_pcsv;
};

/*
 * Hash functions
 */

/**
 * qc_symbol_hash - Get the hash function of an actual C string
 */
hash_t qc_symbol_hash(const char *s)
{
        hash_t ret = 0;
        while (*s != '\0') {
                ret = 31 * ret + *s;
                ++s;
        }
        return ret;
}

/**
 * qc_symbol_hash2delim - Get the hash function of an alpha-numerical
 * string inside the user's program code.
 */
hash_t qc_symbol_hash2delim(const char *s)
{
        hash_t ret = 0;
        while (!QCCHAR_ISDELIM(*s)) {
                ret = 31 * ret + *s;
                ++s;
        }
        return ret;
}

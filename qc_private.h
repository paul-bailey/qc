/*
 * Declarations, types, macros, etc., that are used internally by files
 * `qc*.c'
 */
#ifndef QC_PRIVATE_H
#define QC_PRIVATE_H

#include "qc.h"


extern char qc_tokmap[];
#define QC_TKC_ (0x01U)
#define QC_TKL_ (0x02U)
#define QC_TKA_ (0x04U)
#define QC_TKS_ (0x08U)
#define QC_TKB_ (0x10U)
#define QC_TKM_ (0x20U)
#define QC_TKU_ (0x40U)

#define QC_ISCMP_OP(tk)       ((qc_tokmap[(tk) & 0x7FU] & QC_TKC_) != 0)
#define QC_ISLOG_OP(tk)       ((qc_tokmap[(tk) & 0x7FU] & QC_TKL_) != 0)
#define QC_ISASGN_OP(tk)      ((qc_tokmap[(tk) & 0x7FU] & QC_TKA_) != 0)
#define QC_ISSHIFT_OP(tk)     ((qc_tokmap[(tk) & 0x7FU] & QC_TKS_) != 0)
#define QCTOK_ISBINARY(tk)    ((qc_tokmap[(tk) & 0x7FU] & QC_TKB_) != 0)
#define QCTOK_ISMULDIVMOD(tk) ((qc_tokmap[(tk) & 0x7FU] & QC_TKM_) != 0)
#define QCTOK_ISUNARY(tk)     ((qc_tokmap[(tk) & 0x7FU] & QC_TKU_) != 0)

/*
 * Saved state of the program, for when qcputback() is insufficient.
 */
struct qc_program_t {
        char pb_tksbuf[ID_LEN];
        qctoken_t pb_tok;
        char *pb_tks;
        char *pb_pc;
        char *pb_pcsv;
};

#define qc_func_call(var, p)  (p)->f_call(var, p);

extern Namespace *qc_namespace_list; /* For easy cleanup */
extern qctoken_t qc_token;
extern char *qc_token_string;
extern char qc_token_string_buffer[];
extern char *qc_program_counter;
extern jmp_buf qc_jmp_buf;

/* qcfunction.c */
extern Function *qc_func_lookup(const char *name);
extern int qc_insert_fn(Function *f);
extern void qc_ufunc_ret(void);
extern void qc_ufunc_declare(void);
extern void qc_decl_local(void);
extern void qc_decl_local_array(void);
extern void qc_decl_global(void);
extern void qc_decl_global_array(void);
extern Variable *qc_uvar_lookup(const char *s);
extern void assign_var(const char *s, Atom *v);
#define qc_uvar_bound_check(p) 0 /* deprecated */
extern void assign_var_deref(Variable *p, Atom *v);
extern int qc_function_init(void);
extern void qc_function_namespace_init(Namespace *namespace);
extern void qc_function_namespace_exit(Namespace *ns);
extern void qc_function_exit(void);

/* qcparse.c */
extern void qcexpression(Atom *a);
extern void qcputback(void);
extern qctoken_t qc_lex(void);
extern void qc_init_parser(void);
extern char *qc_program_counter_save;

/* qcread.c */
extern char *qc_program_counter;
extern qctoken_t qc_token;
extern char *qc_token_string;
extern char qc_token_string_buffer[TOKEN_LEN];
extern struct qc_ustring_t qc_ustrings[QC_N_STRINGS];
extern int qc_n_ustrings;
extern int qc_find_ustring(const char *s);
extern int qc_interpret_block(void);

/* qcinst.c */
extern void qc_int_crop(Atom *v);
extern void qc_mov(Atom *to, Atom *from);
extern void qc_add(Atom *to, Atom *from);
extern void qc_sub(Atom *to, Atom *from);
extern void qc_mul(Atom *to, Atom *from);
extern void qc_div(Atom *to, Atom *from);
extern void qc_mod(Atom *to, Atom *from);
extern int qc_cmp(Atom *v1, Atom *v2, int op);
extern void qc_or(Atom *to, Atom *from);
extern void qc_and(Atom *to, Atom *from);
extern void qc_xor(Atom *to, Atom *from);
extern void qc_anot(Atom *v);
extern void qc_lnot(Atom *v);
extern void qc_asl(Atom *v, Atom *amt);
extern void qc_asr(Atom *v, Atom *amt);

/* qcerr.c */
extern const char *qc_strerror(int error);
extern void qc_printerr(int error, char *pc);

/* qchelpers.c */
extern hash_t qc_symbol_hash(const char *s);
extern hash_t qc_symbol_hash2delim(const char *s);
extern void qc_program_save(struct qc_program_t *penv);
extern void qc_program_restore(struct qc_program_t *penv);

/* qcchar.c */
extern char qc_cmap[];
#define QCD_  0x01
#define QCB_  0x02
#define QCU_  0x04
#define QCBI_ 0x08
#define QCMD_ 0x10
#define QCCHAR_ISDELIM(c) \
                        ((qc_cmap[(c) & 0x7FU] & QCD_) != 0)
#define QCCHAR_ISBLOCKDELIM(c) \
                        ((qc_cmap[(c) & 0x7FU] & QCB_) != 0)
#define QCCHAR_ISUNARY(c) \
                        ((qc_cmap[(c) & 0x7FU] & QCU_) != 0)
#define QCCHAR_ISBINARY(c) \
                        ((qc_cmap[(c) & 0x7FU] & QCBI_) != 0)
#define QCCHAR_ISMULDIVMOD(c) \
                        ((qc_cmap[(c) & 0x7FU] & QCMD_) != 0)

extern char qc_ch2tok[];





#endif /* QC_PRIVATE_H */

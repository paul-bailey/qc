/*
 * Program to generate a character map table for qc, so that we don't
 * need all these strchr() calls to slow down the runtime parser.
 * Direct the program output to a temporary file, edit the generated
 * table as needed, then merge it into ../qcchar.c.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../qc.h"
#include "../qc_private.h"

void mkheader(void)
{
        printf("/*\n");
        printf(" * An automatically generated (& therefore ugly) map table to determine\n");
        printf(" * type of delimiter. This should be faster than the excessive strchrs\n");
        printf(" * that were being used.\n");
        printf(" *\n");
        printf(" * Modify this at your own risk.\n");
        printf(" */\n");

        printf("#include \"qc.h\"\n");
        printf("#include \"qc_private.h\"\n");
}

void mkchar(void)
{
        int count, row, column, next;
        printf("char qc_cmap[] = {\n");

        count = 0;
        for (row = 0; row < 128 / 4; ++row) {
                printf("     /*");
                for (column = 0; column < 4; ++column) {
                        if (isgraph(count + column)) {
                                printf(" `%c'", count + column);
                        } else {
                                switch (count + column) {
                                case '\t':
                                        printf(" `\\t'");
                                        break;
                                case '\n':
                                        printf(" `\\n'");
                                        break;
                                case '\r':
                                        printf(" `\\r'");
                                        break;
                                case ' ':
                                        printf(" ` '");
                                        break;
                                default:
                                        printf(" 0x%X", count + column);
                                        break;
                                }
                        }
                }
                printf(" */\n");

                printf("        ");
                for (column = 0; column < 4; ++column) {
                        if (count == 0) {
                                /* Special case, since the nul char gets
                                 * counted with all the strchrs */
                                printf("QCD_, ");
                                ++count;
                                continue;
                        }

                        next = 0;
                        if (strchr("-*!~&", count)) {
                                printf("QCU_");
                                next = 1;
                        }
                        if (strchr("{}", count)) {
                                if (next)
                                        printf(" | ");
                                printf("QCB_");
                                next = 1;
                        }
                        if (strchr(" !;,+-<>'/*%^=()\t\n&|[]{}", count)) {
                                if (next)
                                        printf(" | ");
                                printf("QCD_");
                                next = 1;
                        }
                        if (strchr("^&|", count)) {
                                if (next)
                                        printf(" | ");
                                printf("QCBI_");
                                next = 1;
                        }
                        if (strchr("*/%", count)) {
                                if (next)
                                        printf(" | ");
                                printf("QCMD_");
                                next = 1;
                        }
                        if (!next)
                                printf("0");
                        printf(", ");
                        ++count;
                }
                printf("\n\n");
        }

        printf("};\n");
}

void mktokx(void)
{
        int row, col, count;
        printf("/*\n");
        printf(" * Table to map single characters into tokens.\n");
        printf(" * This must be rebuilt every time you change the\n");
        printf(" * `enum QC_TOKENS' list in qc.h\n");
        printf(" */\n");
        printf("char qc_ch2tok[] = {\n");

        count = 0;
        for (row = 0; row < (128 / 4); ++row) {
                printf("\t");
                for (col = 0; col < 4; ++col) {
                        int v = 0;
                        switch (count) {
                        case '[':
                                v = QC_OPENSQU;
                                break;
                        case ']':
                                v = QC_CLOSESQU;
                                break;
                        case '{':
                                v = QC_OPENBR;
                                break;
                        case '}':
                                v = QC_CLOSEBR;
                                break;
                        case '(':
                                v = QC_OPENPAREN;
                                break;
                        case ')':
                                v = QC_CLOSEPAREN;
                                break;
                        case '=':
                                v = QC_EQEQ;
                                break;
                        case ';':
                                v = QC_SEMI;
                                break;
                        case ',':
                                v = QC_COMMA;
                                break;
                        case ':':
                                v = QC_COLON;
                                break;
                        case '*':
                                v = QC_MULTOK;
                                break;
                        case '/':
                                v = QC_DIVTOK;
                                break;
                        case '%':
                                v = QC_MODTOK;
                                break;
                        case '+':
                                v = QC_PLUSTOK;
                                break;
                        case '-':
                                v = QC_MINUSTOK;
                                break;
                        case '&':
                                v = QC_ANDTOK;
                                break;
                        case '|':
                                v = QC_ORTOK;
                                break;
                        case '^':
                                v = QC_XORTOK;
                                break;
                        case '!':
                                v = QC_LNOTTOK;
                                break;
                        case '~':
                                v = QC_ANOTTOK;
                                break;
                        case '<':
                                v = QC_LT;
                                break;
                        case '>':
                                v = QC_GT;
                                break;
                        default:
                                v = 0;
                                break;
                        }
                        printf("%d, ", v);
                        ++count;
                }
                printf("\n");
        }
        printf("};\n");
}

void mktokm(void)
{
        int row, col, count;
        printf("/*\n");
        printf(" * Table to map tokens into token descriptors.\n");
        printf(" * This must be rebuilt every time you change the\n");
        printf(" * `enum QC_TOKENS' list in qc.h\n");
        printf(" */\n");
        printf("char qc_tokmap[] = {\n");

        count = 0;
        for (row = 0; row < (128 / 4); ++row) {
                printf("\t");
                for (col = 0; col < 4; ++col) {
                        int v = 0;
                        if (count >= QC_LT && count <= QC_NE)
                                v |= QC_TKC_;
                        if (count >= QC_LAND && count <= QC_LOR)
                                v |= QC_TKL_;
                        if (count >= QC_PLUSPLUS && count <= QC_EQEQ)
                                v |= QC_TKA_;
                        if (count >= QC_LSL && count <= QC_LSR)
                                v |= QC_TKS_;
                        if (count >= QC_ANDTOK && count <= QC_XORTOK)
                                v |= QC_TKB_;
                        if (count >= QC_MULTOK && count <= QC_MODTOK)
                                v |= QC_TKM_;
                        if (count == QC_MINUSTOK || count == QC_PLUSTOK
                         || count == QC_MULTOK || count == QC_LNOTTOK
                         || count == QC_ANOTTOK || count == QC_ANDTOK)
                                v |= QC_TKU_;
                        printf("%d, ", v);
                        ++count;
                }
                printf("\n");
        }
        printf("};\n");
}

int main(void)
{
        mkheader();
        mkchar();
        mktokx();
        mktokm();
        return 0;
}

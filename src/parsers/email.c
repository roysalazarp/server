#include "parsers.h"

#define IS_ASCII_LETTER(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define IS_EMAIL_TOKEN(c)  ((IS_ASCII_LETTER(c)) || ((c) == '-') || ((c) == '_') || ((c) == '+'))

#define EMAIL_START 0
#define EMAIL_USERNAME 1
#define EMAIL_MAIL_SERVER 2
#define EMAIL_DOMAIN 3

int parse_email(Email *e, char *email, uint32_t email_length) {
    uint32_t cursor = 0;
    int ch;

    int start;
    
    uint32_t t = 0;
    for (; cursor < email_length;) {
        ch = email[cursor];
        switch (t) {
            case EMAIL_START: {
                for (;;) {
                    if (IS_EMAIL_TOKEN(ch)) {
                        start = cursor;
                        t = EMAIL_USERNAME;
                        break;
                    }
                    if (ch != ' ') {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            case EMAIL_USERNAME: {
                for (;;) {
                    if (ch == '@') {
                        e->username.start = start;
                        e->username.end = cursor - 1;
                        
                        if (!IS_ASCII_LETTER(email[e->username.start]) || !IS_ASCII_LETTER(email[e->username.end])) {
                            return -1;
                        }

                        cursor += 1; /* skip @ */
                        start = cursor;
                        t = EMAIL_MAIL_SERVER;
                        break;
                    }
                    if (!IS_EMAIL_TOKEN(ch)) {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            case EMAIL_MAIL_SERVER: {
                for (;;) {
                    if (ch == '.') {
                        e->mail_server.start = start;
                        e->mail_server.end = cursor - 1;

                        if (!IS_ASCII_LETTER(email[e->mail_server.start]) || !IS_ASCII_LETTER(email[e->mail_server.end])) {
                            return -1;
                        }
                        
                        cursor += 1; /* skip . */
                        start = cursor;
                        t = EMAIL_DOMAIN;
                        break;
                    }
                    if (!IS_EMAIL_TOKEN(ch)) {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            case EMAIL_DOMAIN: {
                for (;;) {
                    if (!IS_ASCII_LETTER(ch)) {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        e->domain.start = start;
                        e->domain.end = cursor - 1;
                        
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            default: {
                break;
            }
        }
    }

    return 0;
}
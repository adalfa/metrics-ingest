#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hiredis.h"

/* FIX: resp array was size 3, risking overflow if input has more than 3 tokens.
   Increased to 16 to safely accommodate extra tokens. */
#define MAX_TOKENS 16

int main(int argc, char **argv)
{
    /* FIX: hardcoded IP/port replaced with argv so the binary is configurable.
       Falls back to original defaults if no args are provided. */
    const char *host = (argc > 1) ? argv[1] : "172.20.1.23";
    int port = (argc > 2) ? atoi(argv[2]) : 6379;

    redisContext *c;
    redisReply *reply;

    char input[512];
    char *resp[MAX_TOKENS];
    char *delim = "|";
    const char *t = ".time";
    int i;

    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    c = redisConnectWithTimeout(host, port, timeout);
    /* FIX: added NULL check on c before dereferencing — redisConnectWithTimeout
       can return NULL on allocation failure. */
    if (c == NULL || c->err) {
        printf("Connection error: %s\n", c ? c->errstr : "allocation failure");
        exit(1);
    }

    do {
        i = 0;

        /* FIX: added field width limit %511s to prevent buffer overflow on input[512]. */
        if (scanf("%511s", input) == EOF) exit(0);
        printf("Input:%s\n", input);

        resp[i] = strtok(input, delim);
        /* FIX: added bounds check (i < MAX_TOKENS - 1) to prevent resp[] overflow. */
        while (resp[i] != NULL && i < MAX_TOKENS - 1) {
            printf("Token:%s\n", resp[i]);
            resp[++i] = strtok(NULL, delim);
        }

        /* FIX: validate that we have exactly 3 tokens before using resp[0..2]. */
        if (i < 3 || resp[0] == NULL || resp[1] == NULL || resp[2] == NULL) {
            fprintf(stderr, "Invalid input: expected key|timestamp|value\n");
            continue;
        }

        /* FIX: added NULL checks on reply before calling freeReplyObject,
           since redisCommand can return NULL on connection error. */
        reply = redisCommand(c, "LPUSH %s %s", resp[0], resp[2]);
        if (reply) freeReplyObject(reply);
        reply = redisCommand(c, "LTRIM %s 0 %d", resp[0], 999);
        if (reply) freeReplyObject(reply);
        printf("time:%s\n", resp[1]);

        /* FIX: double-free removed. strcat() returns its first argument (buff),
           so timelist == buff — freeing both was undefined behaviour.
           Replaced strcpy+strcat with snprintf into a single buffer. */
        size_t timelen = strlen(resp[0]) + strlen(t) + 1;
        char *timelist = malloc(timelen);
        if (!timelist) { perror("malloc"); exit(1); }
        snprintf(timelist, timelen, "%s%s", resp[0], t);

        reply = redisCommand(c, "LPUSH %s %s", timelist, resp[1]);
        if (reply) freeReplyObject(reply);
        reply = redisCommand(c, "LTRIM %s 0 %d", timelist, 999);
        if (reply) freeReplyObject(reply);
        printf("%s\n", timelist);

        free(timelist); /* single free — buff is gone, only one allocation now */
    } while (1);

    redisFree(c);
    return 0;
}

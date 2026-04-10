#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "hiredis.h"

/* FIX: resp array was size 3, risking overflow if input has more than 3 tokens.
   Increased to 16 to safely accommodate extra tokens. */
#define MAX_TOKENS 16

/* Leveled logging: write to stderr with a severity prefix.
   Set log_level to LOG_DEBUG to see token-trace output; default is LOG_INFO. */
#define LOG_DEBUG 0
#define LOG_INFO  1
#define LOG_WARN  2
#define LOG_ERROR 3

static int log_level = LOG_INFO;
static const char *level_str[] = { "DEBUG", "INFO", "WARN", "ERROR" };

#define LOG(lvl, fmt, ...) \
    do { if ((lvl) >= log_level) \
        fprintf(stderr, "[%s] " fmt "\n", level_str[lvl], ##__VA_ARGS__); \
    } while (0)

/* Reconnect: attempt to (re)connect to Redis with exponential back-off.
   Returns a valid context or exits after MAX_RETRIES failures. */
#define MAX_RETRIES 5

static redisContext *redis_connect(const char *host, int port)
{
    struct timeval timeout = { 1, 500000 }; /* 1.5 seconds */
    int delay = 1;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        redisContext *c = redisConnectWithTimeout(host, port, timeout);
        /* FIX: added NULL check on c before dereferencing — redisConnectWithTimeout
           can return NULL on allocation failure. */
        if (c != NULL && !c->err) {
            if (attempt > 1)
                LOG(LOG_INFO, "Reconnected to %s:%d", host, port);
            return c;
        }
        LOG(LOG_WARN, "Connection attempt %d/%d failed: %s — retrying in %ds",
            attempt, MAX_RETRIES, c ? c->errstr : "allocation failure", delay);
        if (c) redisFree(c);
        sleep(delay);
        delay *= 2; /* exponential back-off: 1s, 2s, 4s, 8s, 16s */
    }
    LOG(LOG_ERROR, "Could not connect to Redis at %s:%d after %d attempts",
        host, port, MAX_RETRIES);
    exit(1);
}

/* Execute a Redis command; on connection error, reconnect and retry once. */
static redisReply *redis_cmd(redisContext **c, const char *host, int port,
                             const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    redisReply *reply = redisvCommand(*c, fmt, ap);
    va_end(ap);

    if (reply == NULL || (*c)->err) {
        LOG(LOG_WARN, "Redis error: %s — attempting reconnect", (*c)->errstr);
        redisFree(*c);
        *c = redis_connect(host, port);
        /* Retry the command once after reconnect */
        va_start(ap, fmt);
        reply = redisvCommand(*c, fmt, ap);
        va_end(ap);
    }
    return reply;
}

int main(int argc, char **argv)
{
    /* FIX: hardcoded IP/port replaced with argv so the binary is configurable.
       Falls back to original defaults if no args are provided. */
    /* Usage: perfparse [-v] [host [port]]
       -v enables DEBUG logging; host and port override the defaults. */
    const char *host = "172.20.1.23";
    int port = 6379;
    int i;
    int positional = 0; /* tracks how many non-flag args have been seen */

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            log_level = LOG_DEBUG;
        } else if (positional == 0) {
            host = argv[i];
            positional++;
        } else if (positional == 1) {
            port = atoi(argv[i]);
            positional++;
        }
    }

    redisContext *c = redis_connect(host, port);
    LOG(LOG_INFO, "Connected to Redis at %s:%d", host, port);

    char input[512];
    char *resp[MAX_TOKENS];
    char *delim = "|";
    const char *t = ".time";

    do {
        /* FIX: added field width limit %511s to prevent buffer overflow on input[512]. */
        if (scanf("%511s", input) == EOF) break;
        LOG(LOG_DEBUG, "Input: %s", input);

        i = 0;
        resp[i] = strtok(input, delim);
        /* FIX: added bounds check (i < MAX_TOKENS - 1) to prevent resp[] overflow. */
        while (resp[i] != NULL && i < MAX_TOKENS - 1) {
            LOG(LOG_DEBUG, "Token[%d]: %s", i, resp[i]);
            resp[++i] = strtok(NULL, delim);
        }

        /* FIX: validate that we have at least 3 tokens before using resp[0..2]. */
        if (i < 3 || resp[0] == NULL || resp[1] == NULL || resp[2] == NULL) {
            LOG(LOG_WARN, "Invalid input (got %d tokens, expected key|timestamp|value): %s", i, input);
            continue;
        }

        /* Push value and trim list to last 1000 entries */
        redisReply *reply;
        reply = redis_cmd(&c, host, port, "LPUSH %s %s", resp[0], resp[2]);
        if (reply) freeReplyObject(reply);
        reply = redis_cmd(&c, host, port, "LTRIM %s 0 %d", resp[0], 999);
        if (reply) freeReplyObject(reply);

        /* FIX: double-free removed. strcat() returns its first argument (buff),
           so timelist == buff — freeing both was undefined behaviour.
           Replaced strcpy+strcat with snprintf into a single buffer. */
        size_t timelen = strlen(resp[0]) + strlen(t) + 1;
        char *timelist = malloc(timelen);
        if (!timelist) { perror("malloc"); exit(1); }
        snprintf(timelist, timelen, "%s%s", resp[0], t);

        /* Push timestamp and trim its list to last 1000 entries */
        reply = redis_cmd(&c, host, port, "LPUSH %s %s", timelist, resp[1]);
        if (reply) freeReplyObject(reply);
        reply = redis_cmd(&c, host, port, "LTRIM %s 0 %d", timelist, 999);
        if (reply) freeReplyObject(reply);

        LOG(LOG_DEBUG, "Stored: key=%s time_key=%s", resp[0], timelist);
        free(timelist); /* single free — buff is gone, only one allocation now */
    } while (1);

    LOG(LOG_INFO, "EOF — shutting down");
    redisFree(c);
    return 0;
}

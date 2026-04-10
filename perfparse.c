#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hiredis.h"
int  main (int argc,char **argv)
{
redisContext *c;
    redisReply *reply;

char input[512];
char* resp[3];
char *delim="|";
char *t=".time";
int i;
 struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    c = redisConnectWithTimeout((char*)"172.20.1.23", 6379, timeout);
    if (c->err) {
        printf("Connection error: %s\n", c->errstr);
        exit(1);
    }
do {
 i=0;

if (scanf("%s\n",input)==EOF) exit(1);
printf("Input:%s\n",input);
resp[i]=strtok(input,delim);
while (resp[i]!=NULL)
        {
      printf("Token:%s\n",resp[i]);
        resp[++i]=strtok(NULL,delim);
        }

reply =  redisCommand(c,"LPUSH %s %s", resp[0], resp[2]);
    freeReplyObject(reply);
reply =  redisCommand(c,"LTRIM %s 0 %d", resp[0], 999);
    freeReplyObject(reply);
printf("time:%s\n",resp[1]);

char *buff=malloc(sizeof(char)*(strlen(resp[0])+strlen(t)+1));
strcpy(buff,resp[0]);
char *timelist=strcat(buff,t);
printf("time1-%s-%s\n",timelist,resp[1]);
reply =  redisCommand(c,"LPUSH %s %s", timelist,resp[1]);
//reply =  redisCommand(c,"LPUSH aa bb");
   freeReplyObject(reply);
printf("time2\n");
//printf("%s\n",timelist);
reply=  redisCommand(c,"LTRIM %s 0 %d", timelist, 999);
   freeReplyObject(reply);
printf("%s\n",timelist);
free(timelist);
free(buff);
} while (1);
    redisFree(c);    
}

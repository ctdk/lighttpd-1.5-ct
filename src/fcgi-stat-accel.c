/*
  compile with: 

  $ gcc -lfcgi -lpthread fcgi-stat-accel.c -o fcgi-stat-accel

  fcgi-stat-accel will use the PHP_FCGI_CHILDREN environment variable to set the thread count.

  The default value, if spawned from lighttpd, is 20.
*/


#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <stdio.h>

#include <fastcgi/fcgiapp.h>

#define THREAD_COUNT 20

#define FORBIDDEN(stream) \
        FCGX_FPrintF(stream, "Status: 403 Forbidden\r\nContent-Type: text/html\r\n\r\n<h1>403 Forbidden</h1>\n");
#define NOTFOUND(stream, filename) \
        FCGX_FPrintF(stream, "Status: 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>404 Not Found</h1>\r\n%s", filename);
#define SENDFILE(stream, filename) \
        FCGX_FPrintF(stream, "X-LIGHTTPD-send-file: %s\r\n\r\n", filename); 


static void *doit(void *a){
        FCGX_Request request;
        int rc;
        char *filename;
        FILE *fd;

        FCGX_InitRequest(&request, 0, FCGI_FAIL_ACCEPT_ON_INTR);

        while(1){
                //Some platforms require accept() serialization, some don't. The documentation claims it to be thread safe
//              static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
//              pthread_mutex_lock(&accept_mutex);
                rc = FCGX_Accept_r(&request);
//              pthread_mutex_unlock(&accept_mutex);

                if(rc < 0)
                        break;

        //get the filename
                if((filename = FCGX_GetParam("SCRIPT_FILENAME", request.envp)) == NULL){
                        FORBIDDEN(request.out);
        //don't try to open directories
                }else if(filename[strlen(filename)-1] == '/'){
                        FORBIDDEN(request.out);
        //open the file
                }else if((fd = fopen(filename, "r")) == NULL){
                        NOTFOUND(request.out, filename);
        //no error, serve it
                }else{
                        SENDFILE(request.out, filename);

                        fclose(fd);
                }

                FCGX_Finish_r(&request);
        }
        return NULL;
}

int main(void){
        int i,j,thread_count;
        pthread_t* id;
        char* env_val;

        FCGX_Init();

        thread_count = THREAD_COUNT;
        env_val = getenv("PHP_FCGI_CHILDREN");
        if (env_val != NULL) {
                j = atoi(env_val);
                if (j != 0) {
                        thread_count = j;
                };
        };

        id = malloc(sizeof(*id) * thread_count);

        for (i = 0; i < thread_count; i++) {
                pthread_create(&id[i], NULL, doit, NULL);
        }

        doit(NULL);
        free(id);
        return 0;
}


#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
typedef HANDLE Thread;
#define WORKER_RESULT DWORD WINAPI
#define WORKER_ARGUMENT LPVOID
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t Thread;
#define WORKER_RESULT void *
#define WORKER_ARGUMENT void *
#endif
int read_value(void);
int initialization_count(void);
int destruction_count(void);
static void verify_exit(void) { if (destruction_count() != 2) _Exit(91); }
static _Atomic(unsigned int) ready;
static int results[16];
static WORKER_RESULT worker(WORKER_ARGUMENT argument){
 int *result=argument;
 while(!__c11_atomic_load(&ready,__ATOMIC_ACQUIRE)){}
 *result=0;
 for(int i=0;i<2000;++i)*result|=read_value();
 return 0;
}
int main(void){
 Thread threads[16];int count=0;
 if(atexit(verify_exit))return 24;
#ifndef _WIN32
 alarm(30);
#endif
 if(initialization_count())return 23;
 for(;count<16;++count){
#ifdef _WIN32
  threads[count]=CreateThread(0,0,worker,&results[count],0,0);
  if(!threads[count])break;
#else
  if(pthread_create(&threads[count],0,worker,&results[count]))break;
#endif
 }
 __c11_atomic_store(&ready,1u,__ATOMIC_RELEASE);
 for(int i=0;i<count;++i){
#ifdef _WIN32
  if(WaitForSingleObject(threads[i],30000)!=WAIT_OBJECT_0)return 20;
  CloseHandle(threads[i]);
#else
  if(pthread_join(threads[i],0))return 20;
#endif
 }
 if(count!=16)return 21;
 for(int i=0;i<16;++i)if(results[i])return 22;
 return initialization_count()!=2 || destruction_count()!=0;
}

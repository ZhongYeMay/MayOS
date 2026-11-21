#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
/* Linux/POSIX persistence backend. Never replace an existing save until
 * the new file has been fully written and synchronized. */
int summer_save_file(const SummerGame *game,const char *filename){
 if(!game||!filename||!*filename)return -1;
 SummerSave *state=malloc(sizeof(*state));if(!state)return -2;
 if(summer_save(game,state)){free(state);return -3;}
 size_t len=strlen(filename);
 if(len>4000){free(state);return -1;}
 char *temp=malloc(len+32);
 if(!temp){free(state);return -2;}
 sprintf(temp,"%s.tmp.XXXXXX",filename);
 int fd=mkstemp(temp);
 if(fd<0){free(state);free(temp);return -4;}
 size_t written=0;
 while(written<sizeof(*state)){
  ssize_t n=write(fd,(const char*)state+written,sizeof(*state)-written);
  if(n<=0)break;
  written+=(size_t)n;
 }
 int ok=written==sizeof(*state)&&fsync(fd)==0;
 if(close(fd)!=0)ok=0;
 if(ok&&rename(temp,filename)!=0)ok=0;
 if(!ok)unlink(temp);
 free(state);free(temp);
 return ok?0:-5;
}
int summer_load_file(SummerGame *game,const char *filename){
 if(!game||!filename)return -1;
 FILE *f=fopen(filename,"rb");if(!f)return -2;
 SummerSave *state=malloc(sizeof(*state));if(!state){fclose(f);return -3;}
 int ok=fread(state,1,sizeof(*state),f)==sizeof(*state);
 if(fgetc(f)!=EOF)ok=0;
 fclose(f);
 int result=ok?summer_load(game,state):-4;
 free(state);
 return result;
}

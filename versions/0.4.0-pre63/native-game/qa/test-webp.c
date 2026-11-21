#include "webp.h"
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 assert(argc==2);
 const char *names[]={"studio","coast","bookshop","mio-0","mio-1","mio-2",
 "nagi-0","nagi-1","nagi-2","shiori-0","shiori-1","shiori-2"};
 for(unsigned i=0;i<12;i++){
  char path[1024];
  int n=snprintf(path,sizeof(path),"%s/assets/retro/%s.webp",argv[1],names[i]);
  assert(n>0&&(size_t)n<sizeof(path));
  SummerImage image;
  assert(summer_image_load_webp(path,&image)==0);
  assert(image.w>0&&image.h>0&&image.stride==image.w);
  assert(image.pixels!=0);
  printf("Decoded original: %s %dx%d\n",names[i],image.w,image.h);
  summer_image_free(&image);
  assert(image.pixels==0);
 }
 puts("PASS all 12 original WebP assets decoded without substitution");
 return 0;
}

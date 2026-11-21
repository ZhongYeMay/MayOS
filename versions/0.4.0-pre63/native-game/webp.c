#include "webp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>
int summer_image_load_webp(const char *filename,SummerImage *out){
 if(!filename||!out)return -1;
 memset(out,0,sizeof(*out));
 FILE *f=fopen(filename,"rb");if(!f)return -2;
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return -3;}
 long size=ftell(f);
 if(size<12||size>64*1024*1024L){fclose(f);return -3;}
 rewind(f);
 unsigned char *data=malloc((size_t)size);
 if(!data){fclose(f);return -4;}
 if(fread(data,1,(size_t)size,f)!=(size_t)size){free(data);fclose(f);return -3;}
 fclose(f);
 int width=0,height=0;
 if(!WebPGetInfo(data,(size_t)size,&width,&height)||width<1||height<1||
    width>8192||height>8192){free(data);return -5;}
 if((size_t)width*(size_t)height>SIZE_MAX/sizeof(uint32_t)){free(data);return -5;}
 uint8_t *rgba=WebPDecodeRGBA(data,(size_t)size,&width,&height);
 free(data);
 if(!rgba)return -5;
 uint32_t *pixels=malloc((size_t)width*(size_t)height*sizeof(uint32_t));
 if(!pixels){WebPFree(rgba);return -4;}
 for(size_t i=0;i<(size_t)width*(size_t)height;i++){
  const uint8_t *p=rgba+i*4;
  pixels[i]=((uint32_t)p[3]<<24)|((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
 }
 WebPFree(rgba);
 out->x=0;out->y=0;out->w=width;out->h=height;
 out->stride=width;out->pixels=pixels;
 return 0;
}
void summer_image_free(SummerImage *image){
 if(!image)return;
 free((void *)image->pixels);
 memset(image,0,sizeof(*image));
}

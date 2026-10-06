#include <stdio.h>
int qualifier_read_file(const char* path,unsigned char* destination,
                        unsigned int capacity,unsigned int* actual) {
  FILE* file=fopen(path,"rb");if(!file)return 1;
  size_t size=fread(destination,1,capacity,file);
  int extra=fgetc(file);int bad=ferror(file);fclose(file);
  if(extra!=EOF || bad)return 2;
  *actual=(unsigned int)size;return 0;
}
void qualifier_report(const char* message,unsigned int pages,unsigned int segments) {
  printf("%s image_pages=%u stack_pages=2 segments=%u\n",message,pages,segments);
}

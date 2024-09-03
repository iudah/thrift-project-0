#include <stdio.h>
#include <Thrift-Project.h>

int main(void){
  printf(
"Thrift Project CLI\n\
Version %d.%d.%d\n",THRIFT_PROJECT_VERSION_MAJOR,THRIFT_PROJECT_VERSION_MINOR,THRIFT_PROJECT_VERSION_PATCH);
  return 0;
}

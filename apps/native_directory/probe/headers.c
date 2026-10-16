#include <dirent.h>
#include <unistd.h>
_Static_assert(sizeof(void *)==4,"native C pointer");
_Static_assert(sizeof(struct dirent)==257,"native directory public layout");
_Static_assert(__builtin_offsetof(struct dirent,d_name)==1,"native directory name");
DIR *(*directory_open_type)(const char *)=opendir;
struct dirent *(*directory_read_type)(DIR *)=readdir;
int (*directory_close_type)(DIR *)=closedir;
void (*directory_rewind_type)(DIR *)=rewinddir;
int (*directory_descriptor_type)(DIR *)=dirfd;
int (*descriptor_duplicate_type)(int)=dup;
int (*descriptor_close_type)(int)=close;

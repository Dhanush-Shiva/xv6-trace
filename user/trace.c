#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;
  int mask;

  if (argc < 3) {
    fprintf(2, "Usage: trace mask command\n");
    exit(1);
  }

  mask = atoi(argv[1]);
  trace(mask);

  for (i = 2; i < argc; i++)
    argv[i - 2] = argv[i];
  argv[argc - 2] = 0;

  exec(argv[0], argv);

  fprintf(2, "trace: exec %s failed\n", argv[0]);
  exit(1);
}

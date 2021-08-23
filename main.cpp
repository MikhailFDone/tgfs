#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <iostream>

static struct fuse_operations fuse_example_operations = {};

int main(int argc, char *argv[])
{
  std::cout << "HELLO" << std::endl;
  return fuse_main(argc, argv, &fuse_example_operations, NULL);
}

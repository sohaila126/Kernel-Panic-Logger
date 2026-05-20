int
main(int argc, char *argv[])
{
  struct crash_context ctx;
  uint64 sz;

  int ret = dumppanic(&ctx, &sz);
  if (ret < 0) {
    printf("dumppanic: syscall failed\n");
    return -1;
  }

  if (sz != sizeof(ctx)) {
    printf("dumppanic: size mismatch (expected %lu, got %lu)\n",
           sizeof(ctx), sz);
    return -1;
  }

  print_ctx(&ctx);
  return 0;
}

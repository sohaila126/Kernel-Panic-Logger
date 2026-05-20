int
log_test(void)
{
  int failed = 0;

  log_info(__FILE__, __LINE__, "log_test: INFO message (pid=42)");
  log_warn(__FILE__, __LINE__, "log_test: WARN message (x=%x)", 0xDEAD);
  log_debug(__FILE__, __LINE__, "log_test: DEBUG message (str=%s)", "hello");
  log_panic_prep(__FILE__, __LINE__, "log_test: PANIC prep (num=%d)", -1);

  for (int i = 0; i < LOG_SIZE + 5; i++) {
    log_debug(__FILE__, __LINE__, "wrap test i=%d", i);
  }

  log_info(__FILE__, __LINE__, "fmt d=%d ld=%ld lld=%lld", -1, -2L, -3LL);
  log_info(__FILE__, __LINE__, "fmt u=%u lu=%lu llu=%llu", 1U, 2UL, 3ULL);
  log_info(__FILE__, __LINE__, "fmt x=%x lx=%lx llx=%llx", 0xFF, 0xFFFUL, 0xFFFFULL);
  log_info(__FILE__, __LINE__, "fmt s=\"%s\" c='%c' p=%p", "test", 'A', (void*)0x80000000);
  log_info(__FILE__, __LINE__, "fmt %%");

  log_flush();

  printf("log_test: all tests completed.\n");
  return failed;
}

void
panic(char *s)
{
  panicking = 1;

  log_save_crash_context(s);
  log_flush();

  printf("panic: ");
  printf("%s\n", s);
  log_dump_crash_context();

  panicked = 1;
  for(;;)
    ;
}

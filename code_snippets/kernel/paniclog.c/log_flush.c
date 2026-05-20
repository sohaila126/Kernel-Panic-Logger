void
log_flush(void)
{
  static int flushing = 0;
  if (flushing)
    return;
  acquire(&logbuf.lock);
  flushing = 1;

  printf("\n--- LOG FLUSH (most recent first) ---\n");
  int idx = logbuf.head;
  int printed = 0;
  for (int i = 0; i < logbuf.count; i++) {
    idx = (idx - 1 + LOG_SIZE) % LOG_SIZE;
    struct log_entry *e = &logbuf.entries[idx];
    printf("[%d][%s] %s:%d ", e->ticks, lvlstr(e->level),
           e->file, e->line);
    printf("%s\n", e->msg);
    printed++;
  }
  if (printed == 0)
    printf("(log buffer empty)\n");
  printf("--- END LOG FLUSH ---\n");

  flushing = 0;
  release(&logbuf.lock);
}

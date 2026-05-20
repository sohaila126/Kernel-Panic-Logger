void
log_flush(void)
{
  acquire(&logbuf.lock);

  printf("\n--- LOG FLUSH (most recent first) ---\n");
  int idx = logbuf.head;
  int printed = 0;
  for (int i = 0; i < logbuf.count; i++) {
    idx = (idx - 1 + LOG_SIZE) % LOG_SIZE;
    struct log_entry *e = &logbuf.entries[idx];
    printf("[%5d][%s] %s:%d ", e->ticks, lvlstr(e->level),
           e->file, e->line);
    printf("%s\n", e->msg);
    printed++;
  }
  if (printed == 0)
    printf("(log buffer empty)\n");
  printf("--- END LOG FLUSH ---\n");

  release(&logbuf.lock);
}

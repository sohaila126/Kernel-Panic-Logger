static void
log_add(int level, const char *file, int line, const char *fmt, va_list ap)
{
  acquire(&logbuf.lock);

  struct log_entry *e = &logbuf.entries[logbuf.head];

  e->ticks = ticks;
  e->level = level;

  const char *basename = file;
  for (const char *p = file; *p; p++) {
    if (*p == '/' || *p == '\\')
      basename = p + 1;
  }
  safestrcpy(e->file, (char*)basename, LOG_FILE_MAX);
  e->line = line;

  vsnprintf(e->msg, LOG_MSG_MAX, fmt, ap);

  logbuf.head = (logbuf.head + 1) % LOG_SIZE;
  if (logbuf.count < LOG_SIZE)
    logbuf.count++;

  release(&logbuf.lock);
}

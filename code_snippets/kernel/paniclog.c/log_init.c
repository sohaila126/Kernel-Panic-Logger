void
log_init(void)
{
  initlock(&logbuf.lock, "logbuf");
  logbuf.head  = 0;
  logbuf.count = 0;

  memset(&saved_crash_ctx, 0, sizeof(saved_crash_ctx));
  saved_crash_ctx.magic = 0;
}

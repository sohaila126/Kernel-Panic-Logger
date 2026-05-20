void
log_info(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_INFO, file, line, fmt, ap);
  va_end(ap);
}

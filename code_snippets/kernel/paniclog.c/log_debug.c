void
log_debug(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_DEBUG, file, line, fmt, ap);
  va_end(ap);
}

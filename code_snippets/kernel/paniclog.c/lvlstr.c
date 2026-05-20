static const char *lvlstr(int level)
{
  switch (level) {
  case LOG_INFO:  return "INFO";
  case LOG_WARN:  return "WARN";
  case LOG_PANIC: return "PANIC";
  case LOG_DEBUG: return "DEBUG";
  default:        return "????";
  }
}

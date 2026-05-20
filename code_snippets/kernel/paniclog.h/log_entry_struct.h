// struct log_entry — single circular buffer slot
struct log_entry {
  uint      ticks;
  int       level;
  char      file[LOG_FILE_MAX];
  int       line;
  char      msg[LOG_MSG_MAX];
};

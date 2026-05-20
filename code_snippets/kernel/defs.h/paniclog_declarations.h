// paniclog.c
void            log_init(void);
void            log_info(const char*, int, const char*, ...);
void            log_warn(const char*, int, const char*, ...);
void            log_debug(const char*, int, const char*, ...);
void            log_panic_prep(const char*, int, const char*, ...);
void            log_flush(void);
void            log_save_crash_context(const char*);
void            log_dump_crash_context(void);
int             log_test(void);

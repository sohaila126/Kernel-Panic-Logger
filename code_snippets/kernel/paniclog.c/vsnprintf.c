static int
vsnprintf(char *buf, int n, const char *fmt, va_list ap)
{
  char *dst = buf;
  char *end = buf + n - 1;
  int    i, cx, c0, c1, c2;
  char  *s;
  unsigned long long x;
  char   tmp[24];
  int    tmplen;
  char   digits[] = "0123456789abcdef";

#define PUTC(ch) do { if (dst < end) *dst++ = (ch); } while(0)

  for (i = 0; (cx = fmt[i] & 0xff) != 0; i++) {
    if (cx != '%') { PUTC(cx); continue; }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if (c0) c1 = fmt[i+1] & 0xff;
    if (c1) c2 = fmt[i+2] & 0xff;

    if (c0 == 'd') {
      x = (long long)va_arg(ap, int);
      if ((long long)x < 0) { PUTC('-'); x = -(long long)x; }
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
    } else if (c0 == 'l' && c1 == 'd') {
      x = va_arg(ap, long long);
      if ((long long)x < 0) { PUTC('-'); x = -(long long)x; }
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
      x = va_arg(ap, long long);
      if ((long long)x < 0) { PUTC('-'); x = -(long long)x; }
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 2;
    } else if (c0 == 'u') {
      x = va_arg(ap, uint32);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
    } else if (c0 == 'l' && c1 == 'u') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 2;
    } else if (c0 == 'x') {
      x = va_arg(ap, uint32);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 16]; } while ((x /= 16) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
    } else if (c0 == 'l' && c1 == 'x') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 16]; } while ((x /= 16) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 16]; } while ((x /= 16) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 2;
    } else if (c0 == 'p') {
      x = va_arg(ap, uint64);
      PUTC('0'); PUTC('x');
      for (int j = 0; j < 16; j++, x <<= 4)
        PUTC(digits[x >> 60]);
    } else if (c0 == 's') {
      s = va_arg(ap, char *);
      if (s == 0) s = "(null)";
      while (*s) PUTC(*s++);
    } else if (c0 == 'c') {
      PUTC(va_arg(ap, int));
    } else if (c0 == '%') {
      PUTC('%');
    } else if (c0 == 0) {
      break;
    } else {
      PUTC('%'); PUTC(c0);
    }
  }

#undef PUTC

  *dst = '\0';
  return dst - buf;
}

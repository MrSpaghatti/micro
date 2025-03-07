/*** includes ***/
#include <cstdarg>
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

// ctype.h is a header file that defines functions to classify characters by their types
#include <ctype.h>
// errno.h is a header file that defines macros for reporting and retrieving error conditions
#include <errno.h>
// fcntl.h is a header file that defines file control options
#include <fcntl.h>
// stdio.h is a header file that defines the standard input/output library
#include <stdio.h>
// stdarg.h is a header file that defines macros to access the individual arguments of a variable argument list
#include <stdarg.h>
// stdlib.h is a header file that defines several general purpose functions
#include <stdlib.h>
// string.h is a header file that defines several functions to manipulate C strings and arrays
#include <string.h>
// unistd.h is a header file that provides access to the POSIX operating system API
#include <sys/ioctl.h>
// sys/types.h is a header file that defines several data types used in system calls
#include <sys/types.h>
// termios.h is a header file that defines the terminal I/O interface
#include <termios.h>
// time.h is a header file that defines several functions to manipulate date and time information
#include <time.h>
// fcntl.h is a header file that defines file control options
#include <unistd.h>

/*** defines ***/
#define MICRO_VERSION "0.0.1"
#define MICRO_TAB_STOP 8
#define MICRO_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

/*** data ***/
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt);

/*** terminal ***/
void die(const char *s) {
  // 2J command clears the screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // H command moves the cursor to the top left of the screen
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  // tcsetattr sets the attributes of the terminal
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  // tcgetattr gets the current attributes of the terminal
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcgetattr");
  }
  // atexit() registers a function to be called when the program exits
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  
  /*
  BRKINT = breaks input
  INPCK = parity check
  ISTRIP = strips 8th bit of each input byte
  ICRNL = translates carriage return to newline
  IXON = software flow control
  */
  raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL |IXON);
  //OPOST = output processing
  raw.c_oflag &= ~(OPOST);
  //CS8 = sets character size to 8 bits
  raw.c_cflag |= (CS8);
  /*
  ECHO = echo input characters
  ICANON = turns off canonincal mode (reading byte by byte instead of line by line)
  ISIG = signals
  IEXTEN = extended input processing
  */
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  // VMIN = minimum number of bytes of input needed before read() can return
  raw.c_cc[VMIN] = 0;
  // VTIME = maximum amount of time to wait before read() returns
  raw.c_cc[VTIME] = 1;

  // tcsetattr sets the attributes of the terminal
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read")
    }
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return '\x1b';
    }

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) {
          return '\x1b';
        }
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1':
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
            return PAGE_UP;
            case '6':
            return PAGE_DOWN;
            case '7':
            return HOME_KEY;
            case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A':
            return ARROW_UP;
          case 'B':
            return ARROW_DOWN;
          case 'C':
            return ARROW_RIGHT;
          case 'D':
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
      }
    }
    
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
    i++;
  }
  buf[i] = '\0';

  
  if (buf[0] != '\x1b' || buf[1] != '[') {
    return -1;
  }
  
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
    return -1;
  }

  return 0;
}

int getWindowsSize(int *rows, int *cols) {
  struct winsize ws;
  // ioctl() gets the size of the terminal
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      return getCursorPosition(rows, cols);
    }
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') {
      rx += (MICRO_TAB_STOP - 1) - (rx % MICRO_TAB_STOP);
    }
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t') {
      cur_rx += (MICRO_TAB_STOP - 1) - (cur_rx % MICRO_TAB_STOP);
    }
    cur_rx++;
    if (cur_rx > rx) {
      return cx;
    }
  }
  return cx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      tabs++;
    }
  }

  free(row->render);
  row->render = malloc(row->size + tabs*7 + 1);
  
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % 8 != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) {
    return;
  }

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) {
    return;
  }
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  // if at is less than 0 or greater than the size of the row, set at to the size of the row
  if (at < 0 || at > row->size) {
    at = row->size;
  }
  // row->chars is reallocated to be one byte larger
  row->chars = realloc(row->chars, row->size + 2);
  // memmove() copies the characters from the end of the row to the character at the given index
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  // the size of the row is incremented
  row->size++;
  // the character at the given index is set to the given character
  row->chars[at] = c;
  // the row is updated
  editorUpdateRow(row);
  // the editor is marked as dirty
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  // row->chars is reallocated to be len bytes larger
  row->chars = realloc(row->chars, row->size + len + 1);
  // memmove() copies the characters from the end of the row to the end of the given string
  memcpy(&row->chars[row->size], s, len);
  // the size of the row is incremented by the length of the given string
  row->size += len;
  // the row is updated
  editorUpdateRow(row);
  // the editor is marked as dirty
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) {
    return;
  }
  // memmove() copies the characters from the character at the given index to the end of the row
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  // the size of the row is decremented
  row->size--;
  // the row is updated
  editorUpdateRow(row);
  // the editor is marked as dirty
  E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewLine() {
  // if the cursor is at the beginning of the line, insert a new row at the current position
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    // otherwise, split the current row at the cursor position
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar() {
  if (E.cy == E.numrows) {
    return;
  }
  if (E.cx == 0 && E.cy == 0) {
    return;
  }

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  // strdup() duplicates a string
  E.filename = strdup(filename);
  // FILE is a type that represents a file stream
  FILE *fp = fopen(filename, "r");
  // fopen() opens a file
  if (!fp) {
    die("fopen");
  }
  char *line = NULL;
  // ssize_t is a signed integer type that can represent the size of an object
  size_t linecap = 0;
  ssize_t linelen;
  // getline() reads a line from a file
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}


void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)");
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);
  // open() opens a file
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    // ftruncate() truncates a file to a specified length
    if (ftruncate(fd, len) != -1) {
      // write() writes to a file
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/
void editorFind() {
  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)");
  if (query == NULL) {
    editorSetStatusMessage("Search cancelled");
    return;
  }

  for (int i = 0; i < E.numrows; i++) {
    char *match = strstr(E.row[i].render, query);
    if (match) {
      E.cy = i;
      E.cx = editorRowCxToRx(&E.row[i], match - E.row[i].render);
      E.rowoff = E.numrows;

      break;
    }
  }
  free(query);
}

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) {
    return;
  }

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) {
          welcomelen = E.screencols;
        }
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) {
          abAppend(ab, " ", 1);
        }
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) {
        len = 0;
      }
      if (len > E.screencols) { 
        len = E.screencols;
      }
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if (len > E.screencols) {
    len = E.screencols;
  }
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) {
    msglen = E.screencols;
  }
  if (msglen && time(NULL) - E.statusmsg_time < 5) {
    abAppend(ab, E.statusmsg, msglen);
  }
}

void editorRefreshScreen() {
  editorScroll();
  // abuf is a buffer that stores the text to be written to the terminal
  struct abuf ab = ABUF_INIT;

  // ?25l command hides the cursor
  abAppend(&ab, "\x1b[?25l", 6);
  // H command moves the cursor to the top left of the screen
  abAppend(&ab, "\x1b[H", 3);

  // draw rows
  editorDrawRows(&ab);
  // draw status bar
  editorDrawStatusBar(&ab);
  // draw message bar
  editorDrawMessageBar(&ab);

  char buf[32];
  // snprintf() writes formatted output to a string
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  // append the cursor position to the buffer
  abAppend(&ab, buf, strlen(buf));

  // H command moves the cursor to the top left of the screen
  abAppend(&ab, "\x1b[H", 3);
  // ?25h command shows the cursor
  abAppend(&ab, "\x1b[?25h", 6);

  // write() writes to the terminal
  write(STDOUT_FILENO, ab.b, ab.len);
  // abFree() frees the memory allocated by abAppend()
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  // vsnprintf() writes formatted output to a string
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void help() {
  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-L = clear screen");
}

/*** input ***/
char *editorPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) {
        buf[--buflen] = '\0';
      }
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;

    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
    
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
  }
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times = MICRO_QUIT_TIMES;

  // editorReadKey() reads a keypress
  int c = editorReadKey();

  switch (c) {
    case '\r':
      editorInsertNewLine();
      break;
    // CTRL_KEY() masks the key with 00011111
    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      // 2J command clears the screen
      write(STDOUT_FILENO, "\x1b[2J", 4);
      // H command moves the cursor to the top left of the screen
      write(STDOUT_FILENO, "\x1b[H", 3);
      
      exit(0);
      break;

      // CTRL_KEY('s') saves the file
      case CTRL_KEY('s'):
        editorSave();
        break;

      // HOME_KEY and END_KEY move the cursor to the beginning and end of the line
      case HOME_KEY:
        E.cx = 0;
        break;
      case END_KEY:
        if (E.cy < E.numrows) {
          E.cx = E.row[E.cy].size;
        }
        break;
      
      // BACKSPACE deletes the character to the left of the cursor
      case BACKSPACE:
      // CTRL_KEY('h') is the same as BACKSPACE
      case CTRL_KEY('h'):
      // CTRL_KEY('h') and DEL_KEY delete the character at the cursor
      case DEL_KEY:
        if (c == DEL_KEY) {
          editorMoveCursor(ARROW_RIGHT)
        }
        editorDelChar();
        break;
      // PAGE_UP and PAGE_DOWN move the cursor up and down by a page
      case PAGE_UP:
      case PAGE_DOWN:
        {
          if (c == PAGE_UP) {
            E.cy = E.rowoff;
          } else if (c == PAGE_DOWN) {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows) {
              E.cy = E.numrows;
            }
          }

          int times = E.screenrows;
          while (times--) {
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
          }
        }
        break;

      // ARROW_UP, ARROW_LEFT, ARROW_DOWN, and ARROW_RIGHT move the cursor up, left, down, and right
      case ARROW_UP:
      case ARROW_LEFT:
      case ARROW_DOWN:
      case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

      // CTRL_KEY('l') and '\x1b' do nothing
      case CTRL_KEY('l'):
      case '\x1b':
        break;

      default:
        editorInsertChar(c);
        break;
  }
  // reset quit_times to MICRO_QUIT_TIMES
  quit_times = MICRO_QUIT_TIMES;
}

/*** init ***/
void initEditor() {
  // E.cx and E.cy are the cursor position
  E.cx = 0;
  E.cy = 0;
  // E.rx is the render index
  E.rx = 0;
  // E.rowoff is the row offset
  E.rowoff = 0;
  // E.coloff is the column offset
  E.coloff = 0;
  // E.numrows is the number of rows
  E.numrows = 0;
  // E.row is the row
  E.row = NULL;
  // E.dirty is the dirty flag
  E.dirty = 0;
  // E.filename is the filename
  E.filename = NULL;
  // E.statusmsg is the status message
  E.statusmsg[0] = '\0';
  // E.statusmsg_time is the status message time
  E.statusmsg_time = 0;

  // getWindowsSize() gets the size of the terminal
  if (getWindowsSize(&E.screenrows, &E.screencols) == -1) {
    // die() prints an error message and exits the program
    die("getWindowSize");
  }
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  help();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}

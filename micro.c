/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  // tcsetattr sets the attributes of the terminal
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  // tcgetattr gets the current attributes of the terminal
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    die("tcgetattr");
  }
  // atexit() registers a function to be called when the program exits
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  
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

/*** init ***/

int main() {
  enableRawMode();
  
  while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
      die("read");
    }
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') break;
  }
  return 0;
}

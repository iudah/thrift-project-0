#include <stdio.h>
#ifdef _WIN32
#include <conio.h>
#include <stdio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#define _getch getchar
#endif

void get_password(char *password, int max_length) {
#ifndef _WIN32
  struct termios oldt, newt;
#endif

  int i = 0;
  char ch;

#ifndef _WIN32
  // Turn off echo
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
#endif

  printf("Enter password: ");

  while ((ch = _getch()) != '\r' && ch != '\n' && i < max_length - 1) {
    // Handle backspace
    if (ch == '\b' && i > 0) {
      printf("\b \b");
      i--;
    } else if (ch != '\b') {
      password[i++] = ch;
      printf("*");
    }
  }
  password[i] = '\0';
  printf("\n");

#ifndef _WIN32
  // Restore echo
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
}

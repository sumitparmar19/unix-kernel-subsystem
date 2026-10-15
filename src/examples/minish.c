/* minish: a minimal shell with pipelines (kernel-v2, ADR-005).

   Runs `prog1 args | prog2 args | ...' by connecting each stage's
   standard output to the next stage's standard input with pipe()
   and exec_redirect().

   Usage:  minish                  read commands from standard input
           minish CMD...           run one command line and exit
                                   (e.g. minish echo hi | upper | wc) */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define MAX_STAGES 8
#define LINE_MAX 256

/* Trims leading and trailing spaces from S in place. */
static char *
trim (char *s)
{
  char *end;

  while (*s == ' ')
    s++;
  end = s + strlen (s);
  while (end > s && end[-1] == ' ')
    *--end = '\0';
  return s;
}

/* Runs one command line, which may contain `|'.  Returns the exit
   status of the last stage, or -1 if the pipeline could not start. */
static int
run (char *line)
{
  char *stages[MAX_STAGES];
  pid_t pids[MAX_STAGES];
  int n = 0, i, in = -1, status = -1;
  char *save, *tok;

  for (tok = strtok_r (line, "|", &save); tok != NULL;
       tok = strtok_r (NULL, "|", &save))
    {
      if (n == MAX_STAGES)
        {
          printf ("minish: too many stages (max %d)\n", MAX_STAGES);
          return -1;
        }
      stages[n++] = trim (tok);
    }

  for (i = 0; i < n; i++)
    {
      int fds[2] = { -1, -1 };
      bool last = i == n - 1;

      if (!last && !pipe (fds))
        {
          printf ("minish: pipe failed\n");
          break;
        }
      pids[i] = exec_redirect (stages[i], in, last ? -1 : fds[1]);
      /* The children hold their own references; drop ours so the
         next stage sees end of file when the writer exits. */
      if (in != -1)
        close (in);
      if (!last)
        close (fds[1]);
      in = fds[0];
      if (pids[i] == PID_ERROR)
        printf ("minish: cannot run \"%s\"\n", stages[i]);
    }
  if (in != -1)
    close (in);

  for (i = 0; i < n; i++)
    if (pids[i] != PID_ERROR)
      status = wait (pids[i]);
  return status;
}

/* Reads a line from standard input into LINE, echoing it.  Returns
   false at end of input. */
static bool
read_line (char *line, size_t size)
{
  size_t len = 0;
  char c;

  while (read (STDIN_FILENO, &c, 1) == 1)
    {
      if (c == '\r' || c == '\n')
        {
          putchar ('\n');
          line[len] = '\0';
          return true;
        }
      if (c == '\b' || c == 0x7f)
        {
          if (len > 0)
            {
              len--;
              printf ("\b \b");
            }
        }
      else if (len + 1 < size)
        {
          line[len++] = c;
          putchar (c);
        }
    }
  return false;
}

int
main (int argc, char *argv[])
{
  char line[LINE_MAX];

  if (argc > 1)
    {
      /* One command line given as arguments. */
      int i;
      line[0] = '\0';
      for (i = 1; i < argc; i++)
        {
          if (i > 1)
            strlcat (line, " ", sizeof line);
          strlcat (line, argv[i], sizeof line);
        }
      return run (line);
    }

  printf ("minish: type commands, `a | b | c' for pipelines, `exit' to quit\n");
  for (;;)
    {
      char *cmd;
      int status;

      printf ("$ ");
      if (!read_line (line, sizeof line))
        break;
      cmd = trim (line);
      if (!strcmp (cmd, "exit"))
        break;
      if (*cmd == '\0')
        continue;
      status = run (cmd);
      printf ("[exit %d]\n", status);
    }
  return 0;
}

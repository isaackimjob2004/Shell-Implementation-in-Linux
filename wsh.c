// Author:  Vojtech Aschenbrenner <asch@cs.wisc.edu>, Fall 2023
// Revised: John Shawger <shawgerj@cs.wisc.edu>, Spring 2024
// Revised: Vojtech Aschenbrenner <asch@cs.wisc.edu>, Fall 2024
// Revised: Leshna Balara <lbalara@cs.wisc.edu>, Spring 2025

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

#include "wsh.h"

/* Print an application error message (does not exit) */
void app_error(char *msg)
{
  fprintf(stderr, "%s\n", msg);
}

void non_recoverable_error(char *msg)
{
  fprintf(stderr, "%s\n", msg);
  exit(-1);
}

// This is just basic string parsing, some of the things not handled are mentioned as TODOs*/
// Exits currently do not return the correct value according to the spec,
// so you will need to change the parameter when appropriate*/
// Some helper functions available to you but currently not reachable:
// char *do_variable_substitution(const char *command)
// char *replaceCommandSubstitution(const char *command)
// print_ps_header (also provides how to print ps entries as HINT)
int main(int argc, char **argv)
{
  check_params(argv, argc);
  setenv("PATH", "/bin", 1);
  if (argc == 1)
    interactive_main();
  else
    batch_main(argv[1]);

  return 0; /* Not reached */
}

static int exit_status = 0;

/* TODO: Print prompt where appropriate */
void interactive_main(void)
{
  if (!getcwd(cwd, MAXLINE))
  {
    perror("getcwd");
    exit(-1);
  }
  while (1)
  {
    printf("wsh> "); // Print the prompt
    fflush(stdout);  // Ensure the prompt is displayed immediately
    char cmdline[MAXLINE];
    if (fgets(cmdline, MAXLINE, stdin) == NULL)
    {
      if (ferror(stdin))
        app_error("fgets error");
      if (feof(stdin))
      {
          /* TODO: Handle end of file */
        exit(exit_status);
      }
    }

    eval(cmdline);
    fflush(stdout);
  }
}

/* Batch mode: read each line from the given script file */
int batch_main(char *scriptFile)
{
  FILE *file = fopen(scriptFile, "r");
  if (file == NULL)
  {
    perror("Error opening file");
    exit(-1);
  }

  char line[MAXLINE];
  while (fgets(line, MAXLINE, file) != NULL)
  {
    int length = strlen(line);
    if (length > 0 && line[length - 1] != '\n')
    {
      line[length] = '\n';
      line[length + 1] = '\0';
    }
    eval(line);
  }

  fclose(file);
  exit(0);
  return 0;
}

/* Evaluate a command line */
/* TODO: Actual Evaluation */
static char *local_vars[MAXARGS];
static int local_var_count = 0;

void eval(char *cmdline)
{
  /* If the command contains a pipe, handle it separately */
  if (strchr(cmdline, '|') != NULL)
  {
    eval_pipe(cmdline);
    return;
  }

  // Replace subcmds from cmdline
  char *first_cmd = replaceCommandSubstitution(cmdline);
  if (!first_cmd)
  {
    return;
  }

  // Replace VAR from cmdline
  char *final_cmd = do_variable_substitution(first_cmd);
  free(first_cmd);
  if (!final_cmd)
  {
    return;
  }

  // Parse cmds into argv and argc
  char *argv[MAXARGS];
  int argc;
  parseline_no_subst(final_cmd, argv, &argc);
  free(final_cmd);
  if (argc == 0)
  {
    return;
  }

  // Exec built in cmds
  if (strcmp(argv[0], "exit") == 0)
  {
    if(!argv[1]) {
      exit(0);
    }
    else {
      app_error("Incorrect usage of exit. Usage: exit");
      exit_status = 255;
      return;
    }
  }
  else if (strcmp(argv[0], "export") == 0)
  {
    if (!argv[1])
    {
      app_error("Incorrect usage of export. Usage: export {VariableName}={VariableValue}");
      exit_status = 255;
      return;
    }
    char *equal_sign = strchr(argv[1], '=');
    if (equal_sign)
    {
      *equal_sign = '\0'; // to split the str into VAR and val
      char *var = argv[1];
      char *value = equal_sign + 1;
      if(*value == '\0')
      {
        if (unsetenv(var) != 0)
        {
          perror("unsetenv");
        }
      }
      else{
        if (setenv(var, value, 1) != 0)
        {
          perror("setenv");
        }
      }
    }
    else
    {
      if (unsetenv(argv[1]) != 0)
      {
        perror("unsetenv");
      }
    }
    return;
  }
  else if (strcmp(argv[0], "local") == 0)
  {
      if (!argv[1] || strchr(argv[1], '=') == NULL)
      {
          app_error("Incorrect usage of local. Usage: local {VariableName}={VariableValue}");
          exit_status = 255;
          return;
      }

      char *equal_sign = strchr(argv[1], '=');
      size_t name_length = equal_sign - argv[1];

      // Extract variable name
      char var_name[256];
      strncpy(var_name, argv[1], name_length);
      var_name[name_length] = '\0';

      // Extract variable value
      char *value = equal_sign + 1;

      if (value[0] == '$')
      {
          for (int i = 0; i < local_var_count; i++)
          {
              if (strncmp(local_vars[i], value + 1, strlen(value + 1)) == 0 && local_vars[i][strlen(value + 1)] == '=')
              {
                  value = strchr(local_vars[i], '=') + 1;
                  break;
              }
          }
      }

      // Search for existing variable and overwrite
      for (int i = 0; i < local_var_count; i++)
      {
          if (strncmp(local_vars[i], argv[1], name_length) == 0 && local_vars[i][name_length] == '=')
          {
              free(local_vars[i]);
              local_vars[i] = malloc(strlen(var_name) + strlen(value) + 2);
              sprintf(local_vars[i], "%s=%s", var_name, value);
              return;
          }
      }

      // If not found, add as a new variable
      if (local_var_count < MAXARGS)
      {
          local_vars[local_var_count] = malloc(strlen(var_name) + strlen(value) + 2);
          sprintf(local_vars[local_var_count], "%s=%s", var_name, value);
          local_var_count++;
      }
      else
      {
          fprintf(stderr, "No space to store local var\n");
      }
      return;
  }

  else if (strcmp(argv[0], "vars") == 0)
  {
    for (int i = 0; i < local_var_count; i++)
    {
      printf("%s\n", local_vars[i]);
    }
    return;
  }
  else if (strcmp(argv[0], "ls") == 0)
  {
      struct dirent *entry;
      DIR *dir = opendir(".");
      if (!dir)
      {
          perror("ls");
          return;
      }

      char *files[1024]; // arr to store file names
      int count = 0;
      while ((entry = readdir(dir)) != NULL)
      {
          if (entry->d_name[0] == '.')
              continue; // skip hidden file

          files[count] = strdup(entry->d_name);
          count++;
      }
      closedir(dir);

      // double for loop sort the arr
      for (int i = 0; i < count - 1; i++)
      {
          for (int j = i + 1; j < count; j++)
          {
              if (strcmp(files[i], files[j]) > 0)
              {
                  char *temp = files[i];
                  files[i] = files[j];
                  files[j] = temp;
              }
          }
      }

      // Print ls
      for (int i = 0; i < count; i++)
      {
          struct stat st;
          if (stat(files[i], &st) == 0 && S_ISDIR(st.st_mode))
              printf("%s/\n", files[i]);
          else
              printf("%s\n", files[i]);
          free(files[i]);
      }

      return;
  }
  else if (strcmp(argv[0], "ps") == 0)
  {
    struct dirent *entry;
    DIR *dir = opendir("/proc/");
    if (!dir)
    {
      perror("ps");
      return;
    }

    printf("%5s %5s %1s %s\n", "PID", "PPID", "S", "COMMAND");
    while ((entry = readdir(dir)) != NULL)
    {
      if (!isdigit(entry->d_name[0]))
        continue;
      char path[256];
	    snprintf(path, sizeof(path), "/proc/%.240s/stat", entry->d_name);

      FILE *file = fopen(path, "r");
      if (!file)
        continue;

      int pid, ppid;
      char state;
      char comm[256];
      if (fscanf(file, "%d (%255[^)]) %c %d", &pid, comm, &state, &ppid) == 4)
      {
        printf("%5d %5d %c %s\n", pid, ppid, state, comm);
      }
      fclose(file);
    }
    closedir(dir);
    return;
  }

  // exec external cmds
  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    return;
  }

  if (pid == 0)
  {
    // Child proc
    execvp(argv[0], argv);
    app_error("Command not found or not executable");
    exit_status = 255;
    return;
  }

  // Parent proc, wait for child proc
  int status;
  waitpid(pid, &status, 0);
}

/* basic check to verify we have at most 2 arguments */
void check_params(char **argv, int argc)
{
  (void)argv;
  if (argc > 2)
  {
    printf("Usage: wsh or wsh script.wsh \n");
    printf("No arguments allowed.\n");
    exit(-1);
  }
}

/* Evaluate a command line that contains a pipe */
/* TODO: The basic parsing in eval_pipe does not handle substitutions of any kind */
void eval_pipe(char *cmdline)
{
    // Process cmd substitutions
    char *expanded_cmdline = replaceCommandSubstitution(cmdline);
    if (!expanded_cmdline)
        return;

    cmdline = expanded_cmdline;

    // Split the processed command line on |
    char *commands[MAXARGS];
    int num_commands = 0;
    char *token = strtok(cmdline, "|");
    while (token != NULL && num_commands < MAXARGS)
    {
        while (*token == ' ')
            token++; // Trim leading whitespace
        commands[num_commands++] = strdup(token);
        token = strtok(NULL, "|");
    }

    free(expanded_cmdline);

    /* For each pipeline segment, tokenize without re‑substitution */
    char **argv_list[num_commands];
    int argc_list[num_commands];
    for (int i = 0; i < num_commands; i++)
    {
        argv_list[i] = malloc(sizeof(char *) * MAXARGS);
        if (!argv_list[i])
        {
            perror("malloc");
            exit(1);
        }
        parseline_no_subst(commands[i], argv_list[i], &argc_list[i]);
        free(commands[i]);
        if (!argc_list[i])
        {
            return;
        }
    }

    /* Create (num_commands-1) pipes */
    int *pipefds = NULL;
    if (num_commands > 1) {
        pipefds = malloc(2 * (num_commands - 1) * sizeof(int) * 2);
        if (!pipefds) {
            perror("malloc");
            exit(1);
        }
    }
  
    for (int i = 0; i < num_commands - 1; i++)
    {
        if (pipe(pipefds + i * 2) < 0)
        {
            perror("pipe");
            exit(1);
        }
    }

    /* Execute commands */
    for (int i = 0; i < num_commands; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            exit(1);
        }

        if (pid == 0) // Child process
        {
            /* Redirect input if not the first command */
            if (i > 0)
            {
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) < 0)
                {
                    perror("dup2 input");
                    exit(1);
                }
            }

            /* Redirect output if not the last command */
            if (i < num_commands - 1)
            {
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) < 0)
                {
                    perror("dup2 output");
                    exit(1);
                }
            }

            /* Close all pipes */
            for (int j = 0; j < 2 * (num_commands - 1); j++)
                close(pipefds[j]);

            /* Execute command */
            execvp(argv_list[i][0], argv_list[i]);
            fprintf(stderr, "Command not found or not executable\n");
            exit(1);
        }
    }

    /* Close all pipe file descriptors in parent */
    for (int i = 0; i < 2 * (num_commands - 1); i++)
        close(pipefds[i]);

    /* Wait for all child processes */
    for (int i = 0; i < num_commands; i++)
        wait(NULL);

    /* Free allocated memory */
    for (int i = 0; i < num_commands; i++)
        free(argv_list[i]);
}


/* Replace variables of the form $VAR (skipping those starting with $(') */
char *do_variable_substitution(const char *command)
{
    const char *delimiters = " \t\n=";
    char *commandCopy = strdup(command);
    if (!commandCopy)
    {
        perror("strdup");
        exit(-1);
    }

    char *current = strdup(command);
    if (!current)
    {
        perror("strdup");
        exit(-1);
    }

    char *saveptr;
    char *token = strtok_r(commandCopy, delimiters, &saveptr);
    while (token != NULL)
    {
        if (token[0] == '$' && token[1] != '(') // Skip command substitution cases
        {
            char *var_name = token + 1; // Remove `$`
            char *var_value = getenv(var_name); // Check environment variables

            if (!var_value)
            {
                for (int i = 0; i < local_var_count; i++)
                {
                    char *equal_sign = strchr(local_vars[i], '=');
                    if (equal_sign)
                    {
                        *equal_sign = '\0';
                        if (strcmp(local_vars[i], var_name) == 0)
                        {
                            var_value = equal_sign + 1;
                        }
                        *equal_sign = '='; // Restore the variable
                    }
                }
            }

            if (!var_value)
            {
                var_value = ""; // If not found, replace with an empty string
            }

            /* Replace $VAR with its value */
            char *new_command = malloc(strlen(current) - strlen(token) + strlen(var_value) + 1);
            if (!new_command)
            {
                perror("malloc");
                exit(-1);
            }

            char *pos = strstr(current, token);
            if (pos)
            {
                size_t before_var_len = pos - current;
                strncpy(new_command, current, before_var_len);
                new_command[before_var_len] = '\0';
                strcat(new_command, var_value);
                strcat(new_command, pos + strlen(token));

                free(current);
                current = new_command;
            }
        }
        token = strtok_r(NULL, delimiters, &saveptr);
    }
    free(commandCopy);
    return current;
}

/* Replace command substitutions of the form $(subcommand) recursively
  Returns NULL in case of any errors */
char *replaceCommandSubstitution(const char *command)
{
    char *result = strdup(command);
    if (!result)
    {
        perror("strdup");
        exit(-1);
    }

    // Expand inner command substitutions first
    char *sub_start;
    while ((sub_start = strstr(result, "$(")) != NULL)
    {
        char *sub_end = sub_start + 2;
        int paren_count = 1;

        // Find matching closing )
        while (*sub_end && paren_count > 0)
        {
            if (*sub_end == '(')
                paren_count++;
            else if (*sub_end == ')')
                paren_count--;

            sub_end++;
        }

        // Check for unmatched parentheses
        if (paren_count != 0)
        {
            app_error("Unmatched parentheses in command substitution");
            free(result);
            return NULL;
        }

        // Extract command inside
        int sub_len = (sub_end-1) - (sub_start + 2);
        if (sub_len <= 0)
        {
            app_error("Invalid command substitution");
            free(result);
            return NULL;
        }
        char *sub_cmd = malloc(sub_len + 1);
        if (!sub_cmd)
        {
            perror("malloc");
            exit(-1);
        }
        strncpy(sub_cmd, sub_start + 2, sub_len);
        sub_cmd[sub_len] = '\0';

        // Expand inner substitutions recursively
        char *expanded_sub_cmd = replaceCommandSubstitution(sub_cmd);
        free(sub_cmd);

        // Execute fully expanded command
        char output[MAXLINE] = {0};
        FILE *fp = popen(expanded_sub_cmd, "r");
        if (!fp)
        {
            perror("popen");
            free(expanded_sub_cmd);
            free(result);
            return NULL;
        }

        size_t total_read = 0;
        while (fgets(output + total_read, MAXLINE - total_read, fp) != NULL)
        {
            total_read = strlen(output);
        }
        pclose(fp);
        free(expanded_sub_cmd);

        // Trim trailing newlines and ensure space separation
        size_t out_len = strlen(output);
        if (out_len > 0 && output[out_len-1]=='\n')
        {
          output[out_len - 1] = '\0';
        }

        size_t prefix_len = sub_start - result;
        size_t output_len = strlen(output); 
        size_t suffix_len = strlen(sub_end);

        size_t new_len = prefix_len + output_len + suffix_len + 1;
        char *new_result = malloc(new_len);
        if (!new_result)
        {
            perror("malloc");
            free(result);
            return NULL;
        }
        memcpy(new_result, result, prefix_len);                 
        memcpy(new_result + prefix_len, output, output_len);    
        memcpy(new_result + prefix_len + output_len, sub_end, suffix_len+1);
		    free(result);
        result = new_result;
    }
    return result;
}


/* Tokenize without further substitution.
   This version now strdup()’s each token so that the returned argv tokens remain valid. */
void parseline_no_subst(const char *cmdline, char **argv, int *argc)
{
  char *buf = strdup(cmdline);
  if (!buf)
  {
    perror("strdup");
    exit(-1);
  }
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = ' ';
  else
  {
    buf = realloc(buf, len + 2);
    if (!buf)
    {
      perror("realloc");
      exit(-1);
    }
    strcat(buf, " ");
  }

  int count = 0;
  char *p = buf;
  while (*p && (*p == ' '))
    p++; /* skip leading spaces */

  while (*p)
  {
    char *token_start = p;
    char *token;
    if (*p == '\'')
    {
      token_start = ++p;
      token = strchr(p, '\'');
      if (!token)
      {
        app_error("Missing closing quote");
        free(buf);
        return;
      }
      *token = '\0';
      p = token + 1;
    }
    else
    {
      token = strchr(p, ' ');
      if (!token)
        break;
      *token = '\0';
      p = token + 1;
    }
    argv[count++] = strdup(token_start);
    while (*p && (*p == ' '))
      p++;
  }
  argv[count] = NULL;
  *argc = count;
  free(buf);
}

typedef struct
{
  u_int16_t width;
  const char *header;
} ps_out_t;

static const ps_out_t out_spec[] = {
    {5, "PID"},
    {5, "PPID"},
    {1, "S"},
    {16, "CMD"},
};

/* Print the header with appropriate formatting for ps command */
void print_ps_header()
{
  for (size_t i = 0; i < sizeof(out_spec) / sizeof(out_spec[0]); i++)
  {
    printf("%-*s ", out_spec[i].width, out_spec[i].header);
  }
  printf("\n");

  /* HINT: of how to print the actual entry
     printf("%*d %*d %*c %-*s\n",
           out_spec[0].width, pid, // pid is int
           out_spec[1].width, ppid, // ppid is int
           out_spec[2].width, state, // state is char
           out_spec[3].width, comm); // command is char array
  */
}




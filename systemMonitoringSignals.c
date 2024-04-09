#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#define READ_END 0
#define WRITE_END 1
#include "stats_functions.h"

// Custom signal handler for SIGINT (Ctrl + C)
void sigint_handler() {
  char response;
  printf("\033[30E");  // Move cursor down 2 lines
  printf("\nCtrl + C pressed. Do you want to quit the program? (y/n): ");
  scanf(" %c", &response);
  if (response == 'y' || response == 'Y') {
    printf("Exiting program.\n");
    exit(0);
  } else {
    // Re-enable the signal handler for SIGINT
    signal(SIGINT, sigint_handler);
    // Clear the input buffer
    while ((getchar()) != '\n');
  }
}

// Function that returns 1 if a string is an integer or not.
int isInteger(const char *str) {
  // Make a temporary as to not change the original string.
  char str1[strlen(str) + 1];
  strcpy(str1, str);
  char *endptr;
  strtol(str1, &endptr, 10);  // 10 specifies base 10 for decimal numbers

  // Check if conversion was successful
  return (*endptr == '\0');
}

// Function to compare part of a string to another string, used for cases of
// tdelay and sample flags
int cmpString(char *str, int num_ltr, char *str1) {
  // Check case that immediately make them not equal
  if (strlen(str) < strlen(str1)) {
    return 0;
  }

  char new_str[strlen(str) + 1];
  strcpy(new_str, str);
  new_str[num_ltr - 1] = '\0';

  if (strcmp(new_str, str1) == 0) {
    return 1;
  }
  return 0;
}

// Function to extract a positive integer from a string, used in cases of tdelay
// and sample.
int extractPositiveInteger(const char *str) {
  int result = 0;
  int i = 0;

  // Skip leading non-numeric characters
  while (str[i] != '\0' && (str[i] < '0' || str[i] > '9')) {
    i++;
  }

  // Build the integer from numeric characters
  while (str[i] >= '0' && str[i] <= '9') {
    result = result * 10 + (str[i] - '0');
    i++;
  }

  return result;
}
// Function to count the number of current users
int countUsers() {
  // Use utmp.h to count the users.
  struct utmp *utmp_entry;

  setutent();  // Set the file position to the beginning of the utmp file

  int users = 0;

  while ((utmp_entry = getutent()) != NULL) {
    if (utmp_entry->ut_type == USER_PROCESS) {
      users++;
    }
  }

  endutent();  // Close the utmp file

  return users;
}

// Function to print non-sampled sections when all appear.
void printConstant(int sample, int seconds, int graphics) {
  printRunning(sample, seconds);
  printf("---------------------------------------\n");
  printf("### Memory ### (Phys.Used/Tot -- Virtual Used/Tot)\n");
  for (int i = 0; i < sample; i++) {
    printf("\n");
  }
  printf("---------------------------------------\n");
  printf("### Sessions/users ###\n");
  int n = countUsers();
  for (int i = 0; i < n; i++) {
    printf("\n");
  }
  printf("---------------------------------------\n");
  printf("\n");
  printf("\n");
  if (graphics) {
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
  }
  printf("---------------------------------------\n");
  printSystem();
  printf("---------------------------------------\n");
}

// Helper function that returns used physical memory to compare for graphics.
double firstMemorySample() {
  // Use sys/sysinfo to get memory ifnormation
  struct sysinfo mem_info;
  if (sysinfo(&mem_info) != 0) {
    perror("Failed to get system information");
    return -1;
  }

  // Physical memory
  double phys_used_gb = (double)(mem_info.totalram - mem_info.freeram) *
                        mem_info.mem_unit / (1024 * 1024 * 1024);
  return phys_used_gb;
}

// Function to print when no flags are present or system and user are present,
// accounts for graphics as well.
void printAllInformation(int sample, int seconds, int graphics) {
  if (graphics == 0) {
    printConstant(sample, seconds, graphics);

    for (int i = 0; i < sample; ++i) {
      int memoryPipe[2], usersPipe[2], cpuPipe[2];
      if (pipe(memoryPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }
      if (pipe(usersPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      if (pipe(cpuPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      pid_t memoryPid, usersPid, cpuPid;
      memoryPid = fork();
      if (memoryPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (memoryPid == 0) {  // Child process
        close(memoryPipe[0]);       // Close unused read end of the pipe
        dup2(memoryPipe[1],
             STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
        close(memoryPipe[1]);  // Close the write end of the pipe in the child
        printMemory();         // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }

      usersPid = fork();
      if (usersPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (usersPid == 0) {
        close(usersPipe[0]);  // Close unused read end of the pipe
        dup2(usersPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(usersPipe[1]);  // Close the write end of the pipe in the child
        printUsers();         // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }
      // Close unnecessary pipe ends in the parent process

      cpuPid = fork();
      if (cpuPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (cpuPid == 0) {
        close(cpuPipe[0]);  // Close unused read end of the pipe
        dup2(cpuPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(cpuPipe[1]);    // Close the write end of the pipe in the child
        printCpu();           // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }
      // Close unnecessary pipe ends in the parent process

      close(usersPipe[1]);
      close(memoryPipe[1]);
      close(cpuPipe[1]);

      char buffer[1024];
      char buffer1[1024];
      char buffer2[1024];
      ssize_t nbytes1, nbytes2, nbytes3;

      while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 5 + i, (int)nbytes1,
               buffer);  // Move cursor to the top left corner and then to
                         // the 17 + i rows down and the first column, then
                         // print the content
      }

      while ((nbytes2 = read(usersPipe[0], buffer1, sizeof(buffer1))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 7 + sample, (int)nbytes2,
               buffer1);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      int users = countUsers();
      while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", users + 8 + sample, (int)nbytes3,
               buffer2);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      close(memoryPipe[0]);
      close(usersPipe[0]);
      close(cpuPipe[0]);
      sleep(seconds);  // Wait for 1 second before sampling again

      wait(NULL);
      wait(NULL);
      wait(NULL);
    }
    printf("\033[999B");
    return;
  } else {
    printConstant(sample, seconds, 1);
    double first = firstMemorySample();
    for (int i = 0; i < sample; ++i) {
      int memoryPipe[2], usersPipe[2], cpuPipe[2], cpuGraphicalPipe[2];
      if (pipe(memoryPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }
      if (pipe(usersPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      if (pipe(cpuPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      if (pipe(cpuGraphicalPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      pid_t memoryPid, usersPid, cpuPid, cpuGraphPid;
      memoryPid = fork();
      if (memoryPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (memoryPid == 0) {  // Child process
        close(memoryPipe[0]);       // Close unused read end of the pipe
        dup2(memoryPipe[1],
             STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
        close(memoryPipe[1]);  // Close the write end of the pipe in the child

        if (i == 0) {
          printMemoryGraphical(0.00);
        } else {
          printMemoryGraphical(first);
        }
        exit(EXIT_SUCCESS);
      }

      usersPid = fork();
      if (usersPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (usersPid == 0) {
        close(usersPipe[0]);  // Close unused read end of the pipe
        dup2(usersPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(usersPipe[1]);  // Close the write end of the pipe in the child
        printUsers();         // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }
      // Close unnecessary pipe ends in the parent process

      cpuPid = fork();
      if (cpuPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (cpuPid == 0) {
        close(cpuPipe[0]);  // Close unused read end of the pipe
        dup2(cpuPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(cpuPipe[1]);    // Close the write end of the pipe in the child
        printCpu();           // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }

      cpuGraphPid = fork();
      if (cpuGraphPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (cpuGraphPid == 0) {
        close(cpuGraphicalPipe[0]);  // Close unused read end of the pipe
        dup2(cpuGraphicalPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(cpuGraphicalPipe[1]);  // Close the write end of the pipe in the
                                     // child
        printCpuGraphics();  // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }
      // Close unnecessary pipe ends in the parent process

      close(usersPipe[1]);
      close(memoryPipe[1]);
      close(cpuPipe[1]);
      close(cpuGraphicalPipe[1]);

      char buffer[1024];
      char buffer1[1024];
      char buffer2[1024];
      char buffer3[1024];
      ssize_t nbytes1, nbytes2, nbytes3, nbytes4;

      while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 5 + i, (int)nbytes1,
               buffer);  // Move cursor to the top left corner and then to
                         // the 17 + i rows down and the first column, then
                         // print the content
      }

      while ((nbytes2 = read(usersPipe[0], buffer1, sizeof(buffer1))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 7 + sample, (int)nbytes2,
               buffer1);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      int users = countUsers();
      while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", users + 8 + sample, (int)nbytes3,
               buffer2);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      while ((nbytes4 = read(cpuGraphicalPipe[0], buffer3, sizeof(buffer3))) >
             0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", users + 10 + sample + i, (int)nbytes4,
               buffer3);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      close(memoryPipe[0]);
      close(usersPipe[0]);
      close(cpuPipe[0]);
      close(cpuGraphicalPipe[0]);
      sleep(seconds);  // Wait for 1 second before sampling again

      wait(NULL);
      wait(NULL);
      wait(NULL);
      wait(NULL);
    }
    printf("\033[999B");
    return;
  }
}

// Print the things that stay constant
void printUserConstant(int sample, int seconds) {
  printRunning(sample, seconds);
  printf("---------------------------------------\n");
  printf("### Sessions/users ###\n");
  int n = countUsers();
  for (int i = 0; i < n; i++) {
    printf("\n");
  }
  printf("---------------------------------------\n");
  printSystem();
  printf("---------------------------------------\n");
}

// Print user information when only user is present.
void printUserInformation(int sample, int seconds) {
  printUserConstant(sample, seconds);
  for (int i = 0; i < sample; ++i) {
    int usersPipe[2];
    if (pipe(usersPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    pid_t usersPid;
    usersPid = fork();
    if (usersPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (usersPid == 0) {
      close(usersPipe[0]);  // Close unused read end of the pipe
      dup2(usersPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(usersPipe[1]);  // Close the write end of the pipe in the child
      printUsers();         // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    close(usersPipe[1]);

    char buffer1[1024];

    ssize_t nbytes2;

    while ((nbytes2 = read(usersPipe[0], buffer1, sizeof(buffer1))) > 0) {
      // Print the data using printf
      printf("\033[1;1H\033[%d;0H%.*s", 5, (int)nbytes2, buffer1);
    }

    close(usersPipe[0]);

    sleep(seconds);  // Wait for 1 second before sampling again
    wait(NULL);
  }
  printf("\033[999B");
  return;
}

// Print user when sequential is called (no for loop)
void printUserSeq(int sample, int seconds) {
  printUserConstant(sample, seconds);
  int usersPipe[2];
  if (pipe(usersPipe) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  pid_t usersPid;
  usersPid = fork();
  if (usersPid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else if (usersPid == 0) {
    close(usersPipe[0]);  // Close unused read end of the pipe
    dup2(usersPipe[1],
         STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
    close(usersPipe[1]);  // Close the write end of the pipe in the child
    printUsers();         // Execute the function in the child process
    exit(EXIT_SUCCESS);
  }
  // Close unnecessary pipe ends in the parent process

  close(usersPipe[1]);

  char buffer1[1024];

  ssize_t nbytes2;

  while ((nbytes2 = read(usersPipe[0], buffer1, sizeof(buffer1))) > 0) {
    // Print the data using printf
    printf("\033[1;1H\033[%d;0H%.*s", 5, (int)nbytes2, buffer1);
  }

  close(usersPipe[0]);

  sleep(seconds);  // Wait for 1 second before sampling again
  wait(NULL);

  printf("\033[999B");
  return;
}

// Print just system information for when system is called.
void printSystemInformation(int sample, int seconds, int graphics) {
  if (graphics == 0) {
    printRunning(sample, seconds);
    printf("---------------------------------------\n");
    printf("### Memory ### (Phys.Used/Tot -- Virtual Used/Tot)\n");
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
    printf("---------------------------------------\n");
    printf("\n");
    printf("\n");
    printf("---------------------------------------\n");
    printSystem();
    printf("---------------------------------------\n");

    for (int i = 0; i < sample; ++i) {
      int memoryPipe[2], cpuPipe[2];
      if (pipe(memoryPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      if (pipe(cpuPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      pid_t memoryPid, cpuPid;
      memoryPid = fork();
      if (memoryPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (memoryPid == 0) {  // Child process
        close(memoryPipe[0]);       // Close unused read end of the pipe
        dup2(memoryPipe[1],
             STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
        close(memoryPipe[1]);  // Close the write end of the pipe in the child
        printMemory();         // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }

      cpuPid = fork();
      if (cpuPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (cpuPid == 0) {
        close(cpuPipe[0]);  // Close unused read end of the pipe
        dup2(cpuPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(cpuPipe[1]);    // Close the write end of the pipe in the child
        printCpu();           // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }
      // Close unnecessary pipe ends in the parent process

      close(memoryPipe[1]);
      close(cpuPipe[1]);

      char buffer[1024];

      char buffer2[1024];
      ssize_t nbytes1, nbytes3;

      while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 5 + i, (int)nbytes1,
               buffer);  // Move cursor to the top left corner and then to
                         // the 17 + i rows down and the first column, then
                         // print the content
      }

      while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 6 + sample, (int)nbytes3,
               buffer2);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      close(memoryPipe[0]);

      close(cpuPipe[0]);
      sleep(seconds);  // Wait for 1 second before sampling again

      wait(NULL);
      wait(NULL);
    }
    printf("\033[999B");
    return;
  } else {
    printRunning(sample, seconds);
    printf("---------------------------------------\n");
    printf("### Memory ### (Phys.Used/Tot -- Virtual Used/Tot)\n");
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
    printf("---------------------------------------\n");
    printf("\n");
    printf("\n");
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
    printf("---------------------------------------\n");
    printSystem();
    printf("---------------------------------------\n");
    double first = firstMemorySample();
    for (int i = 0; i < sample; ++i) {
      int memoryPipe[2], cpuPipe[2], cpuGraphicalPipe[2];
      if (pipe(memoryPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      if (pipe(cpuPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      if (pipe(cpuGraphicalPipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }

      pid_t memoryPid, cpuPid, cpuGraphPid;
      memoryPid = fork();
      if (memoryPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (memoryPid == 0) {  // Child process
        close(memoryPipe[0]);       // Close unused read end of the pipe
        dup2(memoryPipe[1],
             STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
        close(memoryPipe[1]);  // Close the write end of the pipe in the child

        if (i == 0) {
          printMemoryGraphical(0.00);
        } else {
          printMemoryGraphical(first);
        }
        exit(EXIT_SUCCESS);
      }

      cpuPid = fork();
      if (cpuPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (cpuPid == 0) {
        close(cpuPipe[0]);  // Close unused read end of the pipe
        dup2(cpuPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(cpuPipe[1]);    // Close the write end of the pipe in the child
        printCpu();           // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }

      cpuGraphPid = fork();
      if (cpuGraphPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
      } else if (cpuGraphPid == 0) {
        close(cpuGraphicalPipe[0]);  // Close unused read end of the pipe
        dup2(cpuGraphicalPipe[1],
             STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
        close(cpuGraphicalPipe[1]);  // Close the write end of the pipe in the
                                     // child
        printCpuGraphics();  // Execute the function in the child process
        exit(EXIT_SUCCESS);
      }
      // Close unnecessary pipe ends in the parent process

      close(memoryPipe[1]);
      close(cpuPipe[1]);
      close(cpuGraphicalPipe[1]);

      char buffer[1024];

      char buffer2[1024];
      char buffer3[1024];
      ssize_t nbytes1, nbytes3, nbytes4;

      while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 5 + i, (int)nbytes1,
               buffer);  // Move cursor to the top left corner and then to
                         // the 17 + i rows down and the first column, then
                         // print the content
      }

      while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 6 + sample, (int)nbytes3,
               buffer2);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      while ((nbytes4 = read(cpuGraphicalPipe[0], buffer3, sizeof(buffer3))) >
             0) {
        // Print the data using printf
        printf("\033[1;1H\033[%d;0H%.*s", 8 + sample + i, (int)nbytes4,
               buffer3);  // Move cursor to the top left corner and then to
                          // the 17 + i rows down and the first column, then
                          // print the content
      }

      close(memoryPipe[0]);

      close(cpuPipe[0]);
      close(cpuGraphicalPipe[0]);
      sleep(seconds);  // Wait for 1 second before sampling again

      wait(NULL);

      wait(NULL);
      wait(NULL);
    }
    printf("\033[999B");
    return;
  }
}

// Print system for when sequential is called and only system
void printSystemSeq(int sample, int seconds, int graphics, int i) {
  if (graphics == 0) {
    printRunning(sample, seconds);
    printf("---------------------------------------\n");
    printf("### Memory ### (Phys.Used/Tot -- Virtual Used/Tot)\n");
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
    printf("---------------------------------------\n");
    printf("\n");
    printf("\n");
    printf("---------------------------------------\n");
    printSystem();
    printf("---------------------------------------\n");

    int memoryPipe[2], cpuPipe[2];
    if (pipe(memoryPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    if (pipe(cpuPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    pid_t memoryPid, cpuPid;
    memoryPid = fork();
    if (memoryPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (memoryPid == 0) {  // Child process
      close(memoryPipe[0]);       // Close unused read end of the pipe
      dup2(memoryPipe[1],
           STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
      close(memoryPipe[1]);  // Close the write end of the pipe in the child
      printMemory();         // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }

    cpuPid = fork();
    if (cpuPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (cpuPid == 0) {
      close(cpuPipe[0]);  // Close unused read end of the pipe
      dup2(cpuPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(cpuPipe[1]);    // Close the write end of the pipe in the child
      printCpu();           // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    close(memoryPipe[1]);
    close(cpuPipe[1]);

    char buffer[1024];

    char buffer2[1024];
    ssize_t nbytes1, nbytes3;

    while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
      // Print the data using printf
      printf("\033[1;1H\033[%d;0H%.*s", 5 + i, (int)nbytes1,
             buffer);  // Move cursor to the top left corner and then to
                       // the 17 + i rows down and the first column, then
                       // print the content
    }

    while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
      // Print the data using printf
      printf("\033[1;1H\033[%d;0H%.*s", 6 + sample, (int)nbytes3,
             buffer2);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    close(memoryPipe[0]);

    close(cpuPipe[0]);
    sleep(seconds);  // Wait for 1 second before sampling again

    wait(NULL);
    wait(NULL);

    printf("\033[999B");
    return;
  } else {
    printRunning(sample, seconds);
    printf("---------------------------------------\n");
    printf("### Memory ### (Phys.Used/Tot -- Virtual Used/Tot)\n");
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
    printf("---------------------------------------\n");
    printf("\n");
    printf("\n");
    for (int i = 0; i < sample; i++) {
      printf("\n");
    }
    printf("---------------------------------------\n");
    printSystem();
    printf("---------------------------------------\n");
    double first = firstMemorySample();

    int memoryPipe[2], cpuPipe[2], cpuGraphicalPipe[2];
    if (pipe(memoryPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    if (pipe(cpuPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    if (pipe(cpuGraphicalPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    pid_t memoryPid, cpuPid, cpuGraphPid;
    memoryPid = fork();
    if (memoryPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (memoryPid == 0) {  // Child process
      close(memoryPipe[0]);       // Close unused read end of the pipe
      dup2(memoryPipe[1],
           STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
      close(memoryPipe[1]);  // Close the write end of the pipe in the child

      if (i == 0) {
        printMemoryGraphical(0.00);
      } else {
        printMemoryGraphical(first);
      }
      exit(EXIT_SUCCESS);
    }

    cpuPid = fork();
    if (cpuPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (cpuPid == 0) {
      close(cpuPipe[0]);  // Close unused read end of the pipe
      dup2(cpuPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(cpuPipe[1]);    // Close the write end of the pipe in the child
      printCpu();           // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }

    cpuGraphPid = fork();
    if (cpuGraphPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (cpuGraphPid == 0) {
      close(cpuGraphicalPipe[0]);  // Close unused read end of the pipe
      dup2(cpuGraphicalPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(cpuGraphicalPipe[1]);  // Close the write end of the pipe in the
                                   // child
      printCpuGraphics();          // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    close(memoryPipe[1]);
    close(cpuPipe[1]);
    close(cpuGraphicalPipe[1]);

    char buffer[1024];

    char buffer2[1024];
    char buffer3[1024];
    ssize_t nbytes1, nbytes3, nbytes4;

    while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
      // Print the data using printf
      printf("\033[1;1H\033[%d;0H%.*s", 5 + i, (int)nbytes1,
             buffer);  // Move cursor to the top left corner and then to
                       // the 17 + i rows down and the first column, then
                       // print the content
    }

    while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
      // Print the data using printf
      printf("\033[1;1H\033[%d;0H%.*s", 6 + sample, (int)nbytes3,
             buffer2);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    while ((nbytes4 = read(cpuGraphicalPipe[0], buffer3, sizeof(buffer3))) >
           0) {
      // Print the data using printf
      printf("\033[1;1H\033[%d;0H%.*s", 8 + sample + i, (int)nbytes4,
             buffer3);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    close(memoryPipe[0]);

    close(cpuPipe[0]);
    close(cpuGraphicalPipe[0]);
    sleep(seconds);  // Wait for 1 second before sampling again

    wait(NULL);

    wait(NULL);
    wait(NULL);

    printf("\033[999B");
    return;
  }
}

// Print everything when sequential is called and appropriate condition.
void printAllSeq(int sample, int seconds, int graphics, int i) {
  if (graphics == 0) {
    printConstant(sample, seconds, graphics);
    int memoryPipe[2], usersPipe[2], cpuPipe[2];
    if (pipe(memoryPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
    if (pipe(usersPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    if (pipe(cpuPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    pid_t memoryPid, usersPid, cpuPid;
    memoryPid = fork();
    if (memoryPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (memoryPid == 0) {  // Child process
      close(memoryPipe[0]);       // Close unused read end of the pipe
      dup2(memoryPipe[1],
           STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
      close(memoryPipe[1]);  // Close the write end of the pipe in the child
      printMemory();         // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }

    usersPid = fork();
    if (usersPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (usersPid == 0) {
      close(usersPipe[0]);  // Close unused read end of the pipe
      dup2(usersPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(usersPipe[1]);  // Close the write end of the pipe in the child
      printUsers();         // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    cpuPid = fork();
    if (cpuPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (cpuPid == 0) {
      close(cpuPipe[0]);  // Close unused read end of the pipe
      dup2(cpuPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(cpuPipe[1]);    // Close the write end of the pipe in the child
      printCpu();           // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    close(usersPipe[1]);
    close(memoryPipe[1]);
    close(cpuPipe[1]);

    char buffer[1024];
    char buffer1[1024];
    char buffer2[1024];
    ssize_t nbytes1, nbytes2, nbytes3;

    while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", 5 + i, (int)nbytes1,
             buffer);  // Move cursor to the top left corner and then to
                       // the 17 + i rows down and the first column, then
                       // print the content
    }

    while ((nbytes2 = read(usersPipe[0], buffer1, sizeof(buffer1))) > 0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", sample + 7, (int)nbytes2,
             buffer1);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", sample + 14, (int)nbytes3,
             buffer2);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }
    close(memoryPipe[0]);
    close(usersPipe[0]);
    close(cpuPipe[0]);

    wait(NULL);
    wait(NULL);
    wait(NULL);

    printf("\033[100B");
    return;
  } else {
    printConstant(sample, seconds, 1);
    double first = firstMemorySample();

    int memoryPipe[2], usersPipe[2], cpuPipe[2], cpuGraphicalPipe[2];
    if (pipe(memoryPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
    if (pipe(usersPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    if (pipe(cpuPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    if (pipe(cpuGraphicalPipe) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    pid_t memoryPid, usersPid, cpuPid, cpuGraphPid;
    memoryPid = fork();
    if (memoryPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (memoryPid == 0) {  // Child process
      close(memoryPipe[0]);       // Close unused read end of the pipe
      dup2(memoryPipe[1],
           STDOUT_FILENO);   // Redirect stdout to the write end of the pipe
      close(memoryPipe[1]);  // Close the write end of the pipe in the child

      if (i == 0) {
        printMemoryGraphical(0.00);
      } else {
        printMemoryGraphical(first);
      }
      exit(EXIT_SUCCESS);
    }

    usersPid = fork();
    if (usersPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (usersPid == 0) {
      close(usersPipe[0]);  // Close unused read end of the pipe
      dup2(usersPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(usersPipe[1]);  // Close the write end of the pipe in the child
      printUsers();         // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    cpuPid = fork();
    if (cpuPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (cpuPid == 0) {
      close(cpuPipe[0]);  // Close unused read end of the pipe
      dup2(cpuPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(cpuPipe[1]);    // Close the write end of the pipe in the child
      printCpu();           // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }

    cpuGraphPid = fork();
    if (cpuGraphPid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (cpuGraphPid == 0) {
      close(cpuGraphicalPipe[0]);  // Close unused read end of the pipe
      dup2(cpuGraphicalPipe[1],
           STDOUT_FILENO);  // Redirect stdout to the write end of the pipe
      close(cpuGraphicalPipe[1]);  // Close the write end of the pipe in the
                                   // child
      printCpuGraphics();          // Execute the function in the child process
      exit(EXIT_SUCCESS);
    }
    // Close unnecessary pipe ends in the parent process

    close(usersPipe[1]);
    close(memoryPipe[1]);
    close(cpuPipe[1]);
    close(cpuGraphicalPipe[1]);

    char buffer[1024];
    char buffer1[1024];
    char buffer2[1024];
    char buffer3[1024];
    ssize_t nbytes1, nbytes2, nbytes3, nbytes4;

    while ((nbytes1 = read(memoryPipe[0], buffer, sizeof(buffer))) > 0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", 5 + i, (int)nbytes1,
             buffer);  // Move cursor to the top left corner and then to
                       // the 17 + i rows down and the first column, then
                       // print the content
    }

    while ((nbytes2 = read(usersPipe[0], buffer1, sizeof(buffer1))) > 0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", 7 + sample, (int)nbytes2,
             buffer1);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    int users = countUsers();
    while ((nbytes3 = read(cpuPipe[0], buffer2, sizeof(buffer2))) > 0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", users + 8 + sample, (int)nbytes3,
             buffer2);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    while ((nbytes4 = read(cpuGraphicalPipe[0], buffer3, sizeof(buffer3))) >
           0) {
      // Print the data using printf
      printf("\033[%d;0H%.*s", users + 10 + sample + i, (int)nbytes4,
             buffer3);  // Move cursor to the top left corner and then to
                        // the 17 + i rows down and the first column, then
                        // print the content
    }

    close(memoryPipe[0]);
    close(usersPipe[0]);
    close(cpuPipe[0]);
    close(cpuGraphicalPipe[0]);

    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    printf("\033[999B");
    return;
  }
}

// Deal with conditionals and print appropriate text
void printConditionals(int sample, int seconds, int graphics, int system,
                       int user, int sequential) {
  if (sequential == 0) {
    if ((system == 0 && user == 0) || (system == 1 && user == 1)) {
      printAllInformation(sample, seconds, graphics);
    } else if (user == 1 && system == 0) {
      printUserInformation(sample, seconds);
    } else {
      printSystemInformation(sample, seconds, graphics);
    }
  } else {
    if ((system == 0 && user == 0) || (system == 1 && user == 1)) {
      for (int i = 0; i < sample; i++) {
        printf("\033[1;1H");
        printAllSeq(sample, seconds, graphics, i);

        // Scroll the content within the scrolling region upward by total_lines
        // lines
        printf("\033[999B");
        sleep(seconds);
      }
    } else if (user == 1 && system == 0) {
      for (int i = 0; i < sample; i++) {
        printf("\033[1;1H");
        printUserSeq(sample, seconds);

        // Scroll the content within the scrolling region upward by total_lines
        // lines
        printf("\033[999B");
        sleep(seconds);
      }
    } else {
      for (int i = 0; i < sample; i++) {
        printf("\033[1;1H");
        printSystemSeq(sample, seconds, graphics, i);

        // Scroll the content within the scrolling region upward by total_lines
        // lines
        printf("\033[999B");
        sleep(seconds);
      }
    }
  }
}

int main(int argc, char **argv) {
  // Set SIGTSTP signal handler to ignore
  signal(SIGTSTP, SIG_IGN);
  signal(SIGINT, sigint_handler);

  printf("\033[2J");    // Clear console
  printf("\033[1;1H");  // Start at top left

  // Case 1: No CLA, print everything, sample size 10, interval 1s
  if (argc == 1) {
    printAllInformation(10, 1, 0);
  }

  // CLAs present
  else {
    // Default Values
    int sample = 10;
    int seconds = 1;
    // Check for positional arguments
    for (int i = 1; i < argc; i++) {
      if (isInteger(argv[i])) {
        sample = strtol(argv[i], NULL, 10);
        // Check if correct format
        if (argv[i + 1] == NULL || !isInteger(argv[i + 1])) {
          printf(
              "Invalid Format, please follow the format x y, where x and y "
              "are "
              "integers indicating sample size and seconds.\n");
          exit(0);
        }
        seconds = strtol(argv[i + 1], NULL, 10);
        break;
      }
    }

    int system = 0;
    int user = 0;
    int sequential = 0;
    int graphics = 0;
    // Check for other flags
    for (int n = 1; n < argc; n++) {
      if (strcmp(argv[n], "--system") == 0) {
        system = 1;
      }
      if (strcmp(argv[n], "--user") == 0) {
        user = 1;
      }
      if (strcmp(argv[n], "--sequential") == 0) {
        sequential = 1;
      }
      if (strcmp(argv[n], "--graphics") == 0) {
        graphics = 1;
      }
      if (cmpString(argv[n], 11, "--samples=")) {
        sample = extractPositiveInteger(argv[n]);
      }
      if (cmpString(argv[n], 10, "--tdelay=")) {
        seconds = extractPositiveInteger(argv[n]);
      }
    }
    printConditionals(sample, seconds, graphics, system, user, sequential);
  }
  // Move cursor to the bottom to not overlap with printed information.
  printf("\033[999;1H");

  return 0;
}
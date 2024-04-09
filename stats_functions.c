#include "stats_functions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utmp.h>

// Function to print uptime
void printUptime() {
  FILE *uptime_file = fopen("/proc/uptime", "r");  // Open /proc/uptime

  double uptime_seconds;
  fscanf(uptime_file, "%lf", &uptime_seconds);  // Scan for seconds
  fclose(uptime_file);

  // Calculate days, hours, minutes, and seconds
  unsigned long long days = (unsigned long long)(uptime_seconds / (3600 * 24));
  unsigned long long hours =
      (unsigned long long)((int)uptime_seconds % (3600 * 24)) / 3600;
  unsigned long long minutes =
      (unsigned long long)((int)uptime_seconds % 3600) / 60;
  unsigned long long seconds = (unsigned long long)uptime_seconds % 60;

  // Calculate total time in hours, minutes, and seconds
  unsigned long long totalHours = (days * 24) + hours;

  // Print the result
  printf(
      "System running since last reboot: %llu days %02llu:%02llu:%02llu "
      "(%02llu:%02llu:%02llu)\n",
      days, hours, minutes, seconds, totalHours, minutes, seconds);
}

// Function to print all system information
void printSystem() {
  // Use sys/utsname.h to get system data.
  struct utsname systemData;
  uname(&systemData);
  printf("### System Information ###\n");
  printf("System Name = %s\n", systemData.sysname);
  printf("Machine Name = %s\n", systemData.nodename);
  printf("Version = %s\n", systemData.version);
  printf("Release = %s\n", systemData.release);
  printf("System Name = %s\n", systemData.sysname);
  printf("Architecture = %s\n", systemData.machine);
  printUptime();  // Print uptime whenever system information is printed.
}

// Function to get cpu usage
double getUsage() {
  FILE *file = fopen("/proc/stat", "r");
  if (file == NULL) {
    perror("Error opening /proc/stat");
    return -1;
  }

  char line[256];
  fgets(line, sizeof(line), file);

  long prev_user, prev_nice, prev_system, prev_idle, prev_iowait, prev_irq,
      prev_softirq;
  sscanf(line, "cpu %ld %ld %ld %ld %ld %ld %ld", &prev_user, &prev_nice,
         &prev_system, &prev_idle, &prev_iowait, &prev_irq, &prev_softirq);

  fclose(file);

  // Sleep for 1 second
  usleep(6000);

  file = fopen("/proc/stat", "r");
  if (file == NULL) {
    perror("Error opening /proc/stat");
    return -1;
  }

  fgets(line, sizeof(line), file);

  long curr_user, curr_nice, curr_system, curr_idle, curr_iowait, curr_irq,
      curr_softirq;
  sscanf(line, "cpu %ld %ld %ld %ld %ld %ld %ld", &curr_user, &curr_nice,
         &curr_system, &curr_idle, &curr_iowait, &curr_irq, &curr_softirq);

  fclose(file);

  // Calculate total and idle times
  long prev_total_time = prev_user + prev_nice + prev_system + prev_idle +
                         prev_iowait + prev_irq + prev_softirq;
  long curr_total_time = curr_user + curr_nice + curr_system + curr_idle +
                         curr_iowait + curr_irq + curr_softirq;
  long prev_idle_time = prev_idle;
  long curr_idle_time = curr_idle;

  // Calculate utilization
  double utilization = ((double)(curr_total_time - prev_total_time -
                                 (curr_idle_time - prev_idle_time)) /
                        (double)(curr_total_time - prev_total_time)) *
                       100.0;

  return utilization;
}

/// Function to print CPU information
void printCpu() {
  // Get number of cores using sysconf
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  printf("Number of cores: %d\n", num_cores);
  double cpu_usage =
      getUsage();  // Get double representing cpu usage percentage
  printf("total cpu use = %.2f%%\n", cpu_usage);
}

/// Function to print CPU information
void printCpuGraphics() {
  double cpu_usage =
      getUsage();  // Get double representing cpu usage percentage
  printf("\t\t | | |");
  double counter = cpu_usage;
  for (int i = 0; counter >= 1; i++) {
    printf(" |");
    counter -= 1;
  }
  printf(" %.2f\n", cpu_usage);
}

// Function to print samples, seconds and memory usage
void printRunning(int sample, int sec) {
  printf("Nbr of samples: %d -- every %d secs\n", sample, sec);
  // Using sys/resource.h to find memory usage
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  long memory_usage_kb = usage.ru_maxrss;
  printf("Memory usage: %lu kilobytes\n", memory_usage_kb);
}

// Function to print memory information at one snapshot in time
void printMemory() {
  // Use sys/sysinfo to get memory ifnormation
  struct sysinfo mem_info;
  if (sysinfo(&mem_info) != 0) {
    perror("Failed to get system information");
    return;
  }

  // Physical memory
  double phys_used_gb = (double)(mem_info.totalram - mem_info.freeram) *
                        mem_info.mem_unit / (1024 * 1024 * 1024);
  double phys_total_gb =
      (double)mem_info.totalram * mem_info.mem_unit / (1024 * 1024 * 1024);

  // Virtual memory
  double virt_used_gb = (double)(mem_info.totalswap - mem_info.freeswap) *
                        mem_info.mem_unit / (1024 * 1024 * 1024);
  double virt_total_gb =
      (double)mem_info.totalswap * mem_info.mem_unit / (1024 * 1024 * 1024);

  printf("%.2f GB / %.2f GB -- %.2f GB / %.2f GB\n", phys_used_gb,
         phys_total_gb, virt_used_gb, virt_total_gb);
}

// Function to print memory information at one snapshot in time
double printMemoryGraphical(double prev_phys) {
  // Use sys/sysinfo to get memory ifnormation
  struct sysinfo mem_info;
  if (sysinfo(&mem_info) != 0) {
    perror("Failed to get system information");
    return 0.00;
  }

  // Physical memory
  double phys_used_gb = (double)(mem_info.totalram - mem_info.freeram) *
                        mem_info.mem_unit / (1024 * 1024 * 1024);
  double phys_total_gb =
      (double)mem_info.totalram * mem_info.mem_unit / (1024 * 1024 * 1024);

  // Virtual memory
  double virt_used_gb = (double)(mem_info.totalswap - mem_info.freeswap) *
                        mem_info.mem_unit / (1024 * 1024 * 1024);
  double virt_total_gb =
      (double)mem_info.totalswap * mem_info.mem_unit / (1024 * 1024 * 1024);

  printf("%.2f GB / %.2f GB -- %.2f GB / %.2f GB", phys_used_gb, phys_total_gb,
         virt_used_gb, virt_total_gb);
  printf("\t|");
  if (prev_phys == 0) {
    printf("o 0.00 (%.2f)", phys_used_gb);
  } else {
    double diff = phys_used_gb - prev_phys;
    if (diff < 0) {
      diff = 0.00;
    }
    double count = diff;
    for (int i = 0; count > 0.00; i++) {
      printf("#");
      count -= 0.01;
    }
    printf("* %.2f (%.2f)\n", diff, phys_used_gb);
  }
  return phys_used_gb;
}

// Function to print users with a new line added for every user
void printUsers() {
  // Use utmp.h to get user information
  struct utmp *utmp_entry;

  setutent();  // Set the file position to the beginning of the utmp file

  int users = 0;
  int sessions = 0;

  while ((utmp_entry = getutent()) != NULL) {
    if (utmp_entry->ut_type == USER_PROCESS) {
      // Print User information
      printf(" %-10s %s (%s)\n", utmp_entry->ut_user, utmp_entry->ut_line,
             utmp_entry->ut_host);
      users++;
      sessions++;
    } else if (utmp_entry->ut_type == LOGIN_PROCESS) {
      sessions++;
    }
  }

  endutent();  // Close the utmp file
}
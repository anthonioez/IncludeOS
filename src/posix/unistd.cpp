// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <unistd.h>
#include <fd_map.hpp>
#include <kernel/os.hpp>
#include <kernel/rng.hpp>
#include <fs/vfs.hpp>
#include <file_fd.hpp>

static const int rng_fd       {998}; // temp

int open(const char* s, int, ...)
{
  if(strcmp(s, "/dev/random") == 0 || strcmp(s, "/dev/urandom") == 0) {
    return rng_fd;
  }
  if (s == nullptr) {
    errno = EFAULT;
    return -1;
  }
  if (strcmp(s, "") == 0) {
    errno = ENOENT;
    return -1;
  }
  try {
    auto ent = fs::VFS::stat_sync(s);
    if (ent.is_valid())
    {
      auto& fd = FD_map::_open<File_FD>(ent);
      return fd.get_id();
    }
    errno = ENOENT;
    return -1;
  }
  catch (...) {
    errno = ENOENT;
    return -1;
  }
}

int close(int fd)
{
  if(fd == rng_fd) {
    return 0;
  }
  try {
    return FD_map::_close(fd);
  }
  catch(const FD_not_found&)
  {
    errno = EBADF;
  }
  return -1;
}

int read(int fd, void* buf, size_t len)
{
  if (buf == nullptr) {
    errno = EFAULT;
    return -1;
  }
  if(fd == rng_fd) {
    rng_extract(buf, len);
    return len;
  }
  try {
    auto& fildes = FD_map::_get(fd);
    return fildes.read(buf, len);
  }
  catch(const FD_not_found&) {
    errno = EBADF;
    return -1;
  }
  return 0;
}

int write(int fd, const void* ptr, size_t len)
{
  if (fd < 4) {
    return OS::print((const char*) ptr, len);
  }
  else if (fd == rng_fd) {
    rng_absorb(ptr, len);
    return len;
  }
  try {
    auto& fildes = FD_map::_get(fd);
    return fildes.write(ptr, len);
  }
  catch(const FD_not_found&) {
    errno = EBADF;
    return -1;
  }
}

// read value of a symbolic link (which we don't have)
ssize_t readlink(const char* path, char*, size_t bufsiz)
{
  printf("readlink(%s, bufsize=%u)\n", path, bufsiz);
  return 0;
}

int fsync(int fildes)
{
  try {
    (void) fildes;
    //auto& fd = FD_map::_get(fildes);
    // files should return 0, and others should not
    return 0;
  }
  catch(const FD_not_found&) {
    errno = EBADF;
    return -1;
  }
}

int fchown(int, uid_t, gid_t)
{
  return -1;
}

off_t lseek(int fd, off_t offset, int whence) {
  try {
    auto& fildes = FD_map::_get(fd);
    return fildes.lseek(offset, whence);
  }
  catch(const FD_not_found&) {
    errno = EBADF;
    return -1;
  }
}

int isatty(int fd) {
  if (fd == 1 || fd == 2 || fd == 3) {
    debug("SYSCALL ISATTY Dummy returning 1");
    return 1;
  }
  try {
    auto& fildes = FD_map::_get(fd);
    (void) fildes;
    errno = ENOTTY;
    return 0;
  }
  catch(const FD_not_found&) {
    errno = EBADF;
    return 0;
  }
}

#include <kernel/irq_manager.hpp>
#include <kernel/rtc.hpp>
unsigned int sleep(unsigned int seconds)
{
  int64_t now  = RTC::now();
  int64_t done = now + seconds;
  while (true) {
    if (now >= done) break;
    OS::block();
    now = RTC::now();
  }
  return 0;
}

// todo: use fs::path as backing
static std::string cwd {"/"};
const std::string& cwd_ref() { return cwd; }

int chdir(const char *path)
// todo: handle relative path
// todo: handle ..
{
  if (not path or strlen(path) < 1)
  {
    errno = ENOENT;
    return -1;
  }
  if (strcmp(path, ".") == 0)
  {
    return 0;
  }
  std::string desired_path;
  if (*path != '/')
  {
    desired_path = cwd;
    if (!(desired_path.back() == '/')) desired_path += "/";
    desired_path += path;
  }
  else
  {
    desired_path.assign(path);
  }
  try {
    auto ent = fs::VFS::stat_sync(desired_path);
    if (ent.is_dir())
    {
      cwd = desired_path;
      assert(cwd.front() == '/');
      assert(cwd.find("..") == std::string::npos);
      return 0;
    }
    else
    {
      // path is not a dir
      errno = ENOTDIR;
      return -1;
    }
  }
  catch (const fs::Err_not_found& e) {
    errno = ENOTDIR;
    return -1;
  }
}

char *getcwd(char *buf, size_t size)
{
  assert(cwd.front() == '/');
  if (size == 0)
  {
    errno = EINVAL;
    return nullptr;
  }
  if ((cwd.length() + 1) < size)
  {
    snprintf(buf, size, "%s", cwd.c_str());
    return buf;
  }
  else
  {
    errno = ERANGE;
    return nullptr;
  }
}

int ftruncate(int fd, off_t length)
{
  (void) fd;
  (void) length;
  // TODO: needs writable filesystem
  return EBADF;
}

#include <limits.h>
#include <sys/resource.h>
long sysconf(int name) {
  // for indeterminate limits, return -1, *don't* set errno
  switch (name) {
    case _SC_AIO_LISTIO_MAX:
    case _SC_AIO_MAX:
    case _SC_AIO_PRIO_DELTA_MAX:
      return -1;
    case _SC_ARG_MAX:
      return ARG_MAX;
    case _SC_ATEXIT_MAX:
      return INT_MAX;
    case _SC_CHILD_MAX:
      return 0;
    case _SC_CLK_TCK:
      return 100;
    case _SC_DELAYTIMER_MAX:
      return -1;
    case _SC_HOST_NAME_MAX:
      return 255;
    case _SC_LOGIN_NAME_MAX:
      return 255;
    case _SC_NGROUPS_MAX:
      return 16;
    case _SC_MQ_OPEN_MAX:
    case _SC_MQ_PRIO_MAX:
      return -1;
    case _SC_OPEN_MAX:
      return -1;
    case _SC_PAGE_SIZE: // is also _SC_PAGESIZE
      return OS::page_size();
    case _SC_RTSIG_MAX:
    case _SC_SEM_NSEMS_MAX:
      return -1;
    case _SC_SEM_VALUE_MAX:
      return INT_MAX;
    case _SC_SIGQUEUE_MAX:
      return -1;
    case _SC_STREAM_MAX:  // See APUE 2.5.1
      return FOPEN_MAX;
    case _SC_TIMER_MAX:
      return -1;
    case _SC_TTY_NAME_MAX:
    case _SC_TZNAME_MAX:
      return 255;
    case _SC_ADVISORY_INFO:
    case _SC_BARRIERS:
    case _SC_ASYNCHRONOUS_IO:
    case _SC_CLOCK_SELECTION:
    case _SC_CPUTIME:
      return -1;
    case _SC_MEMLOCK:
    case _SC_MEMLOCK_RANGE:
    case _SC_MESSAGE_PASSING:
      return -1;
    case _SC_MONOTONIC_CLOCK:
      return 200809L;
    case _SC_PRIORITIZED_IO:
    case _SC_PRIORITY_SCHEDULING:
    case _SC_RAW_SOCKETS:
      return -1;
    case _SC_REALTIME_SIGNALS:
      return -1;
    case _SC_SAVED_IDS:
      return 1;
    case _SC_SEMAPHORES:
    case _SC_SHARED_MEMORY_OBJECTS:
      return -1;
    case _SC_SPAWN:
      return 0;
    case _SC_SPIN_LOCKS:
    case _SC_SPORADIC_SERVER:
    case _SC_SYNCHRONIZED_IO:
    case _SC_THREAD_CPUTIME:
    case _SC_THREAD_PRIO_INHERIT:
    case _SC_THREAD_PRIO_PROTECT:
    case _SC_THREAD_PRIORITY_SCHEDULING:
    case _SC_THREAD_SPORADIC_SERVER:
    case _SC_TIMEOUTS:
    case _SC_TIMERS:
    case _SC_TRACE:
    case _SC_TRACE_EVENT_FILTER:
    case _SC_TRACE_INHERIT:
    case _SC_TRACE_LOG:
    case _SC_TYPED_MEMORY_OBJECTS:
    case _SC_2_FORT_DEV:
    case _SC_2_PBS:
    case _SC_2_PBS_ACCOUNTING:
    case _SC_2_PBS_CHECKPOINT:
    case _SC_2_PBS_LOCATE:
    case _SC_2_PBS_MESSAGE:
    case _SC_2_PBS_TRACK:
    case _SC_XOPEN_REALTIME:
    case _SC_XOPEN_REALTIME_THREADS:
    case _SC_XOPEN_STREAMS:
      return -1;
    case _SC_XOPEN_UNIX:
      return 1;
    case _SC_XOPEN_VERSION:
      return 600;
    default:
      errno = EINVAL;
      return -1;
  }
}

uid_t getuid() {
  return 0;
}

gid_t getgid() {
  return 0;
}

long fpathconf(int fd, int name) {
  try {
    auto& fildes = FD_map::_get(fd);
    switch (name) {
    case _PC_FILESIZEBITS:
      return 64;
    case _PC_LINK_MAX:
      return -1;
    case _PC_NAME_MAX:
      return 255;
    case _PC_PATH_MAX:
      return 1024;
    case _PC_SYMLINK_MAX:
      return -1;
    case _PC_CHOWN_RESTRICTED:
      return -1;
    case _PC_NO_TRUNC:
      return -1;
    case _PC_VDISABLE:
      return -1;
    case _PC_ASYNC_IO:
      return 0;
    case _PC_SYNC_IO:
      return 0;
    default:
      errno = EINVAL;
      return -1;
    }
  }
  catch(const FD_not_found&) {
    errno = EBADF;
    return -1;
  }
}

long pathconf(const char *path, int name) {
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    errno = ENOENT;
    return -1;
  }
  long res = fpathconf(fd, name);
  close(fd);
  return res;
}

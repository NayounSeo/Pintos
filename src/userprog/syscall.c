#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <devices/shutdown.h>
#include <filesys/filesys.h>
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

void sys_halt (void);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
tid_t sys_exec (const char *cmd_line);
int sys_wait (tid_t tid);

int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);

void check_address(void *addr);
void get_argument(void *esp, int *arg, int count);

struct lock l;

/* See lib/syscall-nr.h */
void
syscall_init (void) 
{
  lock_init(&l);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  // printf ("system call!\n");
  /* slide 102 복붙 ^0^ */
  uint32_t *esp = f->esp;
  check_address ((void *) esp);
  int sys_no = *esp;
  int arg[4];

  esp++;

  switch (sys_no)
  {
    case SYS_HALT: {
      sys_halt ();
      break;
    }
    case SYS_EXIT: {
      // puts("SYS_EXIT");
      get_argument (esp, arg, 1);
      int status = * (int *) arg[0];
      sys_exit(status);
      break;
    }
    case SYS_EXEC: {
      // puts("SYS_EXEC");
      get_argument (esp, arg, 1);
      check_address ((void *) arg[0]);
      f->eax = sys_exec((const char *) arg[0]);
      break;
    }
    case SYS_WAIT: {
      // puts("SYS_WAIT");
      get_argument (esp, arg, 1);
      int tid = * (int *) arg[0];
      f->eax = sys_wait(tid);
      break;
    }
    case SYS_CREATE: {
      // puts("SYS_CREATE");
      get_argument(esp, arg, 2);
      check_address((void *) arg[0]);
      const char *f1 = (char *) arg[0];
      unsigned init_size = * (unsigned *) arg[1];
      f->eax = sys_create (f1, init_size);
      break;
    }
    case SYS_REMOVE: {
      // puts("SYS_REMOVE");
      get_argument(esp, arg, 1);
      check_address((void *) arg[0]);
      const char *f2 = (char *) arg[0];
      f->eax = sys_remove (f2);
      break;
    }
    case SYS_OPEN: {
      // puts("SYS_OPEN");
      get_argument (esp, arg, 1);
      check_address ((void *) arg[0]);
      f->eax = sys_open((const char *) arg[0]);
      break;
    }
    case SYS_FILESIZE: {
      // puts("SYS_FILESIZE");
      get_argument (esp, arg, 1);
      f->eax = sys_filesize((int) arg[0]);
      break;
    }
    case SYS_READ: {
      // puts("SYS_READ");
      get_argument (esp, arg, 3);
      check_address ((void *) arg[1]);
      f->eax = sys_read((int) arg[0], (void *) arg[1], (unsigned) arg[2]);
      break;
    }
    case SYS_WRITE: {
      // puts("SYS_WRITE");
      get_argument (esp, arg, 3);
      f->eax = sys_write((int) arg[0], (void *) arg[1], (unsigned) arg[2]);
      break;
    }
    case SYS_SEEK: {
      // puts("SYS_SEEK");
      get_argument (esp, arg, 2);
      sys_seek((int) arg[0], (unsigned) arg[1]);
      break;
    }
    case SYS_TELL: {
      // puts("SYS_TELL");
      get_argument (esp, arg, 1);
      f->eax = sys_tell((int) arg[0]);
      break;
    }
    case SYS_CLOSE: {
      // puts("SYS_CLOSE");
      get_argument (esp, arg, 1);
      sys_close((int) arg[0]);
      break;
    }
  }
  // thread_exit ();
}

void sys_halt(void)
{
  shutdown_power_off();
  return;
}

void sys_exit(int status)
{
  struct thread *cur = thread_current ();
  printf("%s: exit(%d)\n", cur->name, status);
  /* 프로세스 디스크립터에 exit status 저장*/
  cur->exit_status = status;
  thread_exit ();
}

bool sys_create(const char *file, unsigned initial_size)
{
  lock_acquire (&l);
  bool success = filesys_create(file, (off_t) initial_size);
  lock_release (&l);
  return success;
}

bool sys_remove(const char *file)
{
  lock_acquire (&l);
  bool success = filesys_remove(file);
  lock_release (&l);
  return success;
}

tid_t
sys_exec (const char *cmd_line)
{
  tid_t tid = process_execute (cmd_line);
  struct thread *p = get_child_process(tid);
  sema_down (&thread_current ()->load_sema);

  if (p->is_loaded == 0) {
    return -1;
  }
  return tid;
}

int
sys_wait (tid_t tid)
{
  /* 자식 프로세스가 종료 될 때까지 대기 */
  process_wait (tid);
}

// TODO : 여기서도 lock?
int
sys_open (const char *file)
{
  lock_acquire (&l);
  struct file *f = filesys_open (file);
  if (f == NULL) {
    lock_release (&l);
    return -1;
  }
  int fd = process_add_file (f);
  lock_release (&l);
  return fd;
}

int
sys_filesize (int fd)
{
  lock_acquire (&l);
  struct file *f = process_get_file (fd);
  if (f == NULL) {
    lock_release (&l);
    return -1;
  }
  int size = (int) file_length (f);
  lock_release (&l);
  return size;
}

int
sys_read (int fd, void *buffer, unsigned size)
{
  lock_acquire (&l);
  // puts("sys_read--------------||||--------------");

  struct file *f = process_get_file (fd);
  int bytes;
  int i;
  if (fd == 0) {
    /* 키보드에 입력 저장 후 버퍼에 저장된 크기를 반환 */
    for (i = 0; i < size; i++) {
      * (uint8_t *) buffer = input_getc ();
    }
    lock_release(&l);
    return (int) size;
  } else {
    if (f == NULL) {
      lock_release (&l);
      return -1;
    }
    /* 파일에 데이터를 크기만큼 저장 후 읽은 바이트 수를 리턴 */
    bytes = (int) file_read (f, buffer, size);
    lock_release (&l);
    return bytes;
  }
}

int
sys_write (int fd, void *buffer, unsigned size)
{
  lock_acquire (&l);
  // puts("sys_write--------------||||--------------");
  
  struct file *f = process_get_file (fd);
  if (fd == 1) {
    /* 버퍼에 저장된 값을 화면에 출력 */
    putbuf ((const char *)buffer, size);
    lock_release (&l);
    return size;
  } else {
    if (f == NULL) {
      lock_release (&l);
      return -1;
    }

    /* 버퍼에 저장된 데이터를 크기만큼 파일에 기록 후 기록한 바이트 수를 리턴 */
    off_t bytes = file_write (f, buffer, size);
    lock_release (&l);
    return bytes;
  }
}

void
sys_seek (int fd, unsigned position)
{
  lock_acquire (&l);
  struct file *f = process_get_file (fd);
  if (f == NULL) {
    lock_release (&l);
    return -1;
  }
  file_seek (f, (off_t) position);
  lock_release (&l);
  return;
}

unsigned
sys_tell (int fd)
{
  lock_acquire (&l);
  struct file *f = process_get_file (fd);
  if (f == NULL) {
    lock_release (&l);
    return -1;
  }
  unsigned pos = (unsigned) file_tell (f);
  lock_release (&l);
  return pos;
}

void
sys_close (int fd)
{
  lock_acquire (&l);
  process_close_file (fd);
  lock_release (&l);
  return;
}

void
check_address(void * addr)
{
  // printf("%p\n", addr);
  /* 포인터가 가리키는 주소가 유저영역의 주소인지 확인. 잘못된 접근일 시 ※프로세스 종료※ */
  if ((uint32_t) addr <= 0x8048000 || (uint32_t) addr >= 0xc0000000)
  {
    sys_exit(-1); // TODO : sys_exit이야 lib의 exit이야..?
  }
}

void
get_argument(void *esp, int *arg, int count)
{
  /* 유저 스택에 저장된 인자값들을 커널로 저장.
   * 인자가 저장된 위치가 유저영역인지 check_address () 로 확인 */
  int i;
  // for (i = count - 1; i > -1; i--) // TODO : ???? 역순인가 ㅠㅠ
  for (i = 0; i < count; i++) // TODO : ???? 역순인가 ㅠㅠ
  {
    check_address(esp);
    arg[i] = * (uint32_t *) esp;
    esp += 4;
  }
}
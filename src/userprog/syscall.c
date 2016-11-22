#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <devices/shutdown.h>
#include <threads/thread.h>
#include <filesys/filesys.h>

static void syscall_handler (struct intr_frame *);
void sys_halt (void);
void sys_exit (int status);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);

void check_address(void *addr);
void get_argument(void *esp, int *arg, int count);

/* See lib/syscall-nr.h */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* slide 102 복붙 ^0^ */
  void *esp = f -> esp;
  check_address (esp);
  int sys_no = * (uint32_t *) (esp);
  int arg[4];

  switch (sys_no)
  {
    case SYS_HALT: {
      sys_halt ();
      break;
    }
    case SYS_EXIT: {
      get_argument (esp, arg, 1);
      int status = * (int *) arg[0];
      sys_exit(status);
      break;
    }
    case SYS_EXEC: {
      /* 3장의 것을 일단 그대로 
      */
      // get_argument (esp, arg, 1);
      // check_address ((void *) arg[0]);
      // f->eax = exec((cons char *) arg[0]);
      break;
    }
    // case SYS_WAIT: {
    //   break;
    // }
    case SYS_CREATE: {
      get_argument(esp, arg, 2);
      check_address((void *) arg[0]);
      const char *f1 = (char *) arg[0];
      unsigned init_size = * (unsigned *) arg[1];
      f -> eax = sys_create (f1, init_size);
      break;
    }
    case SYS_REMOVE: {
      get_argument(esp, arg, 1);
      check_address((void *) arg[0]);
      const char *f2 = (char *) arg[0];
      f -> eax = sys_remove (f2);
      break;
    }
  }
  printf ("system call!\n");
  thread_exit ();
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
  thread_exit ();
  return;
}

bool sys_create(const char *file, unsigned initial_size)
{
  return filesys_create(file, (off_t) initial_size);
}

bool sys_remove(const char *file)
{
  return filesys_remove(file);
}

void check_address(void * addr)
{
  /* 포인터가 가리키는 주소가 유저영역의 주소인지 확인. 잘못된 접근일 시 ※프로세스 종료※ */
  if (* (uint32_t *) addr <= 0x08048000 || * (uint32_t *) addr >= 0xc0000000)
  {
    sys_exit(-1); // TODO : sys_exit이야 lib의 exit이야..?
  }
  return;
}

void get_argument(void *esp, int *arg, int count)
{
  /* 유저 스택에 저장된 인자값들을 커널로 저장.
   * 인자가 저장된 위치가 유저영역인지 check_address () 로 확인 */
  int i;
  for (i = count - 1; i > -1; i--) // TODO : ???? 역순인가 ㅠㅠ
  {
    esp += 4;
    arg[i] = * (uint32_t *) esp;
    check_address(esp);
  }
}
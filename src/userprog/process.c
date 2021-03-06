#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

void argument_stack (char **parse, int count, void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  char *fn_copy2;
  char *temp;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  fn_copy2 = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  if (fn_copy == NULL)
    return TID_ERROR;

  /* fn_copy, filed_name_2에 file_name 복사 */
  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (fn_copy2, file_name, PGSIZE);
  /* file_name 문자열을 파싱
   * 첫번째 토큰을 thread_create() 함수에 스레드 이름으로 전달 */
  const char * thread_name = strtok_r (fn_copy2, " ", &temp);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (thread_name, PRI_DEFAULT, start_process, fn_copy);

  palloc_free_page (fn_copy2);
  
  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy);
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  /* 추가한 인자들 */
  char **parse;
  char *temp;
  char *a;
  int count = 0;

  /* 실행파일 이름을 load의 첫 인자로 전달*/
  parse = (char **) malloc (sizeof(char *) * 1);
  temp = strtok_r (file_name, " ", &a);
  parse[count] = (char *) malloc (sizeof(char) * strlen(temp));
  strlcpy (parse[count], temp, strlen (temp) + 1);

  /* 인자들을 토큰화 및 토큰의 개수 계산 */
  while (file_name != NULL)
    {
      // A : parse[count]에는 할당을 해주어야 했다. 복사하니까 당연한가
      count += 1;
      temp = strtok_r (NULL, " ", &a);
      if (temp == NULL) {
        break;
      }
      parse = (char **) realloc (parse, sizeof(char *) * (count + 1));
      parse[count] = (char *) malloc (sizeof(char) * strlen(temp));
      strlcpy (parse[count], temp, strlen (temp) + 1);
    }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (parse[0], &if_.eip, &if_.esp);

  // printf("success = %d\n", success);
  /* 적재 (load) 성공 시 부모 프로세스 다시 진행 */

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) {
    /* TODO 메모리 적재 실패 시 프로세스 디스크립터에 메모리 적재 실패 */
    thread_current ()-> is_loaded = 0;
    sema_up(&thread_current ()->load_sema); // TODO : 이래도 잘 들어가나..?
    thread_exit ();
  }
  /* TODO 메모리 적재 성공 시 프로세스 디스크립터에 메모리 적재 성공 */
  thread_current ()-> is_loaded = 1;
  sema_up(&thread_current ()->load_sema); // TODO : 이래도 잘 들어가나..?

  /* 토큰화된 인자들을 스택에 저장 */
  argument_stack(parse, count, &if_.esp);

  /* 썼으면 반납! */
  int i;
  for (i = 0; i < count; i++)
    {
      free(parse[i]);
    }
  free(parse);

  /* 중간 점검 */
  // hex_dump (if_.esp, if_.esp, PHYS_BASE - if_.esp, true);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* 유저 스택에 파싱된 토큰을 저장하는 함수 구현 */
void
argument_stack (char **parse, int count, void **esp)
  {
    int i;
    // 지난번에 고생했던 것 : 스택에 저장한 주소를 스택에 다시 넣어줘야 하기 때문에! *
    uint32_t argv_addr[count];

    for (i = count -1; i > -1; i--)
      {
        *esp -= (strlen(parse[i]) + 1);  //NULL문자 자리까지 스택에 추가해주기. 포인터 연산
        memcpy (*esp, parse[i], strlen (parse[i]) + 1); // 메모리 복사
        argv_addr[i] = (uint32_t) (*esp);  // 방금 쓰인 스택의 주소를 임시 저장
      }

    /* 워드 사이즈 (4바이트)로 정렬해주기.
     * 정렬 후에는 포인터 연산시 4바이트씩 연산 */
    *esp = (uint32_t) (*esp) & 0xfffffffc;
    *esp -= 4;
    memset (*esp, 0, sizeof (uint32_t)); // 프린트에서 uint8_t라고 되어있는 바로 그곳..

    for (i = count - 1; i > -1; i--)
      {
        *esp -= 4;  // 리마인드~ *esp는 지금 포인터야!
        // *esp라는 포인터를 (uint32_t *)로 선언해주고 다시 그 값에 해당하는 것을 대입.
        // Q: esp는 왜 이중포인터로 넘어온거지..?
        * (uint32_t *) (*esp) = argv_addr[i];
      }

    *esp -= 4;
    // * (uint32_t *) (*esp) = argv_addr[0];
    * (uint32_t *) (*esp) = (uint32_t) (*esp) + 4; //닮이

    *esp -= 4;
    * (uint32_t *) (*esp) = (uint32_t) count;

    *esp -= 4;
    memset (*esp, 0, sizeof (uint32_t)); //가짜 반환 주소까지 스택에 넣어주기.
  }

struct thread *
get_child_process (int pid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
    {
      struct thread *t = list_entry(e, struct thread, child);
      if (t->tid == pid)
        {
          return t;
        }
    }
  return NULL;
}

void
remove_child_process (struct thread *p)
{
  list_remove(&p->child);
  palloc_free_page ((void *) p);
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2. */
int
process_wait (tid_t child_tid) 
{
  struct thread *child = get_child_process(child_tid);
  if (child == NULL) {
    return -1;
  }
  sema_down(&child->exit_sema);
  // int exit_status = child->status;
  int exit_status = child->exit_status;
  remove_child_process(child);

  return exit_status;
}

int
process_add_file (struct file *f)
{
  struct thread *cur =  thread_current ();
  /* 16-11-30 NULL 처리 추가 */
  if (f == NULL) {
    return -1;
  }
  // cur->fd_table = (struct file **) realloc (cur->fd_table, 
                               // sizeof(struct file *) * (cur->fd_size + 1));
  // TODO : 여기에 동적 할당이 필요할까? 이미 파일에 할당되어 온 후일 것 같다
  cur->fd_table[cur->fd_size] = f;
  return cur->fd_size++;
}

struct file *
process_get_file (int fd)
{
  /* 16-11-30 NULL 처리 추가 */
  if (fd < 0) {
    return NULL;
  }
  struct file *f = thread_current ()->fd_table[fd];
  return f;  // 없을 시 NULL 리턴
}

void process_close_file (int fd)
{
  /* 16-11-30 NULL 처리 추가 */
  if (fd < 0) {
    return NULL;
  }
  struct file *f = process_get_file(fd);
  file_close(f);
  thread_current ()->fd_table[fd] = NULL;
  /* 16-11-30 발견..! */
  // thread_current ()->fd_size -= 1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  uint32_t fd; 
  
  /* 프로세스에 열린 모든 파일을 닫음 */
  for (fd = 2; fd < cur->fd_size; fd++) {
      process_close_file(fd);
  }

  /* 실행 중인 파일 close */
  file_close(cur->run_file);

  /* 파일 디스크립터 메모리 테이블 해제 */
  palloc_free_page ((void *) cur->fd_table);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;

  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  struct lock l;
  lock_init(&l);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();


  lock_acquire (&l);  // lock 획득
  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      lock_release (&l);  // lock 해제 
      goto done; 
    }

    /* thread 구조체의 run_file을 현재 실행할 파일로 초기화 */
  t->run_file = file;
  /* file_deny_write()를 이용하여 파일에 대한 write를 거부 */
  file_deny_write(file);
  lock_release (&l);  // lock 해제 

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  // file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

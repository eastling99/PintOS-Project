#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* My Code (start) */
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <list.h>
/* My Code (end) */

typedef int pid_t; // Process id. (mapped 1:1 to tid)

// Handler, vectors.
typedef int (*handler) (uint32_t, uint32_t, uint32_t); // Handler that takes 3 unsigned int values.
static handler vector_syscall[128]; // System call vectors.

// File ID structure.
struct fid_elem
{
  int fid; // File ID (file descriptors) commonly used as fid or fd.
  struct file *file;
  struct list_elem elem;
  struct list_elem thread_elem;
};

// File list.
static struct list file_list;
static struct lock file_lock;

// System call handler.
static void syscall_handler (struct intr_frame *);

// System call functions.
static int sys_halt (void);
static int sys_exec (const char *cmd);
static int sys_wait (pid_t pid);
static int sys_create (const char *file, unsigned initial_size);
static int sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fid);
static int sys_read (int fid, void *buffer, unsigned size);
static int sys_write (int fid, const void *buffer, unsigned length);
static int sys_seek (int fid, unsigned pos);
static int sys_tell (int fid);
static int sys_close (int fid);

// Fid allocator.
static int fid_alloc (void);

static struct file *fid_get_file (int fid);
static struct fid_elem *fid_get_fid_elem (int fid);
static struct fid_elem *proc_fid_get_fid_elem (int fid);

// Initialization of system calls.
void
syscall_init (void) 
{
  // Register internal interrupt 0x30 to invoke syscall_handler.
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

/* My Code (start) */
  // Use syscall vectors with syscall numbers.
  vector_syscall[SYS_EXIT] = (handler)sys_exit;
  vector_syscall[SYS_HALT] = (handler)sys_halt;
  vector_syscall[SYS_CREATE] = (handler)sys_create;
  vector_syscall[SYS_OPEN] = (handler)sys_open;
  vector_syscall[SYS_CLOSE] = (handler)sys_close;
  vector_syscall[SYS_READ] = (handler)sys_read;
  vector_syscall[SYS_WRITE] = (handler)sys_write;
  vector_syscall[SYS_EXEC] = (handler)sys_exec;
  vector_syscall[SYS_WAIT] = (handler)sys_wait;
  vector_syscall[SYS_FILESIZE] = (handler)sys_filesize;
  vector_syscall[SYS_SEEK] = (handler)sys_seek;
  vector_syscall[SYS_TELL] = (handler)sys_tell;
  vector_syscall[SYS_REMOVE] = (handler)sys_remove;

  // initialize file lock and list.
  lock_init (&file_lock);
  list_init (&file_list);
  /* My Code (end) */
}

// System Call Handler (Gets called whenever there is a syscall.)
static void
syscall_handler (struct intr_frame *f) 
{
  /* My Code (start) */
  int *i;
  i = f->esp; // initilize i as intr_frame esp. ({i+n : n=>0} will get arguments above the stack)

  handler handle;
  handle = vector_syscall[*i]; // set handler to vector syscall

  // Validate and force exit if compromised.
  if (!is_user_vaddr (i) || *i > SYS_INUMBER || *i < SYS_HALT)
  {
    goto force_exit;
  }

  // If argument's virtual address is compromised (If any one of them is compromised, then exits)
  if (!(is_user_vaddr (i + 1) && is_user_vaddr (i + 2) && is_user_vaddr (i + 3)))
  {
    goto force_exit;
  }

  f->eax = handle (*(i + 1), *(i + 2), *(i + 3)); // save arguments to handler. // save to eax.

  return;

// Force exit
force_exit:
  sys_exit (-1);
  /* My Code (end) */
}

// Syscall to Exit thread.
int
sys_exit (int status)
{
  struct thread *t_curr;
  struct list_elem *x;

  t_curr = thread_current ();

  /* Go through the list of opened files under currently running thread.
  and exit close all the files. */
  while (list_empty (&t_curr->opened_files) == false) // Run while loop until list is empty. 
  {
    x = list_begin (&t_curr->opened_files);
    sys_close (list_entry(x, struct fid_elem, thread_elem)->fid); // close file using sys_close.
  }

  t_curr->exit_status = status; // Set t_curr's exit status.
  thread_exit (); // Exit thread.
  return -1;
}

// Syscall to halt (Shutdown) system.
static int
sys_halt (void)
{
  shutdown_power_off ();
}

// Syscall to Create file.
static int
sys_create (const char *file, unsigned initial_size)
{
  if (!file)
  {
    return sys_exit (-1);
  }
  return filesys_create (file, initial_size); // Create file with initial_size.
}
 
// Syscall to open file.
static int
sys_open (const char *file)
{
  struct file *f;
  struct fid_elem *x;
  int result = -1;

  // Check error (Check if file is NULL)
  if (!file)
  {
    return -1; // cannot open if NULL.
  }
  if (!is_user_vaddr (file))
  {
    sys_exit (-1);
  }

  // Open file after 
  f = filesys_open (file); // open and get file (file name = (*)file).

  if (!f) // Check for error.
  {
    goto finished;
  }

  // Allocate mem for x.
  x = (struct fid_elem *) malloc(sizeof (struct fid_elem));

  // Check for error (Check if there's enough mem.)
  if (!x)
  {
    file_close (f); // Close file.
    goto finished;
  }

  // x's struct is later used for other syscalls (e.g. matching fid)
  // set (struct file_elem) x's variables.
  x->file = f;
  x->fid = fid_alloc (); // allocate fid.

  // Insert element at the back of the list.
  list_push_back (&thread_current ()->opened_files, &x->thread_elem); // for thread
  list_push_back (&file_list, &x->elem); // for file

  result = x->fid; // save x->fid to return.

// Finsh sequence.
finished:
  return result;
}

// Syscall to close file.
static int
sys_close(int fid)
{
  struct fid_elem *x;

  x = proc_fid_get_fid_elem (fid);  // get file from current process using fid.

  if (!x)  // Check for error
  {
    goto finished;
  }

  file_close (x->file);  // Close file.

  /* Remove from list. */
  list_remove (&x->thread_elem);  // for thread
  list_remove (&x->elem);  // for file

  free (x); // free x's malloc

finished:
  return 0;
}

// Syscall to read file. (Structure is very similiar to sys_write)
static int
sys_read (int fid, void *buf, unsigned size)
{
  struct file * f;
  int result = -1;
  unsigned n;
  uint8_t *converted_ptr;

  lock_acquire (&file_lock); // Acquire lock for file

  // Need to take action for STDIN unlike sys_write.
  // If file descriptor is STDIN_FILENO, use input_getc()
  if (fid == STDIN_FILENO)
  {
    // loop until n = size. (Retrieve input for size amount of time.)
    for (n = 0; n != size; ++n) 
    {
      converted_ptr = (uint8_t *)(buf + n);
      *converted_ptr = input_getc(); // Use input_getc(). (Retieves a key from input buffer.)
    }

    result = size;  // Save size to result.
    goto finished;
  }

  // If file descriptor is STDOUT_FILENO 
  else if (fid == STDOUT_FILENO)
  {
      goto finished;
  }
  // Check if the buffer space is valid.
  else if (!is_user_vaddr (buf))
  {
    lock_release (&file_lock);
    sys_exit (-1);  // Exit.
  }
  // Check if there's enough buffer.
  else if (!is_user_vaddr (buf + size))
  {
    lock_release (&file_lock);  // Release file lock.
    sys_exit (-1);  // Exit.
  }
  // Read file.
  else
  {
    f = fid_get_file (fid); // Get file using fid.

    if (!f) // Check error.
    {
      goto finished;
    }

    result = file_read (f, buf, size); // Read file usinf file_read.
  }

finished:    
  lock_release (&file_lock); // Release file lock.
  return result;
}

// Syscall to write file.
static int
sys_write (int fid, const void *buf, unsigned size)
{
  struct file * f;
  int result = -1;

  lock_acquire (&file_lock); // Lock file.

  // If file descriptor is STDOUT_FILENO use pubuf
  if (fid == STDOUT_FILENO)
  {
    putbuf (buf, size); // Write 'size' amount of char to buffer (buf).
  }
  // If file descriptor is STDIN_FILENO, finish sequence.
  else if (fid == STDIN_FILENO)
  {
    goto finished;
  }
  // Check for virtual mem address buf, buf + size
  else if (!is_user_vaddr (buf))
  {
    lock_release (&file_lock); // Release file lock.
    sys_exit (-1);
  }
  else if (!is_user_vaddr (buf + size))
  {
    lock_release (&file_lock); // Release file lock.
    sys_exit (-1);
  }
  // If situation is none of the above, get file by fd and write 
  else
  {
    f = fid_get_file (fid); // get file using fid and save to f.

    if (!f)    // Check for error
    {
      goto finished;
    }

    result = file_write (f, buf, size); // Write size bytes from buf to file 'f'.
  }
		
finished:
  lock_release (&file_lock); // Release file lock.
  return result;
}

// Syscall to execute process.
static int
sys_exec (const char *cmd)
{
  int result = -1;

  // Check Vailidity.
  if (!cmd)
  {
    return -1;
  }
  if (!is_user_vaddr(cmd))
  {
    return -1;
  }
  /* lock file while process_execute. */
  lock_acquire (&file_lock);
  result = process_execute (cmd); // execute process.
  lock_release (&file_lock);

  return result;
}

// Syscall to execute process.
static int
sys_wait (pid_t pid)
{
  return process_wait (pid);  // return to activate process_wait
}

// Get file size.
static int
sys_filesize (int fid)
{
  struct file *f;

  f = fid_get_file (fid); // get file from fid and save.
  if (!f) // Check errors.
  {
    return -1;
  }
  return file_length (f);
}

// Return file position.
static int
sys_tell (int fid)
{
  struct file *f;

  f = fid_get_file (fid); // get file from fid and save.
  if (!f) // Check errors.
  {
    return -1;
  }

  return file_tell (f); // reutrn current position of the file.
}

//Seeks to a specified position in a file using an offset specified from the start of the file.
static int
sys_seek (int fid, unsigned pos)
{
  struct file *f;

  f = fid_get_file (fid);
  if (!f) // Check errors.
  {
    return -1;
  }
  file_seek (f, pos);
  return 0; /* Not used */
}

// Delete a file.
static int
sys_remove (const char *file)
{
  /* Check for errors and return false or exit */
  if (!file)
  {
    return false;
  }

  if (!is_user_vaddr (file))
  {
    sys_exit (-1);
  }

  return filesys_remove (file); // Delete a file.
}

// Allocate fid. (used in system_open.)
static int
fid_alloc (void)
{
  static int fid = 2;
  return fid++;
}

// Get file using fid.
static struct file *
fid_get_file (int fid)
{
  struct fid_elem *get;

  get = fid_get_fid_elem (fid);
  if (!get)
  {
    return NULL;
  }
  return get->file;
}

// Get fid_elem using fid.
static struct fid_elem *
fid_get_fid_elem (int fid)
{
  struct fid_elem *get;
  struct list_elem *x;

  for (x = list_begin (&file_list); x != list_end (&file_list); x = list_next (x))
  {
    get = list_entry (x, struct fid_elem, elem);
    if (get->fid == fid)
      return get;
  }
  return NULL;
}

// Get file from opened_files that is used by running thread.
static struct fid_elem *
proc_fid_get_fid_elem (int fid)
{
  struct fid_elem *get;
  struct list_elem *e;

  struct thread *t_curr;
  t_curr = thread_current (); // save current thread on local struct t_curr.

  // Go through opened_files list and find 
  for (e = list_begin (&t_curr->opened_files); e != list_end (&t_curr->opened_files); e = list_next (e))
  {
    get = list_entry (e, struct fid_elem, thread_elem);

    if (get->fid == fid) // If file id matches.
    {
      return get; // return file.
    }
  }
  // If nothing is matched.
  return NULL;
}

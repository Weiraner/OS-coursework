#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "wmap.h"
#include "file.h"
#include "reflock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

	// Initialize the wmap info and mapping table
	p->info.total_mmaps = 0;
	for(int i = 0; i < MAX_WMMAP_INFO; i++){
		p->info.addr[i] = 0;
		p->info.length[i] = 0;
		p->info.n_loaded_pages[i] = 0;
		// Initialize the mappings
		// For each time, record all information for lazy allocate
		// Because each time we need at least 1 page
		// at most 16 allocated memory!
		p->mappings[i].addr = 0;
		p->mappings[i].length = 0;
		p->mappings[i].flags = 0;
		p->mappings[i].fd = 0;
		p->mappings[i].status = 0;
		p->mappings[i].f = 0;
        }

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

//	np->info.total_mmaps = curproc->info.total_mmaps;

	memmove(np->mappings, curproc->mappings, sizeof(curproc->mappings));

//	memmove(np->info, curproc->info, sizeof(curproc->info));

	for(int i = 0; i < MAX_WMMAP_INFO; i++){
		np->info.addr[i] = curproc->info.addr[i];
		np->info.length[i] = curproc->info.length[i];
		np->info.n_loaded_pages[i] = curproc->info.n_loaded_pages[i];
//		np->mappings[i].addr = curproc->mappings[i].addr;
//		np->mappings[i].length = curproc->mappings[i].length;
//		np->mappings[i].flags = curproc->mappings[i].flags;
//		np->mappings[i].fd = curproc->mappings[i].fd;
//		np->mappings[i].status = curproc->mappings[i].status;
//		np->mappings[i].f = curproc->mappings[i].f;
		if(np->mappings[i].status == 1 && !(np->mappings[i].flags & MAP_ANONYMOUS) && np->mappings[i].f){
			filedup(np->mappings[i].f);
		}
	}
	for(int i = 0; i < MAX_WMMAP_INFO; i++){
		if(np->mappings[i].status == 1){
			uint addr = np->mappings[i].addr;
			int length = np->mappings[i].length;
			uint a = PGROUNDDOWN(addr);
			uint last = PGROUNDDOWN(addr + length - 1);
			for(; a <= last; a += PGSIZE){
				pte_t *pte_parent = walkpgdir(curproc->pgdir, (char*)a, 0);
				pte_t *pte_child = walkpgdir(np->pgdir, (char*)a, 1);

				if(!pte_child){
						panic("fork: walkpgdir failed for child");
				}
				if(!pte_parent){
					*pte_child = 0;
					continue;
				}
				if(*pte_parent & PTE_P){
					uint pa = PTE_ADDR(*pte_parent);
					uint flags = PTE_FLAGS(*pte_parent);
					if(np->mappings[i].flags & MAP_SHARED){
						*pte_child = pa | flags;
					} else {
						if(flags & PTE_W){
							flags &= ~PTE_W;
							flags |= PTE_COW;
							*pte_parent &= ~PTE_W;
							*pte_parent |= PTE_COW;
						}
						*pte_child = pa | flags;
					}
					acquire(&reflock.lock);
					reflock.ref_count[pa / PGSIZE]++;
					release(&reflock.lock);
				} else {
					*pte_child = PTE_FLAGS(*pte_parent) & ~PTE_P;
				}
			}
		}
	}

  pid = np->pid;

	for(i = 0; i < curproc->sz; i += PGSIZE){
		pte_t *pte = walkpgdir(curproc->pgdir, (void*)i, 0);
		if(pte == 0){
			continue;
		}
		if(!(*pte & PTE_P)){
			continue;
		}
		if(*pte & PTE_W){
			uint pa = PTE_ADDR(*pte);
			uint flags = PTE_FLAGS(*pte);
			flags &= ~PTE_W;
			flags |= PTE_COW;

			*pte = pa | flags;
			pte_t *pte_child = walkpgdir(np->pgdir, (void*)i, 1);
			if(pte_child == 0){
				panic("fork: child walkpgdir failed");
			}

			*pte_child = pa | flags;

			acquire(&reflock.lock);
			reflock.ref_count[pa / PGSIZE]++;
			release(&reflock.lock);
		} else {
			uint pa = PTE_ADDR(*pte);
			pte_t *pte_child = walkpgdir(np->pgdir, (void*)i, 1);
			if(pte_child == 0){
				panic("fork: child walpgdir failed");
			}
			*pte_child = *pte;
			acquire(&reflock.lock);
			reflock.ref_count[pa / PGSIZE]++;
			release(&reflock.lock);
		}
	}

	lcr3(V2P(curproc->pgdir));
//	lcr3(V2P(np->pgdir));

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

	// clear all allocated memory
	for (int i = 0; i < MAX_WMMAP_INFO; i++) {
		if (curproc->mappings[i].status == 1) {
			wunmap(curproc->mappings[i].addr);
		}
	}


  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// Implement of wmap
uint
wmap(uint addr, int length, int flags, int fd){
	// addr, length, flags should all be valid
	// Otherwise it won't be called
	struct proc *p = myproc();
	// acquire a lock to change the ptable

	if(flags & MAP_ANONYMOUS){
		fd = -1;
	} else {
		if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0) {
			cprintf("Error: Invalid file descriptor \n");
			return FAILED;
		}
	}

	uint new_start = addr;
	uint new_end = addr + length;

	acquire(&ptable.lock);

	for(int i = 0; i < MAX_WMMAP_INFO; i++){
		if(p->mappings[i].status == 1) {
			uint existing_start = p->mappings[i].addr;
			uint existing_end = existing_start + p->mappings[i].length;

			if(!(new_end <= existing_start || new_start >= existing_end)) {
				cprintf("Error: Overlapping map detected (new: 0x%x-0x%x, existing: 0x%x-0x%x)\n", 
                        new_start, new_end, existing_start, existing_end);
				release(&ptable.lock);
				return FAILED;
			}
		}
	}

	for(int i = 0; i < MAX_WMMAP_INFO; i++){
		if(p->mappings[i].status != 1){
			// If there's enough space
			p->mappings[i].addr = addr;
			p->mappings[i].length = length;
			p->mappings[i].flags = flags;
			p->mappings[i].fd = fd;
			p->mappings[i].status = 1;
			if(!(p->mappings[i].flags & MAP_ANONYMOUS)){
				// file backed
				if(p->mappings[i].fd < 0) {
					return FAILED;
				}
				struct file *f = p->ofile[fd];
				if(!f){
					return FAILED;
				}
				p->mappings[i].f = f;
				filedup(f);
			}

			p->info.total_mmaps++;
			p->info.addr[i] = addr;
			p->info.length[i] = length;
			release(&ptable.lock);
			return addr;
		}
	}
	release(&ptable.lock);
	cprintf("Error: No available mapping slots\n");
	return FAILED;// If nothing to use, failed!

}

int
wunmap(uint addr){
	struct proc *p = myproc();

	if(addr % PGSIZE != 0){
		return -1;
	}
	for(int i = 0; i < MAX_WMMAP_INFO; i++){
		if(p->mappings[i].status == 1 && p->mappings[i].addr == addr){
			int num_pages = (p->mappings[i].length + PGSIZE - 1) / PGSIZE;
			int is_anonymous = (p->mappings[i].flags & MAP_ANONYMOUS);
			int fd = p->mappings[i].fd;
			if(!is_anonymous && fd >= 0 && fd < NOFILE){
				if(!p->mappings[i].f){
					return FAILED;
				}
			}
			for(int j = 0; j < num_pages; j++){
				uint page_addr = addr + j * PGSIZE;
				pte_t *pte = walkpgdir(p->pgdir, (void*)page_addr, 0);
				if(pte && (*pte & PTE_P)){
					uint phys_addr = PTE_ADDR(*pte);

					if(p->mappings[i].f){
						p->mappings[i].f->off = j * PGSIZE;
						filewrite(p->mappings[i].f, (char *)P2V(phys_addr), PGSIZE);
					}

//					kfree(P2V(phys_addr));
					*pte = 0;
				}
			}

			lcr3(V2P(p->pgdir));

			p->mappings[i].status = 0;
			p->info.n_loaded_pages[i] = 0;
			//if(!(p->mappings[i].flags & MAP_ANONYMOUS)){
			//	fileclose(p->mappings[i].f);
			//}
			return SUCCESS;
		}
	}
	return FAILED;
}

uint va2pa(uint va) {
    struct proc *p = myproc();
    pde_t *pgdir = p->pgdir;
    pte_t *pte = walkpgdir(pgdir, (void*)va, 0);
    if (pte == 0 || !(*pte & PTE_P)) {
        return -1;
    }
    uint pa = PTE_ADDR(*pte) | (va & 0xFFF);
    return pa;
}

int getwmapinfo(struct wmapinfo *wminfo) {
    struct proc *p = myproc();
    int count = 0;

    for (int i = 0; i < MAX_WMMAP_INFO; i++) {
        if (p->mappings[i].status == 1) {
            wminfo->addr[count] = p->mappings[i].addr;
            wminfo->length[count] = p->mappings[i].length;
            int n_pages = 0;
            for (int j = 0; j < p->mappings[i].length; j += PGSIZE) {
                pte_t *pte = walkpgdir(p->pgdir, (void*)(p->mappings[i].addr + j), 0);
                if (pte && (*pte & PTE_P)) {
                    n_pages++;
                }
            }
            wminfo->n_loaded_pages[count] = n_pages;
            count++;
        }
    }

    wminfo->total_mmaps = count;
    return 0;
}








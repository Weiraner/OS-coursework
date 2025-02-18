#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "defs.h"
#include "file.h"
#include "reflock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
	case T_PGFLT:{
		// Get the address where page fault occurs
		uint fault_addr = rcr2();
		struct proc *p = myproc();
		pde_t *pgdir = p->pgdir;
		pte_t *pte = walkpgdir(pgdir, (void *)fault_addr, 0);
		if(!pte || !(*pte & PTE_P)){
			//TODO: Don't know if need lock or not
			for(int i = 0; i < MAX_WMMAP_INFO; i++){
				if(p->mappings[i].status == 1 &&
				fault_addr >= p->mappings[i].addr &&
				fault_addr < p->mappings[i].addr + p->mappings[i].length){
					// If the address is in a allocated range
					// Allocate a physical page
					uint aligned_addr = PGROUNDDOWN(fault_addr);
					pte_t *pte = walkpgdir(pgdir, (void *)aligned_addr, 0);
					if(pte && (*pte & PTE_P)){
						return;
					}

					char *mem = kalloc();
					if(mem == 0){
						cprintf("Lazy allocation failed: out of memory\n");
						p->killed = 1;
						return;
					}

					if(!(p->mappings[i].flags & MAP_ANONYMOUS)){
						if(p->mappings[i].f){
							p->mappings[i].f->off = aligned_addr - p->mappings[i].addr;
							fileread(p->mappings[i].f, mem, PGSIZE);
							//if(fileread(f, mem, PGSIZE) != PGSIZE){

							//	cprintf("File read error during page fault");
							//	kfree(mem);
							//	p->killed = 1;
							//	return;
							//}
						}
					} else {
						memset(mem, 0, PGSIZE);
					}

					if(mappages(pgdir, (void *)aligned_addr, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0){
						cprintf("Mapping failed\n");
						kfree(mem);
						p->killed = 1;
						return;
					}
					p->info.n_loaded_pages[i] += 1;
					return;
				}
			}
 		}else if(*pte & PTE_COW){
			uint pa = PTE_ADDR(*pte);
			acquire(&reflock.lock);
			if(reflock.ref_count[pa / PGSIZE] > 1){
				reflock.ref_count[pa / PGSIZE]--;
				char *mem = kalloc();
				if(mem == 0){
					panic("Segmentation fault");
				}
				memmove(mem, (char *)P2V(pa), PGSIZE);
				*pte = V2P(mem) | PTE_FLAGS(*pte);
				reflock.ref_count[V2P(mem) / PGSIZE] = 1;
			}
			release(&reflock.lock);

			*pte |= PTE_W;
			*pte &= ~PTE_COW;
			lcr3(V2P(p->pgdir));
			return;
		} else {
			cprintf("Segmentation Fault");
			p->killed = 1;
			break;
		}
	}
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}

// defs.h 或 reflock.h
#ifndef REFLOCK_H
#define REFLOCK_H
#include "mmu.h"
#include "spinlock.h"
#include "memlayout.h"
struct reflock_t {
  struct spinlock lock;
  uint ref_count[PHYSTOP / PGSIZE];
};

extern struct reflock_t reflock;

#endif

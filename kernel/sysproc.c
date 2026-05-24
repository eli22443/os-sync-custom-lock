#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// LCG state and spinlock protecting it
static uint32 lcg_state = 1; // default initial state
static struct spinlock lcg_lock;

#define MAX_WAITING 16
#define LOCK_COUNT 15

#define MAX_TEAMS 10

static int team_scores[MAX_TEAMS];
static struct spinlock score_lock;

// called from main() during kernel boot
void
score_init(void)
{
  initlock(&score_lock, "score_lock");
  for(int i = 0; i < MAX_TEAMS; i++) {
    team_scores[i] = 0;
  }
}

// reset scores
uint64
sys_reset_team_scores(void)
{
  acquire(&score_lock);
  for(int i = 0; i < MAX_TEAMS; i++) {
    team_scores[i] = 0;
  }
  release(&score_lock);
  return 0;
}

// system call: increment a team's score by 1 and return the new score
uint64
sys_increment_team_score(void)
{
  int team_id;
  argint(0, &team_id);
  
  if(team_id < 0 || team_id >= MAX_TEAMS)
    return -1;
    
  int new_score;
  acquire(&score_lock);
  team_scores[team_id]++;
  new_score = team_scores[team_id];
  release(&score_lock);
  
  return new_score;
}

// system call: get a team's current score (for end-condition checks)
uint64
sys_get_team_score(void)
{
  int team_id;
  argint(0, &team_id);
  
  if(team_id < 0 || team_id >= MAX_TEAMS)
    return -1;
    
  int score;
  acquire(&score_lock);
  score = team_scores[team_id];
  release(&score_lock);
  
  return score;
}

struct israeli_lock {
  struct spinlock lk;        // internal lock protecting this structure
  int active;                 // whether the lock was created and is active
  int favoritism;             // protection factor (0-100)
  int held;                   // whether the lock is currently held (1 or 0)
  struct proc* queue[MAX_WAITING]; // FIFO queue of waiting processes
  int queue_size;             // number of processes currently in the queue
};

static struct israeli_lock locks[LOCK_COUNT]; // global array of locks

// initialize the array during kernel boot
void
israeli_lock_init(void)
{
  for(int i = 0; i < LOCK_COUNT; i++) {
    initlock(&locks[i].lk, "israeli_lock");
    locks[i].active = 0;  
    locks[i].held = 0;
    locks[i].queue_size = 0;
  }
}

// system call: setgid
uint64
sys_setgid(void)
{
  int gid;
  argint(0, &gid);
  struct proc *p = myproc();
  p->gid = gid; 
  return 0;
}

// system call: getgid
uint64
sys_getgid(void)
{
  return myproc()->gid;
}

// system call: israeli_create
uint64
sys_israeli_create(void)
{
  int favoritism;
  argint(0, &favoritism);

  if (favoritism < 0 || favoritism > 100)
    return -1; 

  for(int i = 0; i < LOCK_COUNT; i++) {
    acquire(&locks[i].lk);
    if(!locks[i].active) {
      locks[i].active = 1; 
      locks[i].favoritism = favoritism; 
      locks[i].held = 0;
      locks[i].queue_size = 0;
      release(&locks[i].lk);
      return i; // lock id is its index in the array
    }
    release(&locks[i].lk);
  }
  return -1; 
}

// system call: israeli_acquire
uint64
sys_israeli_acquire(void)
{
  int lock_id;
  argint(0, &lock_id);

  if(lock_id < 0 || lock_id >= LOCK_COUNT)
    return -1;

  struct israeli_lock *l = &locks[lock_id];
  struct proc *p = myproc();

  acquire(&l->lk);
  if(!l->active) {
    release(&l->lk);
    return -1; 
  }

  // if lock is held or others are waiting ahead of us (preserves FIFO order)
  if(l->held || l->queue_size > 0) {
    if(l->queue_size >= MAX_WAITING) {
      release(&l->lk);
      return -1; // queue is full
    }
    // add process to end of queue
    l->queue[l->queue_size++] = p;

    // sleep until lock is free and this process is chosen
    for(;;) {
      if(!l->active) {
        // lock was destroyed; remove self from queue and fail
        for(int i = 0; i < l->queue_size; i++) {
          if(l->queue[i] == p) {
            for(int j = i + 1; j < l->queue_size; j++)
              l->queue[j-1] = l->queue[j];
            l->queue_size--;
            break;
          }
        }
        release(&l->lk);
        return -1;
      }
      if(!l->held && l->queue[0] == p)
        break;
      sleep(l, &l->lk);
    }

    // process woke at queue head; remove it and shift the rest
    if(l->queue_size > 0 && l->queue[0] == p) {
      for(int i = 1; i < l->queue_size; i++) {
        l->queue[i-1] = l->queue[i];
      }
      l->queue_size--;
    }
  }

  if(!l->active) {
    release(&l->lk);
    return -1;
  }

  l->held = 1; // acquire the lock
  release(&l->lk);
  return 0; 
}

// system call: israeli_release
uint64
sys_israeli_release(void)
{
  int lock_id;
  argint(0, &lock_id);

  if(lock_id < 0 || lock_id >= LOCK_COUNT)
    return -1; 

  struct israeli_lock *l = &locks[lock_id];
  struct proc *p = myproc();

  acquire(&l->lk);
  if(!l->active || !l->held) {
    release(&l->lk);
    return -1; 
  }

  l->held = 0; // release the lock

  if(l->queue_size > 0) {
    int G = p->gid; // gid of the releasing process
    int chosen_idx = 0; // default is FIFO (head of queue)
    int found_friend = 0;

    // 1. search for a process with the same gid in the queue
    for(int i = 0; i < l->queue_size; i++) {
      if(l->queue[i]->gid == G) {  
        chosen_idx = i; // pick the earliest among them (lowest queue index)
        found_friend = 1;
        break;
      }
    }

    // 2. apply random protection mechanism
    if(found_friend) {  
      // random coin flip from 0 to 99 using the task-0 PRNG
      uint random_val = lcg_rand() % 100;

      if(random_val >= l->favoritism) {
        // with complementary probability, fall back to normal FIFO (index 0)
        chosen_idx = 0;  
      }
    }

    // if a friend was chosen from the middle, move them to queue head
    if(chosen_idx > 0) {
      struct proc *friend = l->queue[chosen_idx];
      for(int i = chosen_idx; i > 0; i--) {
        l->queue[i] = l->queue[i-1];
      }
      l->queue[0] = friend;
    }

    // wake all waiters so the new queue head can proceed
    wakeup(l);  
  }

  release(&l->lk);
  return 0;  
}

// system call: israeli_destroy
uint64
sys_israeli_destroy(void)
{
  int lock_id;
  argint(0, &lock_id);

  if(lock_id < 0 || lock_id >= LOCK_COUNT)
    return -1;  

  struct israeli_lock *l = &locks[lock_id];

  acquire(&l->lk);
  if(!l->active) {
    release(&l->lk);
    return -1;  
  }

  l->active = 0; // deactivate the lock
  l->held = 0;
 
  // wake any processes stuck in the queue so they can return with an error
  if(l->queue_size > 0) {
    wakeup(l);  
  }
  l->queue_size = 0;

  release(&l->lk);
  return 0;  
}

// initialization function for the LCG lock; must be called during boot (e.g. from main.c)
void
lcg_init(void)
{
  initlock(&lcg_lock, "lcg_lock");
}

// (a) void lcg_srand(uint seed);
void
lcg_srand(uint seed)
{
  acquire(&lcg_lock);
  lcg_state = seed;
  release(&lcg_lock);
}

// (b) uint lcg_rand(void);
uint
lcg_rand(void)
{
  uint result;
  acquire(&lcg_lock);
  
  // X_{n+1} = a * X_n + b
  // modulo happens implicitly via uint32 overflow (since m = 2^32)
  lcg_state = lcg_state * 1664525 + 1013904223;
  result = lcg_state;
  
  release(&lcg_lock);
  return result;
}

// --- wrappers for system calls ---

uint64
sys_lcg_srand(void)
{
  int seed;
  // fetch argument sent from userspace
  argint(0, &seed);
  lcg_srand((uint)seed);
  return 0;
}

uint64
sys_lcg_rand(void)
{
  return lcg_rand();
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// 1. הגדרת משתנה המצב ומנעול ה-Spinlock להגנה עליו
static uint32 lcg_state = 1; // מצב ראשוני ברירת מחדל
static struct spinlock lcg_lock;

#define MAX_WAITING 16
#define LOCK_COUNT 15

struct israeli_lock {
  struct spinlock lk;        // מנעול פנימי להגנה על מבנה הנתונים
  int active;                 // האם המנעול נוצר ופעיל  
  int favoritism;             // מקדם הפרוטקציה (0-100)  
  int held;                   // האם המנעול תפוס כרגע (1 או 0)
  struct proc* queue[MAX_WAITING]; // תור ה-FIFO של התהליכים הממתינים   131, 135]
  int queue_size;             // כמות התהליכים הנוכחית בתור
};

static struct israeli_lock locks[LOCK_COUNT]; // מערך גלובלי של מנעולים  

// אתחול המערך עם עליית ה-Kernel  
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

// קריאת מערכת: setgid
uint64
sys_setgid(void)
{
  int gid;
  argint(0, &gid);
  struct proc *p = myproc();
  p->gid = gid; 
  return 0;
}

// קריאת מערכת: getgid
uint64
sys_getgid(void)
{
  return myproc()->gid;
}

// קריאת מערכת: israeli_create  
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
      return i; // ה-ID של המנעול הוא האינדקס שלו במערך  
    }
    release(&locks[i].lk);
  }
  return -1; 
}

// קריאת מערכת: israeli_acquire  
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

  // אם המנעול תפוס או שיש תהליכים שמחכים לפנינו (שומר על סדר FIFO בתור)  
  if(l->held || l->queue_size > 0) {
    if(l->queue_size >= MAX_WAITING) {
      release(&l->lk);
      return -1; // התור מלא  
    }
    // הוספת התהליך לסוף התור   135]
    l->queue[l->queue_size++] = p;

    // שינה עד שהמנעול מתפנה והתהליך נבחר  
    while(l->held || l->queue[0] != p) {
      sleep(l, &l->lk); 
    }

    // התהליך התעורר והוא בראש התור - מוציאים אותו מהתור ומקדמים את השאר   135]
    for(int i = 1; i < l->queue_size; i++) {
      l->queue[i-1] = l->queue[i];
    }
    l->queue_size--;
  }

  l->held = 1; // תפיסת המנעול  
  release(&l->lk);
  return 0; 
}

// קריאת מערכת: israeli_release  
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

  l->held = 0; // שחרור המנעול  118]

  if(l->queue_size > 0) {
    int G = p->gid; // ה-gid של התהליך המשחרר
    int chosen_idx = 0; // ברירת המחדל היא FIFO (הראשון בתור)
    int found_friend = 0;

    // 1. חיפוש תהליך עם אותו gid בתור  
    for(int i = 0; i < l->queue_size; i++) {
      if(l->queue[i]->gid == G) {  
        chosen_idx = i; // לוקחים את המוקדם ביותר מביניהם (האינדקס הנמוך ביותר בתור)  
        found_friend = 1;
        break;
      }
    }

    // 2. הפעלת מנגנון הפרוטקציה האקראי  
    if(found_friend) {  
      // הטלת מטבע אקראי בין 0 ל-99 בעזרת המחולל מטאסק 0  
      uint random_val = lcg_rand() % 100;
      if(random_val >= l->favoritism) {  
        // בהסתברות המשלימה - חוזרים ל-FIFO רגיל (אינדקס 0)  
        chosen_idx = 0;  
      }
    }

    // אם נבחר חבר מהאמצע, נעביר אותו לראש התור כדי שהוא יתעורר ויקבל את המנעול
    if(chosen_idx > 0) {
      struct proc *friend = l->queue[chosen_idx];
      for(int i = chosen_idx; i > 0; i--) {
        l->queue[i] = l->queue[i-1];
      }
      l->queue[0] = friend;
    }

    // מעירים את כל התהליכים הישנים על המנעול כדי שראש התור החדש יתקדם  
    wakeup(l);  
  }

  release(&l->lk);
  return 0;  
}

// קריאת מערכת: israeli_destroy  
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

  l->active = 0; // ביטול המנעול  
  l->held = 0;
 
  // אם יש תהליכים תקועים בתור, נעיר אותם שיחזרו עם שגיאה
  if(l->queue_size > 0) {
    wakeup(l);  
  }
  l->queue_size = 0;

  release(&l->lk);
  return 0;  
}

// פונקציית אתחול למנעול - יש לוודא שהיא נקראת בזמן עליית המערכת (למשל מתוך main.c)
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
  // המודולו נעשה אוטומטית מעצם הגלישה של uint32_t (מכיוון ש-m = 2^32)
  lcg_state = lcg_state * 1664525 + 1013904223;
  result = lcg_state;
  
  release(&lcg_lock);
  return result;
}

// --- מעטפת עבור קריאות המערכת (System Calls) ---

uint64
sys_lcg_srand(void)
{
  int seed;
  // שליפת הארגומנט שנשלח מה-userspace
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

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define TEAMS 3
#define RUNNERS_PER_TEAM 5
#define TARGET_SCORE 30

int main(int argc, char *argv[]) {
  int favoritism = 50; // ערך ברירת מחדל, נשנה ל-0, 50, או 100 בניסויים

  if (argc > 1) {
    favoritism = atoi(argv[1]);
  }

  printf("Starting Relay Race Tournament with %d%% favoritism...\n", favoritism);

  // 1. יצירת מנעול ישראלי יחיד שמייצג את מקל השליחים
  int lock_id = israeli_create(favoritism);
  if (lock_id < 0) {
    printf("Error: failed to create israeli lock\n");
    exit(1);
  }

  // אתחול מחולל המספרים האקראיים
  lcg_srand(getpid());

  // 2. יצירת רצים (child processes) וחלוקתם לקבוצות
  for (int t = 0; t < TEAMS; t++) {
    for (int r = 0; r < RUNNERS_PER_TEAM; r++) {
      int pid = fork();
      
      if (pid < 0) {
        printf("Fork failed!\n");
        exit(1);
      }

      if (pid == 0) { // קוד הרץ (תהליך הבן)
        // הגדרת ה-gid של הרץ לפי מזהה הקבוצה שלו
        setgid(t);
        
        // לולאת הריצה של הרץ
        while (1) {
          // בדיקה האם המרוץ כבר הסתיים על ידי קבוצה כלשהי לפני שמנסים לקחת את המקל
          int game_over = 0;
          for (int check_t = 0; check_t < TEAMS; check_t++) {
            if (get_team_score(check_t) >= TARGET_SCORE) {
              game_over = 1;
              break;
            }
          }
          if (game_over) break;

          // א. השגת מקל השליחים (המנעול)
          israeli_acquire(lock_id);

          // בדיקה חוזרת למקרה שהמרוץ הסתיים בזמן שישנו/חיכינו בתור לקבלת המקל
          game_over = 0;
          for (int check_t = 0; check_t < TEAMS; check_t++) {
            if (get_team_score(check_t) >= TARGET_SCORE) {
              game_over = 1;
              break;
            }
          }
          
          if (game_over) {
            israeli_release(lock_id);
            break;
          }

          // ב+ג. הגדלת הניקוד והדפסת הודעה למסך
          int current_team = getgid();
          int updated_score = increment_team_score(current_team);
          
          printf("Runner %d (Team %d) acquired the baton. Team %d score = %d\n", 
                 getpid(), current_team, current_team, updated_score);

          // ד. שחרור המקל
          israeli_release(lock_id);

          // ה. שינה קצרה כדי לאפשר לרצים אחרים לתפוס את המקל
          sleep(2);
        }
        exit(0);
      }
    }
  }

  // קוד תהליך האב: מחכה שכל הרצים יסיימו ברגע שאחד מנצח
  for (int i = 0; i < TEAMS * RUNNERS_PER_TEAM; i++) {
    wait(0);
  }

  // הדפסת התוצאות הסופיות החגיגיות
  printf("\n--- TOURNAMENT RESULTS ---\n");
  int winner_team = 0;
  int max_score = 0;
  for (int t = 0; t < TEAMS; t++) {
    int s = get_team_score(t);
    printf("Team %d final score: %d\n", t, s);
    if (s > max_score) {
      max_score = s;
      winner_team = t;
    }
  }
  printf("Winner: Team %d!\n", winner_team);

  
  israeli_destroy(lock_id);
  exit(0);
}
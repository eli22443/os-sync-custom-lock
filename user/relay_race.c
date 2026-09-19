#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define TEAMS 3
#define RUNNERS_PER_TEAM 5
#define TARGET_SCORE 30

int main(int argc, char *argv[])
{
    int favoritism = 50; // default value; try 0, 50, or 100 in experiments

    if (argc > 1)
    {
        favoritism = atoi(argv[1]);
    }

    reset_team_scores();

    printf("Starting Relay Race Tournament with %d%% favoritism...\n", favoritism);

    // 1. Create a single Israeli lock representing the relay baton
    int lock_id = israeli_create(favoritism);
    if (lock_id < 0)
    {
        printf("Error: failed to create israeli lock\n");
        exit(1);
    }

    // Pipe barrier: all runners block until every child is forked.
    int start_pipe[2];
    if (pipe(start_pipe) < 0)
    {
        printf("pipe failed\n");
        exit(1);
    }

    // Fork round-robin (one runner per team per wave) so no team is created last.
    for (int r = 0; r < RUNNERS_PER_TEAM; r++)
    {
        for (int t = 0; t < TEAMS; t++)
        {
            int pid = fork();

            if (pid < 0)
            {
                printf("Fork failed!\n");
                exit(1);
            }

            if (pid == 0)
            { // runner code (child process)
                close(start_pipe[1]);
                char ch;
                read(start_pipe[0], &ch, 1); // wait for parent to close write end
                close(start_pipe[0]);

                setgid(t);
                lcg_srand(getpid());

                // runner main loop
                while (1)
                {
                    // check whether any team already won before trying to take the baton
                    int game_over = 0;
                    for (int check_t = 0; check_t < TEAMS; check_t++)
                    {
                        if (get_team_score(check_t) >= TARGET_SCORE)
                        {
                            game_over = 1;
                            break;
                        }
                    }
                    if (game_over)
                        break;

                    // (a) acquire the relay baton (the lock)
                    israeli_acquire(lock_id);

                    // re-check in case the race ended while sleeping/waiting in queue
                    game_over = 0;
                    for (int check_t = 0; check_t < TEAMS; check_t++)
                    {
                        if (get_team_score(check_t) >= TARGET_SCORE)
                        {
                            game_over = 1;
                            break;
                        }
                    }

                    if (game_over)
                    {
                        israeli_release(lock_id);
                        break;
                    }

                    // (b)+(c) increment score and print a message
                    int current_team = getgid();
                    int updated_score = increment_team_score(current_team);

                    printf("Runner %d (Team %d) acquired the baton. Team %d score = %d\n",
                           getpid(), current_team, current_team, updated_score);

                    // (d) release the baton
                    israeli_release(lock_id);

                    // (e) short sleep so other runners can grab the baton
                    sleep(0);
                }
                exit(0);
            }
        }
    }

    // Release all runners at once.
    close(start_pipe[1]);
    close(start_pipe[0]);

    // parent process: wait for all runners to finish once someone wins
    for (int i = 0; i < TEAMS * RUNNERS_PER_TEAM; i++)
    {
        wait(0);
    }

    // print final tournament results
    printf("\n--- TOURNAMENT RESULTS ---\n");
    int winner_team = 0;
    int max_score = 0;
    for (int t = 0; t < TEAMS; t++)
    {
        int s = get_team_score(t);
        printf("Team %d final score: %d\n", t, s);
        if (s > max_score)
        {
            max_score = s;
            winner_team = t;
        }
    }
    printf("Winner: Team %d!\n", winner_team);

    israeli_destroy(lock_id);

    exit(0);
}

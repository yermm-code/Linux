// scheduler.c  (HW03 basic version)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

#define N 10

typedef enum { READY, RUNNING, SLEEPING, DONE } State;

typedef struct {
    pid_t pid;
    int tq_rem;         // 남은 타임퀀텀
    State state;        // running/ready/sleep/done
    int sleep_rem;      // I/O 대기 남은 시간(틱)
    long wait_ready;    // ready 큐에 있었던 시간(틱)
} PCB;

PCB pcb[N];

int TIME_QUANTUM = 3;
int running_idx = -1;
int rr_pos = 0;

volatile sig_atomic_t tick = 0;
volatile sig_atomic_t io_pid = -1;   // I/O 요청 보낸 자식 pid 기록(현재 running만 보낸다고 가정)

// ---------------- child code ----------------
volatile sig_atomic_t burst = 0;
pid_t ppid_g = 0;

void child_run(int sig) {
    (void)sig;

    // 1 tick 실행할 때마다 CPU burst 1 감소
    burst--;

    // CPU burst 끝나면: 종료 또는 I/O (랜덤)
    if (burst <= 0) {
        int r = rand() % 2; // 0: exit, 1: IO

        if (r == 0) {
            _exit(0);
        } else {
            // 부모에게 I/O 요청 시그널
            kill(ppid_g, SIGUSR2);
            // 다음 CPU burst 새로 초기화(1~10)
            burst = (rand() % 10) + 1;
        }
    }
}

void child_main() {
    ppid_g = getppid();
    srand((unsigned)(time(NULL) ^ getpid()));
    burst = (rand() % 10) + 1;

    signal(SIGUSR1, child_run);

    while (1) pause();
}

// ---------------- parent signal handlers ----------------
void on_alarm(int sig) {
    (void)sig;
    tick = 1;
}

void on_io(int sig, siginfo_t *info, void *ctx) {
    (void)sig; (void)ctx;
    io_pid = info->si_pid;   // 누가 I/O 요청했는지 pid 저장
}

// ---------------- helpers ----------------
int find_idx(pid_t pid) {
    for (int i = 0; i < N; i++) {
        if (pcb[i].pid == pid) return i;
    }
    return -1;
}

int all_tq_zero() {
    for (int i = 0; i < N; i++) {
        if (pcb[i].state != DONE && pcb[i].tq_rem > 0) return 0;
    }
    return 1;
}

void reset_all_tq() {
    for (int i = 0; i < N; i++) {
        if (pcb[i].state != DONE)
            pcb[i].tq_rem = TIME_QUANTUM;
    }
}

int pick_next_ready() {
    // RR 방식: rr_pos부터 한 바퀴 돌며 READY & tq_rem>0 찾기
    for (int k = 0; k < N; k++) {
        int i = (rr_pos + k) % N;
        if (pcb[i].state == READY && pcb[i].tq_rem > 0) {
            rr_pos = (i + 1) % N;
            return i;
        }
    }
    return -1;
}

void reap_done_children() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int idx = find_idx(pid);
        if (idx >= 0) {
            pcb[idx].state = DONE;
            if (running_idx == idx) running_idx = -1;
        }
    }
}

// ---------------- scheduler tick ----------------
void scheduler_tick() {
    // (ready 대기시간 누적)
    for (int i = 0; i < N; i++) {
        if (pcb[i].state == READY) pcb[i].wait_ready++;
    }

    // (sleep 큐 감소 -> 0이면 ready로)
    for (int i = 0; i < N; i++) {
        if (pcb[i].state == SLEEPING) {
            pcb[i].sleep_rem--;
            if (pcb[i].sleep_rem <= 0) {
                pcb[i].sleep_rem = 0;
                pcb[i].state = READY;
            }
        }
    }

    // running이 있으면 1 tick 실행
    if (running_idx >= 0 && pcb[running_idx].state == RUNNING) {
        // (1) 실행 중 프로세스 tq 1 감소
        pcb[running_idx].tq_rem--;

        // 자식에게 실행 시그널
        kill(pcb[running_idx].pid, SIGUSR1);

        // 자식이 종료했을 수도 있으니 수거
        reap_done_children();

        // (4) I/O 요청 처리: sleep 큐로 이동 (1~5)
        if (io_pid == pcb[running_idx].pid) {
            io_pid = -1;
            pcb[running_idx].state = SLEEPING;
            pcb[running_idx].sleep_rem = (rand() % 5) + 1;
            running_idx = -1;
        }
        // (2) tq가 0이면 다음 프로세스로
        else if (running_idx >= 0 && pcb[running_idx].tq_rem <= 0) {
            pcb[running_idx].state = READY;
            running_idx = -1;
        }
        // (3) tq가 0이 아니면 계속 실행 -> 아무것도 안 함
    }

    // (5) 모든 프로세스 tq가 0이면 전체 tq 초기화
    if (all_tq_zero()) {
        reset_all_tq();
    }

    // running이 없으면 다음 ready 선택
    if (running_idx < 0) {
        int nxt = pick_next_ready();
        if (nxt >= 0) {
            running_idx = nxt;
            pcb[running_idx].state = RUNNING;
        }
    }

    // 출력(너무 길지 않게)
    printf("RUN=");
    if (running_idx >= 0) printf("%d(tq=%d)", pcb[running_idx].pid, pcb[running_idx].tq_rem);
    else printf("NONE");

    int rdy=0, slp=0, don=0;
    for (int i=0;i<N;i++){
        if (pcb[i].state==READY) rdy++;
        else if (pcb[i].state==SLEEPING) slp++;
        else if (pcb[i].state==DONE) don++;
    }
    printf(" | READY=%d SLEEP=%d DONE=%d\n", rdy, slp, don);
}

int main(int argc, char *argv[]) {
    if (argc >= 2) TIME_QUANTUM = atoi(argv[1]);
    if (TIME_QUANTUM <= 0) TIME_QUANTUM = 3;

    srand((unsigned)time(NULL));

    // 자식 10개 생성 + PCB 초기화
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            child_main();
            return 0;
        }

        pcb[i].pid = pid;
        pcb[i].tq_rem = TIME_QUANTUM;
        pcb[i].state = READY;
        pcb[i].sleep_rem = 0;
        pcb[i].wait_ready = 0;
    }

    // SIGALRM handler
    signal(SIGALRM, on_alarm);

    // SIGUSR2 handler (I/O 요청 pid 확인하려고 sigaction 사용)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_io;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR2, &sa, NULL);

    // 1초마다 tick
    struct itimerval it;
    it.it_interval.tv_sec = 1;
    it.it_interval.tv_usec = 0;
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, NULL);

    // 최초 running 선택
    running_idx = pick_next_ready();
    if (running_idx >= 0) pcb[running_idx].state = RUNNING;

    // 루프: 모든 자식 DONE까지
    while (1) {
        pause();
        if (tick) {
            tick = 0;

            reap_done_children();
            scheduler_tick();

            int done_cnt = 0;
            for (int i = 0; i < N; i++) if (pcb[i].state == DONE) done_cnt++;
            if (done_cnt == N) break;
        }
    }

    // 평균 대기시간 출력
    long sum = 0;
    for (int i = 0; i < N; i++) sum += pcb[i].wait_ready;

    printf("\n=== RESULT ===\n");
    printf("TIME_QUANTUM=%d\n", TIME_QUANTUM);
    printf("AVG_READY_WAIT=%.2f ticks\n", (double)sum / N);

    return 0;
}


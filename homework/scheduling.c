#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>

#define NUM_CHILDREN 10
#define TIME_QUANTUM 3  // 타임 퀀텀 (변경하며 실험 가능) 

typedef enum { READY, RUNNING, SLEEP, DONE } State;

typedef struct {
    pid_t pid;
    int remain_quantum;     
    int cpu_burst;          
    State state;
    int io_wakeup_time;
    
    // [성능 분석용 통계 변수] 
    int waiting_time;       // Ready Queue에서 대기한 총 시간
    int turnaround_time;    // 생성부터 종료까지 걸린 총 시간
} PCB;

PCB pcb_table[NUM_CHILDREN];
int current_proc_index = -1; 
int global_tick = 0;
int active_process_count = NUM_CHILDREN; 

// 통계 집계용
double total_waiting_time_sum = 0;
double total_turnaround_time_sum = 0;

// ==========================================
// [자식 프로세스] 응용 역할
// ==========================================
int cpu_burst = 0;

void child_work_handler(int sig) {
    // (1) 시그널 받고 실행 시 CPU 버스트 1 감소
    if (cpu_burst > 0) {
        cpu_burst--; 
        printf("    [Child %d] Running... (남은 Burst: %d)\n", getpid(), cpu_burst);
    }

    // (2) CPU 버스트 0 되면 종료 혹은 I/O
    if (cpu_burst <= 0) {
        int action = rand() % 2; // 0: 종료, 1: I/O

        if (action == 0) {
            exit(0); // 종료
        } else {
            // (3) I/O 수행 시 부모에게 요청 시그널
            cpu_burst = (rand() % 10) + 1; // 다음 작업을 위해 버스트 충전
            kill(getppid(), SIGUSR2);
        }
    }
}

void child_process_logic(int id) {
    signal(SIGUSR1, child_work_handler); 
    srand(getpid()); 
    cpu_burst = (rand() % 10) + 1;
    
    printf("[Child %d] 생성됨 (PID: %d, Burst: %d)\n", id, getpid(), cpu_burst);

    while (1) {
        pause(); // sleep() 상태 대기
    }
}

// ==========================================
// [부모 프로세스] 커널 역할
// ==========================================

void child_exit_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for(int i=0; i<NUM_CHILDREN; i++) {
            if (pcb_table[i].pid == pid) {
                pcb_table[i].state = DONE;
                pcb_table[i].turnaround_time = global_tick; // 종료 시간 기록
                
                total_waiting_time_sum += pcb_table[i].waiting_time;
                total_turnaround_time_sum += pcb_table[i].turnaround_time;

                active_process_count--;
                printf("[Kernel] 자식 %d 종료. (대기시간: %d, 반환시간: %d)\n", 
                       i, pcb_table[i].waiting_time, pcb_table[i].turnaround_time);
                
                if (current_proc_index == i) current_proc_index = -1;
                break;
            }
        }
    }
}

void io_request_handler(int sig, siginfo_t *info, void *context) {
    pid_t sender_pid = info->si_pid;
    for(int i=0; i<NUM_CHILDREN; i++) {
        if (pcb_table[i].pid == sender_pid) {
            pcb_table[i].state = SLEEP;
            // (4) I/O 대기시간 랜덤(1~5) 할당
            int io_time = (rand() % 5) + 1;
            pcb_table[i].io_wakeup_time = global_tick + io_time;
            printf("[Kernel] 자식 %d I/O 요청 -> SLEEP (복귀: %d초)\n", i, pcb_table[i].io_wakeup_time);
            break;
        }
    }
}

// 라운드 로빈 스케줄링 
int schedule_next_process() {
    // (3) 퀀텀이 남았으면 계속 실행
    if (current_proc_index != -1 && 
        pcb_table[current_proc_index].state == RUNNING && 
        pcb_table[current_proc_index].remain_quantum > 0) {
        return current_proc_index;
    }

    // (2) 퀀텀 0이면 다음 프로세스 변경
    int start_index = (current_proc_index + 1) % NUM_CHILDREN;
    int i = start_index;
    int found_idx = -1;

    // 실행 가능한(READY) 프로세스 탐색
    do {
        if (pcb_table[i].state == READY) {
            if (pcb_table[i].remain_quantum > 0) {
                found_idx = i;
                break;
            }
        }
        i = (i + 1) % NUM_CHILDREN;
    } while (i != start_index);

    // 실행 가능한 애가 없는데, 혹시 모든 프로세스의 퀀텀이 0인가? 확인 
    if (found_idx == -1) {
        int all_zero = 1;
        int any_alive = 0;
        for (int k=0; k<NUM_CHILDREN; k++) {
            if (pcb_table[k].state != DONE) {
                any_alive = 1;
                if (pcb_table[k].remain_quantum > 0) {
                    all_zero = 0; // 아직 퀀텀 남은 애가 있음
                    break;
                }
            }
        }
        
        // (5) 모든 프로세스 퀀텀 0이면 전체 초기화 
        if (any_alive && all_zero) {
            printf("[Kernel] 모든 프로세스 퀀텀 소진 -> 전체 퀀텀 초기화!\n");
            for (int k=0; k<NUM_CHILDREN; k++) {
                if (pcb_table[k].state != DONE) 
                    pcb_table[k].remain_quantum = TIME_QUANTUM;
            }
            // 다시 검색
            return schedule_next_process();
        }
    }
    
    return found_idx;
}

void timer_handler(int sig) {
    global_tick++;
    
    // [성능 분석] Ready 상태인 프로세스들의 대기 시간 증가 
    for(int i=0; i<NUM_CHILDREN; i++) {
        if (pcb_table[i].state == READY) {
            pcb_table[i].waiting_time++;
        }
    }

    if (active_process_count <= 0) {
        printf("\n=== 시뮬레이션 종료 ===\n");
        // [결과 출력] 평균 대기 시간 계산 
        printf("평균 대기 시간(Average Waiting Time): %.2f ticks\n", total_waiting_time_sum / NUM_CHILDREN);
        printf("평균 반환 시간(Average Turnaround Time): %.2f ticks\n", total_turnaround_time_sum / NUM_CHILDREN);
        exit(0);
    }

    // I/O 완료 체크 및 Wakeup 
    for(int i=0; i<NUM_CHILDREN; i++) {
        if (pcb_table[i].state == SLEEP && global_tick >= pcb_table[i].io_wakeup_time) {
            pcb_table[i].state = READY;
            printf("[Kernel] 자식 %d I/O 완료 -> READY\n", i);
        }
    }

    int next_idx = schedule_next_process();

    if (next_idx != -1) {
        // 문맥 교환
        if (current_proc_index != -1 && current_proc_index != next_idx && 
            pcb_table[current_proc_index].state == RUNNING) {
            pcb_table[current_proc_index].state = READY;
        }

        current_proc_index = next_idx;
        PCB *proc = &pcb_table[next_idx];
        
        proc->state = RUNNING;
        proc->remain_quantum--; // (1) 타임 퀀텀 감소

        printf("[Kernel] Tick %d | 실행: Child %d | 퀀텀: %d\n", global_tick, next_idx, proc->remain_quantum);
        kill(proc->pid, SIGUSR1);
    } else {
        printf("[Kernel] Tick %d | IDLE (모두 I/O 중)\n", global_tick);
    }
}

int main(int argc, char *argv[]) {
    // 타임 퀀텀 값 바꿔가며 수행하기 위해 인자로 받을 수도 있음
    // 예: ./scheduler 5 
    
    struct sigaction sa_alarm;
    sa_alarm.sa_handler = timer_handler;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alarm, NULL);

    struct sigaction sa_io;
    sa_io.sa_sigaction = io_request_handler;
    sigemptyset(&sa_io.sa_mask);
    sa_io.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR2, &sa_io, NULL);

    signal(SIGCHLD, child_exit_handler);

    printf("=== OS 스케줄링 시뮬레이션 (최종) ===\n");
    printf("Time Quantum: %d\n", TIME_QUANTUM);

    for (int i = 0; i < NUM_CHILDREN; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            child_process_logic(i);
            exit(0);
        }
        pcb_table[i].pid = pid;
        pcb_table[i].state = READY;
        pcb_table[i].remain_quantum = TIME_QUANTUM;
        pcb_table[i].cpu_burst = 0;
        pcb_table[i].waiting_time = 0;
        pcb_table[i].turnaround_time = 0;
    }

    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 100000; // 0.1초 단위 (빠른 실행)
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 100000;
    setitimer(ITIMER_REAL, &timer, NULL);

    while(1) {
        pause();
    }
}

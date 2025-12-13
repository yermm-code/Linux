// HW03) OS 스케줄러 시뮬레이션
// 202301494 차예림

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

//최대 프로세스 개수 및 기본값
#define MAX_N 100     
int N = 10;           // 프로세스 개수
int IO_PROB = 50;     // I/O 확률

// 프로세스 상태 정의
typedef enum { READY, RUNNING, SLEEPING, DONE } State;

// PCB 정의
typedef struct {
	pid_t pid;
	int tq_rem;         // 남은 타임퀀텀
	State state;        // 현재 상태(running/ready/sleep/done)
	int sleep_rem;      // I/O 작업 남은 시간
	long wait_ready;    // ready 큐에서 있었던 시간(틱)
} PCB;

PCB pcb[MAX_N];      

int TIME_QUANTUM = 3;		// 기본 타임퀀텀 값
int running_idx = -1;		// 현재 실행중인 PCB 인덱스
int rr_pos = 0;			// 다음 검색 시작 위치 나타내는 라운드 로빈 순회포인터

volatile sig_atomic_t tick = 0;
volatile sig_atomic_t io_pid = -1;	//자식 프로세스 PID

// ============[ 자식 프로세스]================
volatile sig_atomic_t burst = 0;	// 현재 cpu burst 잔여량
pid_t ppid_g = 0;

// 자식프로세스가 SIGUSER1을 받으면 실행됨
void child_run(int sig) {
	(void)sig;

	burst--;

	// CPU burst가 끝났을때
	if (burst <= 0) {
		// 확률 변수(IO_PROB) 반영
		int r = rand() % 100; // 0 ~ 99의 난수 생성

		if (r >= IO_PROB) { 
			// 확률보다 크면 종료
			_exit(0);
		} else {
			// 확률보다 작으면 I/O 요청
			kill(ppid_g, SIGUSR2);
			//다음 작업 진행하기위해 CPU burst 시간 설정
			burst = (rand() % 10) + 1;
		}
	}
}

void child_main() {
	ppid_g = getppid();
	// 랜덤 시드 초기화 해줌
	srand((unsigned)(time(NULL) ^ getpid()));
	burst = (rand() % 10) + 1;
	//부모가 보내는 신호(SIGUSR1) 핸들러 등록
	signal(SIGUSR1, child_run);
	while (1) pause();
}

// ===============[ 부모 프로세스 시그널 핸들러]=============== 
void on_alarm(int sig) {	//1초마다 타이머 울리면 SIGALRM호출
	(void)sig;
	tick = 1;
}
//자식이 I/O 요청하면 SIGUSR2호출
void on_io(int sig, siginfo_t *info, void *ctx) {
	(void)sig; (void)ctx;
	io_pid = info->si_pid;	// 시그널 보낸 자식의 PID 저장함 
}

// ==============[ 헬퍼 함수들 ]======================
int find_idx(pid_t pid) {	//PID로 PCB배열 인덱스 찾음 
	for (int i = 0; i < N; i++) {
		if (pcb[i].pid == pid) return i;
	}
	return -1;
}
//살아있는 모든 프로세스의 타임퀀텀이 0인지 확인(리셋하기 위해)
int all_tq_zero() {
	for (int i = 0; i < N; i++) {	//종료 안됐고 남은 프로세스가 한개라도 존재하면 0 반환함
		if (pcb[i].state != DONE && pcb[i].tq_rem > 0) return 0;
	}
	return 1;
}

// 모든 프로세스의 타임퀀텀=> 초기값으로 리셋시킴 
void reset_all_tq() {
	for (int i = 0; i < N; i++) {
		if (pcb[i].state != DONE)
			pcb[i].tq_rem = TIME_QUANTUM;
	}
}
//라운드로빈으로 담에 실행할 프로세스 선택
int pick_next_ready() {
	for (int k = 0; k < N; k++) {
		int i = (rr_pos + k) % N;
		if (pcb[i].state == READY && pcb[i].tq_rem > 0) {
			rr_pos = (i + 1) % N;	//다음 검색위치 갱신함
			return i;
		}
	}
	return -1;	//실행할 수 있는 프로세스가 없음
}

//좀비상태 방지 위해 종료된 자식 수거
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

void scheduler_tick() {
	// 1. Ready 대기 시간 증가
	for (int i = 0; i < N; i++) {
		if (pcb[i].state == READY) pcb[i].wait_ready++;
	}

	// 2. Sleep중인 프로세스 시간 감소/ 깨우기
	for (int i = 0; i < N; i++) {
		if (pcb[i].state == SLEEPING) {
			pcb[i].sleep_rem--;
			if (pcb[i].sleep_rem <= 0) {
				pcb[i].sleep_rem = 0;
				pcb[i].state = READY;	//완료하면 ready로 이동
			}
		}
	}

	// 3. Running인 프로세스 처리
	if (running_idx >= 0 && pcb[running_idx].state == RUNNING) {
		pcb[running_idx].tq_rem--;
		//자식 프로세스에게 실제 CPU 사용 신호를 전송해줌
		kill(pcb[running_idx].pid, SIGUSR1);
		reap_done_children(); //자식이 실행중에 종료될 수도 있어서 확인해줌
		//자식이 I/O요청 
		if (running_idx >= 0 && io_pid == pcb[running_idx].pid) {
			io_pid = -1;
			pcb[running_idx].state = SLEEPING;
			pcb[running_idx].sleep_rem = (rand() % 5) + 1;
			running_idx = -1; //CPU 비워줌
		}
		//타임퀀텀 모두 소진해서 문맥교환이 필요한 경우
		else if (running_idx >= 0 && pcb[running_idx].tq_rem <= 0) {
			pcb[running_idx].state = READY;
			running_idx = -1;
		}
	}

	// 4. 타임퀀텀이 모두 소진되었으면 리셋해줌
	if (all_tq_zero()) {
		reset_all_tq();
	}

	// 5. CPU가 비어있으면 다음 ready프로세스를 선택
	if (running_idx < 0) {
		int nxt = pick_next_ready();
		if (nxt >= 0) {
			running_idx = nxt;
			pcb[running_idx].state = RUNNING;
		}
	}

	// 출력=> 현재 시스템의 상태
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
	// 실행할때 변수 설정 (./a.out TQ N IO_PROB으로 실행 시 써주면 됨)
	if (argc >= 2) TIME_QUANTUM = atoi(argv[1]);
	if (TIME_QUANTUM <= 0) TIME_QUANTUM = 3;

	if (argc >= 3) N = atoi(argv[2]);
	if (N <= 0 || N > MAX_N) N = 10;

	if (argc >= 4) IO_PROB = atoi(argv[3]);
	if (IO_PROB < 0 || IO_PROB > 100) IO_PROB = 50;

	printf("Config: TQ=%d, N=%d, IO_PROB=%d%%\n", TIME_QUANTUM, N, IO_PROB);
	srand((unsigned)time(NULL));

	// 자식 생성 & PCB 초기화
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

	// 시그널 핸들러 등록
	signal(SIGALRM, on_alarm);
	
	// SIGUSER2 핸들러 등록
	struct sigaction sa; //발신자 PID 확인
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_io;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR2, &sa, NULL);

	// 타이머(1초단위)
	struct itimerval it;
	it.it_interval.tv_sec = 1;
	it.it_interval.tv_usec = 0;
	it.it_value = it.it_interval;
	setitimer(ITIMER_REAL, &it, NULL);

	// 최초 실행
	running_idx = pick_next_ready();
	if (running_idx >= 0) pcb[running_idx].state = RUNNING;

	// 메인 루프(자식 모두 종료될때까지)
	while (1) {
		pause();
		if (tick) {
			tick = 0;
			reap_done_children(); //종료된 자식 정리
			scheduler_tick(); //스케줄링 수행함
			//종료 조건 검사 
			int done_cnt = 0;
			for (int i = 0; i < N; i++) if (pcb[i].state == DONE) done_cnt++;
			if (done_cnt == N) break;
		}
	}

	// 결과 출력
	long sum = 0;
	for (int i = 0; i < N; i++) sum += pcb[i].wait_ready;

	printf("\n=== RESULT ===\n");
	printf("TQ=%d | N=%d | IO=%d%%\n", TIME_QUANTUM, N, IO_PROB);
	printf("AVG_READY_WAIT=%.2f ticks\n", (double)sum / N);
	printf("CSV,%d,%d,%d,%.2f\n", TIME_QUANTUM, N, IO_PROB, ((double)sum / N));
	return 0;
}

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>

#define N 4

void sinx_taylor(int num_elements, int terms, double* x, double* result);

int main()
{
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];

	sinx_taylor(N, 3, x, res);
	for(int i=0; i<N; i++){
		printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
		printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
	}

	return 0;
}

void sinx_taylor(int num_elements, int terms, double* x, double* result)
{
    pid_t pids[num_elements];       
    int pipes[num_elements][2]; 

    for (int i = 0; i < num_elements; i++) 
    {
        if (pipe(pipes[i]) == -1) {
            perror("pipe error");
            exit(EXIT_FAILURE);
        }
 	
        pids[i] = fork(); //자식 프로세스 생성

        if (pids[i] == -1) {
            perror("fork error");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) { 
            close(pipes[i][0]);

            double value = x[i];
            double numer = x[i] * x[i] * x[i];
            double denom = 6.; // 3!임
            int sign = -1;

            for (int j = 1; j <= terms; j++) {
                value += (double)sign * numer / denom;
                numer *= x[i] * x[i];
                denom *= (2. * (double)j + 2.) * (2. * (double)j + 3.);
                sign *= -1;
            }
            write(pipes[i][1], &value, sizeof(double)); 
            close(pipes[i][1]);
            exit(0); 
        }
        else { 
            close(pipes[i][1]);
        }
    }

    for (int i = 0; i < num_elements; i++) 
    {
        double child_result;
        read(pipes[i][0], &child_result, sizeof(double));
        result[i] = child_result; 
        close(pipes[i][0]); 
    }

    //좀비방지하기 위해
    for (int i = 0; i < num_elements; i++) 
        {wait(NULL);}
}

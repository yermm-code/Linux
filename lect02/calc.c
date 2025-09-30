#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
        //argc: 명령줄 인수의 개수
        //argv[]: 명령줄 인수 리스트를 나타내는 포인터배열
        if(argc !=4) {
                exit(1);
        }

        int num1 = atof(argv[1]);
        char op = argv[2][0];
        int num2 = atof(argv[3]);
        int result=0;

        switch(op){
                case '+':
                        result=num1+num2;
                        break;
                case '-':
                        result = num1-num2;
                        break;
                case 'x': // 곱하기는 x 로 처리
                case '*':
                result = num1 * num2;
                 break;
                case '/':
                if (num2 == 0) {
                        printf("0으로 나눌 수 없습니다.\n");
                        exit(1);
                }
                result = num1 / num2;
                break;
                default:
                printf("지원하지 않는 연산자: %c\n", op);
                exit(1);
         }

         printf("%d\n",result);

         return 0;
}

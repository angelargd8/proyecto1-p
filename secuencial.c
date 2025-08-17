// libreriasss
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char*argv[]){
    int semilla = 42;
    srand(semilla);

    int n = atoi(argv[1]);

    if (n<=0){
        printf("El numero de elementos debe de ser mayor que 0");
        return 1;
    }


    return 0;
}
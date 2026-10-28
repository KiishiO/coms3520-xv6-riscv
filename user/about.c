#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//Name: Iteoluwakishi Osomo
//Description: Program prints student details, project info, and any added arguments

int
main(int argc, char *argv[]){
    printf("COM S 3520 Project 1A\n");
    printf("Student: Iteoluwakishi Osomo\n");

    if(argc > 1)
        printf("Argument: %s\n", argv[1]);
    
    exit(0);
}
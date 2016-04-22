#include <unistd.h>
#include <stdio.h>

int main(void){

	int result;
	result = fork();

	if(result == 0){
		printf("Parent print.\n");
	}else{
		printf("Child print.\n");
	}

	return 0;
}

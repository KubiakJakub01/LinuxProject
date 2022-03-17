#define _GNU_SOURCE

#include <stdio.h>

#include <unistd.h>

#include <fcntl.h>

#include <sys/ipc.h>

#include <sys/shm.h>

#include <sys/sem.h>

#include <sys/msg.h>

#include <sys/stat.h>

#include <sys/types.h>

#include <string.h>

#include <stdbool.h>

#include <stdlib.h>

#include <signal.h>

#include <sys/wait.h>



#define SIZE 100

// Numer unikalnego id dla pliku (ftok)

#define KEYNB 65

//Liczba uzywanych semaforów

#define N 3

//Liczba semaforów (do odczytu i zapisu)

#define SEMNUMB 2



#define SEM_S1_KEY "/tmp/sem-s1-key"

#define SEM_S2_KEY "/tmp/sem-s2-key"



/*Deklaracja sygnałów */

//Signal handler - odbiera wszystkie sygnały zapisuje je do K3 i rozsyła sygnał Si4 do wszystkich procesów

void signal_handler(int sig); 



//Sygnał Si4 (SIGURG) - Informuje pozostałe procesy o otrzymanym komunikacie

void signal_P1_Si4(int sig);

void signal_P2_Si4(int sig);

void signal_P3_Si4(int sig);

/*Deklaracja sygnałów */

//Czy process: 1 - działa 0 - wstrzmany

bool isRunning[3];



/* Deklaracja K1 - kolejka komunikatów */

struct mesg_buffer {

    long mesg_type;

    char mesg_text[SIZE*10];

} message;



int msgid;

  

/* Deklaracja K2 - pamięć współdzielona */

int shmid;

char *shm;


 /* Deklaracja K3 - kolejka fifo */

int fd[2];


 /*Deklaracja semaforów*/

 union semun{

	int val;

	struct semid_ds *buf;

	unsigned short int* array;

	struct seminfo *_buf;

};



struct sembuf write_lock = {0,-1,0};

struct sembuf write_unlock = {0,1,0};



struct sembuf read_lock = {1,-1,0};

struct sembuf read_unlock = {1,1,0};



int semid[N];

 

/*Process pid*/

int  pid_1, pid_2, pid_3;

int main(int argc, char *argv[])

{

	/*----------------------Segment inicjalizacji----------------------*/

	// generowanie unikalnego klucza

	key_t key;

	/*Inicjalizacja semaforów*/

	//Sem1 - synchronizacja między P1 i P2

	//Sem2 - synchronizacja między P2 i P3

	//Sem3 - synchronizacja między P1 i P3

	for(int i = 0; i<N; i++)

	{

		if((key = ftok(".", (i+1))) == -1)

		{

			perror ("ftok"); 

			exit (1);	

		}

		semid[i] = semget(key, SEMNUMB, IPC_CREAT | 0666);

		if(semid[i] < 0)

		{

			fprintf(stderr, "\nSEMGET 1 Failed\n");

			exit(EXIT_FAILURE);

		}

		// utworzenie semafora

		union semun op;

		op.val = 1; //mozliwy zapis

		semctl(semid[i], 0, SETVAL, op);

		op.val = 0; //blokada odczytu

		semctl(semid[i], 1, SETVAL, op);

	}

	/*K1*/

	key = ftok(".", KEYNB);

	//Utworzenie kolejki komunikatów

	msgid = msgget(key, 0666 | IPC_CREAT);

	message.mesg_type = 1;

	/*K1*/


	/*K2*/

	// Utworzenie segmenty pamięci współdzielonej 

	key = ftok(".", KEYNB+1);

	shmid = shmget(key, SIZE, IPC_CREAT | 0666);

	shm = (char *)shmat(shmid, NULL, 0);

	/*K2*/

	

	/*K3*/

	// Utworzenie łącza fifo

	mkfifo("myfifo", 0666);

	fd[0] = -1;

	fd[1] = -1;	

	/*K3*/

	

	/*Inicjalizacja mechanizmów obsługi sygnałów dla procesów*/

	struct sigaction sa;

	struct sigaction act;

	sa.sa_handler = &signal_handler;

	sa.sa_flags = SA_RESTART;

	/*----------------------Segment procesów----------------------*/

	bool flag = true;	

	//--------------Proces 3--------------//

	if((pid_3=fork()) == 0)

	{

		pid_3 = getpid();

		fprintf(stderr, "[P3]Mój pid: %d\n", pid_3);

		isRunning[2] = true;

		

		//Przypisane obsługi sygnałów

		act.sa_handler = &signal_P3_Si4;

		sigaction(SIGURG, &act, NULL);

		sigaction(SIGINT, &sa, NULL);

		sigaction(SIGUSR1, &sa, NULL);

		sigaction(SIGUSR2, &sa, NULL);

		

		// Proces P3 odczytuje dane z K2 i przekazuje je na stdout

		while(flag)

		{

			if(!isRunning[2])

			{
				
				sleep(1);

				continue;

			}

			

			semop(semid[1], &read_lock, 1);

			semop(semid[2], &read_lock, 1);

			//Przekazanie danych na stdout

			fprintf(stdout, "%s", shm);

			

			// zwolnienie slotu do pisania

			semop(semid[1], &write_unlock, 1);

			semop(semid[2], &write_unlock, 1);

		}

		exit(0);

	}

	

	//--------------Proces 2--------------//

	if((pid_2=fork()) == 0)

	{

		pid_2 = getpid();

		fprintf(stderr, "[P2]Mój pid: %d\n", pid_2);
		
		isRunning[1] = true;
		

		//Przypisane obsługi sygnałów

		act.sa_handler = &signal_P2_Si4;

		sigaction(SIGURG, &act, NULL);

		sigaction(SIGINT, &sa, NULL);

		sigaction(SIGUSR1, &sa, NULL);

		sigaction(SIGUSR2, &sa, NULL);

		

		// Proces P2 odczy dane z K1 wyświetla ilość otrzymanych bajtów i zapisuje je do K2

		while(flag)

		{

			if(!isRunning[1])

			{

				sleep(1);

				continue;

			}

			

			semop(semid[0], &read_lock, 1);

			

			//Pobranie danych z K1

			msgrcv(msgid, &message, sizeof(message), 1, 0);

			//Wyświetlenie ilości otrzymanych bajtów

			fprintf(stderr, "[P2]Otrzymano %d bajtów \n", strlen(message.mesg_text));

			

								

			//Zapis do K2

			// oczekiwanie na mozliwość pisania

			semop(semid[1], &write_lock, 1);
			

			//Przekazanie danych do K2

			memcpy(shm, message.mesg_text, sizeof(char)*SIZE);

			

			// zwolnienie slota do czytania

			semop(semid[1], &read_unlock, 1);

			

			// zwolnienie slotu do pisania w K1

			semop(semid[0], &write_unlock, 1);

		}

		exit(1);

	}

	

	//--------------Proces 1--------------//

	if((pid_1=fork()) == 0)

	{

		pid_1 = getpid();

		fprintf(stderr, "[P1]Mój pid: %d\n", pid_1);
		
		isRunning[0] = true;

		

		//Przypisane obsługi sygnałów

		act.sa_handler = &signal_P1_Si4;

		sigaction(SIGURG, &act, NULL);

		sigaction(SIGINT, &sa, NULL);

		sigaction(SIGUSR1, &sa, NULL);

		sigaction(SIGUSR2, &sa, NULL);

		//Proces P1 odczytuje dane z stdin i przekazuje je do K1

		int i = 0;

		while (!feof(stdin))

		{		

			if(!isRunning[0])

			{

				sleep(1);

				continue;

			}

							

			// oczekiwanie na mozliwość pisania

			semop(semid[2], &write_lock, 1);

			semop(semid[0], &write_lock, 1);

			//Odczyt z stdin

			fgets(message.mesg_text, SIZE, stdin);

			//Wysłanie wiadomości do kolejki komunikatów

			msgsnd(msgid, &message, sizeof(message), 0);

			

			// zwolnienie slota do czytania

			semop(semid[0], &read_unlock, 1);

			semop(semid[2], &read_unlock, 1);

		}
		
		sleep(1);
		
		kill(pid_2, SIGINT);
		
		sleep(1);		
	}	
	
	FILE *ptr;
	
	ptr = fopen("pidy.txt","w");
	
	fprintf(ptr, "[P1]Mój pid: %d\n", pid_1);
	
	fprintf(ptr, "[P2]Mój pid: %d\n", pid_2);
	
	fprintf(ptr, "[P3]Mój pid: %d\n", pid_3);
	
	fclose(ptr);
	
	wait(NULL);

	wait(NULL);

	wait(NULL);

	return 0;

}

 

/* Implementacj obsługi sygnałów */

void signal_handler(int sig)

{	
	//printf("Pid z pilu: %d Pid_3 == %d \n", pid, pid_3);
	//Wysłanie sygnału Si4 do wszystkich procesów, aby odczytały fifo
	
	if(getpid() != pid_3)
	{
		kill(pid_3, sig);
	}
	else
	{
		kill((pid_3+1), SIGURG);
		
		sleep(1);
		
		kill((pid_3+2), SIGURG);
		
		sleep(1);

		kill(pid_3, SIGURG);			

		if(fd[1] == -1)

		{

			fd[1] = open("myfifo", O_WRONLY, 0666);	

			if(fd[1] == -1) 

			{
				
				fprintf(stderr, "Nie udało się otworzyć fifo do zapisu\n");

				exit(1); 

			}

		}

		//Zapianie signału do kolejki fifo

		char *message;

		if(sig == 2) message = "111";

		else if(sig == 10) message = "222";

		else if(sig == 12) message = "333";

		else message = "000";

		if(write(fd[1], message, sizeof(char)*3) == -1) 

		{
			fprintf(stderr, "Nie udało się poprawnie wysłać wiadomości: sig == %d\n", sig);
		}		
	}
}



//SIGURG

void signal_P1_Si4(int sig)

{

	if(fd[0] == -1)

	{

		//printf("[P1]Otworzenie fifo do odczytu\n");

		fd[0] = open("myfifo", O_RDONLY);

		if(fd[0] == -1) 

		{
			fprintf(stderr, "Nie udało się otworzyć fifo do odczytu\n");
			
			exit(1);

		}

	}

	char received_signal;

	//Odczyt danych

	if(read(fd[0], &received_signal, sizeof(char)) == 0)

	{
		fprintf(stderr, "[P1-Si4] Fifo puste\n");
	}	

	if(received_signal == '1') //SIGINT

	{
		
		fprintf(stderr, "Zakończenie procesu P1\n");	
		
		sleep(1);

		/*----------------------Segment czyśczenia----------------------*/

		// usunięcie semaforów

		semctl(semid[0], 0, IPC_RMID);

		semctl(semid[1], 0, IPC_RMID);

		semctl(semid[2], 0, IPC_RMID);

			

		/*K1*/

		// usunięcie kolejki komunikatów

		msgctl(msgid, IPC_RMID, NULL); 

		/*K1*/

		

		sleep(0.5);

		/*K2*/

		// odłączenie pamięci

		shmdt(shm);

		// usunięcie pamięci

		shmctl(shmid, IPC_RMID, NULL);

		/*K2*/		

					

		/*K3*/

		//Zamknięcie łączy fifo

		close(fd[0]);

		close(fd[1]);

		unlink("myfifo");

		/*K3*/	

		exit(0);

	}

	else if(received_signal == '2') //SIGUSR1

	{

		if(isRunning[0])

		{
			fprintf(stderr, "[P1]Wstrzymuje swoje działanie\n");

			isRunning[0] = false;

		}

	}

	else if(received_signal == '3') //SIGUSR2

	{

		if(!isRunning[0])

		{
			fprintf(stderr, "[P1]Wznawiam swoje działanie\n");

			isRunning[0] = true;

		}

	}

	else

	{
		fprintf(stderr, "Nieznany sygnał\n");
	}

}



void signal_P2_Si4(int sig)

{

	if(fd[0] == -1)

	{

		fd[0] = open("myfifo", O_RDONLY);

		if(fd[0] == -1) 

		{
			fprintf(stderr, "Nie udało się otworzyć fifo do odczytu\n");

			exit(1);

		}

	}

	char received_signal;

	

	//Odczyt danych

	if(read(fd[0], &received_signal, sizeof(char)) == 0)

	{
		fprintf(stderr, "[P2-Si4] Fifo puste\n");
	}
	

	if(received_signal == '1') //SIGINT

	{	
		fprintf(stderr, "Zakończenie procesu P2\n");	

		close(fd[0]);

		close(fd[1]);

		exit(0);

	}

	else if(received_signal == '2') //SIGUSR1

	{

		if(isRunning[1])

		{
			fprintf(stderr, "[P2]Wstrzymuje swoje działanie\n");	

			isRunning[1] = false;

		}

	}

	else if(received_signal == '3') //SIGUSR2

	{

		if(!isRunning[1])

		{

			fprintf(stderr, "[P2]Wznawiam swoje działanie\n");

			isRunning[1] = true;

		}

	}

	else

	{
		fprintf(stderr, "[P2]Niezany sygnał\n");
	}

}



void signal_P3_Si4(int sig)

{

	if(fd[0] == -1)

	{

		fd[0] = open("myfifo", O_RDONLY);

		if(fd[0] == -1) 

		{
			fprintf(stderr, "Nie udało się otworzyć fifo do odczytu\n");

			exit(1);

		}

	}

	char received_signal;

	

	//Odczyt danych

	if(read(fd[0], &received_signal, sizeof(char)) == 0)

	{
		fprintf(stderr, "[P3-Si4] Fifo puste\n");
	}

	if(received_signal == '1') //SIGINT

	{	
		
		fprintf(stderr, "Zakończenie procesu P3\n");
		
		close(fd[0]);

		close(fd[1]);

		exit(0);

	}

	else if(received_signal == '2') //SIGUSR1

	{

		if(isRunning[2])

		{
			fprintf(stderr, "[P3]Wstrzymuje swoje działanie\n");

			isRunning[2] = false;
		}

	}

	else if(received_signal == '3') //SIGUSR2

	{

		if(!isRunning[2])

		{

			fprintf(stderr, "[P3]Wznawiam swoje działanie\n");

			isRunning[2] = true;

		}

	}

	else

	{
		fprintf(stderr, "Nieznany sygnał\n");
	}

}

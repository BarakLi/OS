#define _CRT_SECURE_NO_WARNINGS
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>   

#define BUFFER_SIZE 49
#define PROCESSNAME_SIZE 256
#define MAX_VESS 50
#define MIN_VESS 2
#define MAX_SLEEP_TIME 3000
#define MIN_SLEEP_TIME 5
#define DECIMAL 10
#define True 1
#define False 0
#define LANE_NUM 2//Number of Lanes in vessels Interchange -> 0-bussy, 1-free

#define MEMALLOC(var); if(!var)\
{\
printf("Memory Allocation failed\n");\
exit(-1);\
}

/*Global variables, all types - */

//The Interchange DB: interchangeLanes[x] = -1 means lane x is available;
//otherwise interchangeLanes[x] = VessID
int interchangeLanes[LANE_NUM];
HANDLE mutex, writingMutex;
HANDLE ReadHandle, WriteHandle, ReadHandle2, WriteHandle2;
DWORD  write, read;
char timeString[11];
char message[BUFFER_SIZE] = { 0 };
char answer[BUFFER_SIZE] = { 0 };

//vessels (global variables)-
int vesselsNum;
int* vesselssID;
HANDLE* vesselsArr;
HANDLE* semaphVess; //Semaphores - one for Each vessel

//Controlling Lane Resources (initiated to LANE_NUM)
HANDLE lanesSemaphore;
//Protecting the shared Data that managing lanes (interchangeLanes Array)
HANDLE lnMutex;


//Functions decleration - 

//Returns the current time in string format.
char* currentTime();

//there are 2 options - -1 is free, !=-1 is busy
void initArray();

//Function that initalize semaphores & mutex.
int initGlobalData(int vesselsNum);

//The Thread Function for each Vessel
DWORD WINAPI runVess(PVOID);

//chooce random number for sleep time
int threadSleepTime();

//close all threads and global data
void cleanupGlobalData(int vesselsNum);

//enter and exit from Canal, each route within it Suez can contain at a given time only one vessel.
int enterVessInterchange(int vessId);
int exitVessInterchange(int lane, int vessId);





int main(int argc, char* argv[])
{
	vesselsNum = atoi(argv[1]);
	srand((unsigned int)time(NULL));
	if (argc != 2 || atoi(argv[1]) < MIN_VESS || atoi(argv[1]) > MAX_VESS)
	{
		printf("Haifa port: %d is invalid parameter - should be between 2 and 50!\n", vesselsNum);
		return 1;
	}
	printf("%s Haifa port: The number of vessels is %d\n", currentTime(), vesselsNum);

	TCHAR ProcessName[PROCESSNAME_SIZE];
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD ThreadId;

	vesselsArr = (HANDLE*)malloc(vesselsNum * sizeof(HANDLE));
	MEMALLOC(vesselsArr);
	semaphVess = (HANDLE*)malloc(vesselsNum * sizeof(HANDLE));
	MEMALLOC(semaphVess);
	vesselssID = (int*)malloc(vesselsNum * sizeof(int));
	MEMALLOC(vesselssID);

	//Initialise Train vesselss Interchange Lanes DB
	initArray();

	if (!initGlobalData(vesselsNum))
		return 1;

	/*Set up security attributes so that the pipe handles are inherited*/
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES),NULL,TRUE };

	/* allocate memory*/
	ZeroMemory(&pi, sizeof(pi));

	/*create pipe*/
	if (!CreatePipe(&ReadHandle, &WriteHandle, &sa, 0)) {
		fprintf(stderr, "Haifa port: Create Pipe Faile\n");
		return 1;
	}
	if (!CreatePipe(&ReadHandle2, &WriteHandle2, &sa, 0)) {
		fprintf(stderr, "Haifa port: Create Pipe Faile\n");
		return 1;
	}
	/*Establish the START_INFO structure for the child process*/
	GetStartupInfo(&si);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si.hStdOutput = WriteHandle2;
	si.hStdInput = ReadHandle;
	si.dwFlags = STARTF_USESTDHANDLES;

	/* we do not want the child to inherit the write end of the pipe */
	SetHandleInformation(WriteHandle, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(ReadHandle2, HANDLE_FLAG_INHERIT, 0);

	//Initializes Eilat
	wcscpy(ProcessName, L"EilatPort.exe");
	if (!CreateProcess(NULL, ProcessName, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
	{
		fprintf(stderr, "Haifa port: Process Creation Failed\n");
		exit(-1);
	}
	_itoa(atoi(argv[1]), message, DECIMAL);

	printf("%s Haifa port: Requesting permission to start sailing route between Haifa and Eilat\n", currentTime());
	WaitForSingleObject(writingMutex, INFINITE);
	//Writes to Eilat
	if (!WriteFile(WriteHandle, message, BUFFER_SIZE, &write, NULL))
		fprintf(stderr, "Haifa port: Error writing to pipe\n");
	if (!ReleaseMutex(writingMutex))
		printf("main::Unexpected error writingMutex.V()\n");


	//Read the answer from Eilat to Haifa
	if (ReadFile(ReadHandle2, answer, BUFFER_SIZE, &read, NULL))
	{
		if (atoi(answer) == 0)
		{
			printf("Haifa port: The number of vessels is a prime and isn't allowed\n");
			CloseHandle(ReadHandle2);
			CloseHandle(WriteHandle2);
			CloseHandle(ReadHandle);
			CloseHandle(WriteHandle);
			/*Close all handles*/
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			cleanupGlobalData(vesselsNum);
			return 1;
		}
		else
			printf("%s Haifa port: The number of vessels is acceptable, sailing is allowed\n", currentTime());
	}
	else
		fprintf(stderr, "Haifa port: answer from Eilat to Haifa - Error reading from pipe\n");

	lanesSemaphore = CreateSemaphore(NULL, LANE_NUM, LANE_NUM, NULL);
	if (lanesSemaphore == NULL)
	{
		printf("main::Unexpected Error in lanesSemaphore Creation\n");
		return 1;
	}

	//Creating the Mutex Semaphore
	lnMutex = CreateMutex(NULL, FALSE, NULL);
	if (lnMutex == NULL)
	{
		printf("main::Unexpected Error in lnMutex Creation\n");
		return 1;
	}


	//Initializes the vessels threads
	for (int i = 0; i < vesselsNum; i++)
	{
		//Create Thread	Producers 
		vesselssID[i] = i + 1;
		vesselsArr[i] = CreateThread(NULL, 0, runVess, &vesselssID[i], 0, &ThreadId);

		if (vesselsArr[i] == NULL)
		{
			printf("main::Unexpected Error in Thread Creation\n");
			return 1;
		}
	}

	//return vessels from Eilat port
	for (int ship = 0; ship < vesselsNum; ship++)
	{
		int backID;
		if (ReadFile(ReadHandle2, answer, BUFFER_SIZE, &read, NULL))
		{
			backID = atoi(answer);
			if (!ReleaseSemaphore(semaphVess[backID - 1], 1, NULL))
				printf("main::Unexpected error semaphVess[%d].V()\n", (backID - 1));

		}
		else
			fprintf(stderr, "main::Error reading from pipe\n");
	}


	/*Wait for child to close*/
	WaitForSingleObject(pi.hProcess, INFINITE);

	/*Wait for threads to close*/
	WaitForMultipleObjects(vesselsNum, vesselsArr, TRUE, INFINITE);

	cleanupGlobalData(vesselsNum);

	fprintf(stderr, "%s Haifa port: all vessel Treads are done \n", currentTime());


	/* close all handles */
	CloseHandle(ReadHandle2);
	CloseHandle(WriteHandle2);
	CloseHandle(ReadHandle);
	CloseHandle(WriteHandle);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);


	fprintf(stderr, "%s Haifa port: Exiting..... \n", currentTime());
	return False;
}


char* currentTime() {
	time_t current_time;
	struct tm* time_info;

	time(&current_time);
	time_info = localtime(&current_time);

	strftime(timeString, sizeof(timeString), "[%H:%M:%S]", time_info);
	return timeString;
}

//Init  to -1 ----> lan is free
void initArray()
{
	int i;
	for (i = 0; i < LANE_NUM; i++)
		interchangeLanes[i] = -1;
}

//random sleep time between 3 mill- 5 mill
int threadSleepTime()
{
	int sleepTime = (rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME + 1)) + MIN_SLEEP_TIME;
	return sleepTime;
}

DWORD WINAPI runVess(PVOID Param)
{
	int lane, time;
	int vessID = *(int*)Param;

	lane = enterVessInterchange(vessID);

	//Error Handling
	if (lane == -1)
	{
		printf("Unexpected Error Thread %d Entering\n", vessID);
		return 1;
	}
	//enter to lane
	printf("%s vessle %d starts sailing @ Haifa port \n", currentTime(), vessID);
	Sleep(threadSleepTime());
	printf("%s Vessel %d - entering canal: Med. Sea ==> Red Sea\n", currentTime(), vessID);

	Sleep(threadSleepTime());
	//
	char message[BUFFER_SIZE];
	_itoa(vessID, message, DECIMAL);
	Sleep(threadSleepTime());

	if (!exitVessInterchange(lane, vessID))
	{
		printf("Haifa port: Unexpected Error Thread %d Exiting\n", vessID);
		return 1;
	}

	WaitForSingleObject(writingMutex, INFINITE);
	if (!WriteFile(WriteHandle, message, BUFFER_SIZE, &write, NULL))
		fprintf(stderr, "Haifa port: Error writing to pipe\n");
	if (!ReleaseMutex(writingMutex))
		printf("Haifa port: runVess::Unexpected error writingMutex.V()\n");

	//Waits for vessel return from Eilat
	WaitForSingleObject(semaphVess[vessID - 1], INFINITE);
	Sleep(threadSleepTime());
	fprintf(stderr, "%s vessel %d - done sailing @ Haifa port\n", currentTime(), vessID);

	return 0;

}


int initGlobalData(int num) {
	mutex = CreateMutex(NULL, FALSE, NULL);
	if (mutex == NULL) {
		printf("Haifa port: Create Mutex error: %d\n", GetLastError());
		return False;
	}
	writingMutex = CreateMutex(NULL, FALSE, NULL);
	if (writingMutex == NULL) {
		printf("Haifa port: Create Writing Mutex error: %d\n", GetLastError());
		return False;
	}
	for (int i = 0; i < num; i++)
	{
		semaphVess[i] = CreateSemaphore(NULL, 0, 1, NULL);
		if (semaphVess[i] == NULL)
		{
			printf("Haifa port: main::Unexpected Error in semaphVess semaphore Creation\n");
			return False;
		}
	}

	return True;
}

void cleanupGlobalData(int vesselsNum) {

	for (int i = 0; i < vesselsNum; i++)
		CloseHandle(vesselsArr[i]);
	for (int i = 0; i < vesselsNum; i++)
		CloseHandle(semaphVess[i]);
	CloseHandle(mutex);
	CloseHandle(writingMutex);
	CloseHandle(lnMutex);
	CloseHandle(lanesSemaphore);
	//free all pointers
	free(vesselsArr);
	free(semaphVess);
	free(vesselssID);
}


int enterVessInterchange(int vessId) {
	int availableLane = 0;
	//P() on lanesSemaphore - if reached 0, then no available lanes - block until signaled by a train leaving Interchange
	WaitForSingleObject(lanesSemaphore, INFINITE);

	//Getting here means there is available lane - now P() on lnMutex to update interchangeLanes DB exclusively (in Critical Section)
	//If another Thread updates it, then block - wait for it to Finish
	WaitForSingleObject(lnMutex, INFINITE);

	for (availableLane = 0; availableLane < LANE_NUM; availableLane++) {
		if (interchangeLanes[availableLane] == -1)
		{
			interchangeLanes[availableLane] = vessId;
			break;
		}
	}

	//lnMutex.V() exiting Critical Section
	if (!ReleaseMutex(lnMutex))
		printf("Haifa port: enterVessInterchange::Unexpected error lnMutex.V()\n");


	if (availableLane >= LANE_NUM)
	{
		printf("Haifa port: enterVessInterchange::Unexpected ERROR!!!\n");
		availableLane = -1;
	}

	return availableLane;
}

int exitVessInterchange(int lane, int vessId)
{
	int ret = True;
	//now P() on lnMutex to update interchangeLanes DB exclusively (in Critical Section)
	//If another Thread updates it, then block - wait for it to Finish
	WaitForSingleObject(lnMutex, INFINITE);

	if (interchangeLanes[lane] != vessId)
	{
		printf("Haifa port: exitVessInterchange::Unexpected ERROR!!!\n");
		ret = False;
	}
	else
		interchangeLanes[lane] = -1;


	//lnMutex.V() exiting Critical Section
	if (!ReleaseMutex(lnMutex))
		printf("Haifa port: exitVessInterchange::Unexpected error lnMutex.V()\n");

	//lanesSemaphore.V() to signal a Train waiting for a lane on this Semaphore
	//Must be called after DB is updated, as otherwise you might signal someone before DB is updated and it won't find an 
	//available lane in DB to update!
	if (!ReleaseSemaphore(lanesSemaphore, 1, NULL))
		printf("Haifa port: exitVessInterchange::Unexpected error lanesSemaphore.V()\n");

	return ret;

}
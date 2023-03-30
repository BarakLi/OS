#define _CRT_SECURE_NO_WARNINGS
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <time.h>    //for strand

#define BUFFER_SIZE 49
#define MAX_SHIP 50
#define MAX_WEIGHT 50
#define MIN_SHIP 2
#define DECIMAL 10
#define True 1
#define False 0
#define MAX_SLEEP_TIME 3000
#define MIN_SLEEP_TIME 5
#define MAX_CARGO 50 //Tons
#define MIN_CARGO 5 //Tons
#define LANE_NUM 2//Number of Lanes in vessels Interchange -> 0-bussy, 1-free

#define MEMALLOC(var); if(!var)\
{\
printf("Memory Allocation failed\n");\
exit(-1);\
}


//The Interchange DB: interchangeLanes[x] = -1 means lane x is available;
//otherwise interchangeLanes[x] = VessID
int interchangeLanes[LANE_NUM];

//single mutex to protect access to the global state data.
HANDLE mutex, mutexEnterAdt, mutexExitFromADT, mutexUnload;
//initalize semaphore counter of the barrier.
HANDLE semBarrIn, semBarrOut;
//Controlling Lane Resources (initiated to LANE_NUM)
HANDLE lanesSemaphore;
//Protecting the shared Data that managing lanes (interchangeLanes Array)
HANDLE lnMutex;

//Global variables - 
HANDLE ReadHandle, WriteHandle;
DWORD read, written;
char timeString[11];
int BarrierCounter; // number od vessels in barrier
int ADTCounter = 0, Cexit = 0; //number of vessels in ADT


//vessels -
int numOfVess;
int* vesselsID;
HANDLE* vesselsArr;
HANDLE* semVess;
int* vesselsCargoWeight; //vessles cargo weight


//cranes - 
int numOfCranes;
int* cranesId;
HANDLE* CranesArr;
//Array of Scheduling Constraints Semaphores for cranes
HANDLE* semCrane;
int* freeCranes; //array for free cranes in ADT (vessels can load cargo befor them)

//functions - 

//Thread function for each Vessel
DWORD WINAPI vesselSail(PVOID);

//The Thread Function for each crane
DWORD WINAPI operateCrane(PVOID Param);

//Number lottery for the cranes
int RandomNumOfCranes(int vesselsNum);

//Checks if the number is primary
int isPrime(int num);

//Calculates random sleep time beetwen 5 milliseconds to 3 seconds 
int threadSleepTime();

//print currnet time
char* currentTime();

//initialize global data
bool initGlobalVariables(int numOfVess, int numOfCranes);

//Function to operate the entire unloading process, from create cranse to unload func.
void Unloading_Quay(int vessId);

//Function for entering vessels to barrier [before entry to ADT] . 
void EnterToBarrier(vessel_Id);

//Function for entering vessels to ADT
void EnterToADT(int vessel_Id);

//chooce random number for cargo weight between 5-50 tons
int randomCargoWight();

//unlouding vessels cargo front the cranes 
void unloading(int craneId);

//close all treads handles and global data
void cleanupGlobalData(int numOfVess, int numOfCranes);

//there are 2 options - -1 is free, !=-1 is busy
void initArray();

//enter and exit from Canal, each route within it Suez can contain at a given time only one vessel.
int enterVessInterchange(int vessId);
int exitVessInterchange(int lane, int vessId);




int main(void)
{
	CHAR buffer[BUFFER_SIZE] = { 0 };
	DWORD ThreadId;
	int check = 0;
	int currentVessel = 0;

	ReadHandle = GetStdHandle(STD_INPUT_HANDLE);
	WriteHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	/*Have Eilat read from the pipe*/
	if (ReadFile(ReadHandle, buffer, BUFFER_SIZE, &read, NULL))
	{
		fprintf(stderr, "%s Eilat port: Haifa asks for permission for start sailing\n", currentTime());
		numOfVess = atoi(buffer);
		check = isPrime(numOfVess);
		if (check == 1) {
			fprintf(stderr, "%s Eilat port: Eilat allowed the sailing\n", currentTime());
		}
		else
		{
			fprintf(stderr, "%s Eilat port: Eilat denied the sailing\n", currentTime());
			exit(-1);
		}
	}
	else
		fprintf(stderr, "Eilat port: Error reading from pipe\n");
	// sending the converted message to Haifa
	_itoa(check, buffer, DECIMAL);
	if (!WriteFile(WriteHandle, buffer, BUFFER_SIZE, &written, NULL)) {
		fprintf(stderr, "Eilat port: Error writing to pipe\n");
	}

	//get random amout of Cranes
	numOfCranes = RandomNumOfCranes(numOfVess, numOfCranes);
	fprintf(stderr, "Eilat port: numOfCranes = %d \n", numOfCranes);


	//allocate vessel pointers 
	vesselsArr = (HANDLE*)malloc(numOfVess * sizeof(HANDLE));
	MEMALLOC(vesselsArr);
	vesselsID = (int*)malloc(numOfVess * sizeof(int));
	MEMALLOC(vesselsID);
	semVess = (HANDLE*)malloc(numOfVess * sizeof(HANDLE));
	MEMALLOC(semVess);
	vesselsCargoWeight = (int*)malloc(numOfVess * sizeof(int));
	MEMALLOC(vesselsCargoWeight);

	//allocate Crane pointers
	cranesId = (int*)malloc(numOfCranes * sizeof(int));
	MEMALLOC(cranesId);
	CranesArr = (HANDLE*)malloc(numOfCranes * sizeof(HANDLE));
	MEMALLOC(CranesArr);
	semCrane = (HANDLE*)malloc(numOfCranes * sizeof(HANDLE));
	MEMALLOC(semCrane);
	freeCranes = (int*)malloc(numOfCranes * sizeof(int));
	MEMALLOC(freeCranes);
	//Initialise Train vesselss Interchange Lanes DB
	initArray();

	if (!initGlobalVariables(numOfVess, numOfCranes)) {
		fprintf(stderr, "Eilat port: failed to create semaphore/mutex.\n");

	}


	//Initialize cranes
	for (int k = 0; k < numOfCranes; k++)
	{
		freeCranes[k] = -1; //Simulates a free LANE, where no cranes are used

		//Create Thread	Producers 
		cranesId[k] = k + 1;
		CranesArr[k] = CreateThread(NULL, 0, operateCrane, &cranesId[k], 0, &ThreadId);
		if (CranesArr[k] == NULL)
		{
			printf("Eilat port: main::Unexpected Error in Thread Creation\n");
			return 1;
		}
	}

	//Initialize vessels
	for (int i = 0; i < numOfVess; i++)
	{
		//fprintf(stderr, "buffer2 - %s \n", buffer);
		if (ReadFile(ReadHandle, buffer, BUFFER_SIZE, &read, NULL))
		{
			currentVessel = atoi(buffer);
			fprintf(stderr, "%s vessel :%d - arrived @ Eilat port \n", currentTime(), atoi(buffer));
		}
		else {
			fprintf(stderr, "Eilat port: vessel pointers - Error reading from pipe\n");
			return 1;
		}
		//Create Thread	Producers 
		vesselsID[currentVessel - 1] = currentVessel;
		vesselsArr[i] = CreateThread(NULL, 0, vesselSail, &vesselsID[currentVessel - 1], 0, &ThreadId);

		if (vesselsArr[i] == NULL)
		{
			fprintf(stderr, "Eilat port: main::Unexpected Error in Thread Creation\n");
			return 1;
		}
	}


	/*Wait for threads to close*/
	WaitForMultipleObjects(numOfVess, vesselsArr, TRUE, INFINITE);
	WaitForMultipleObjects(numOfCranes, CranesArr, TRUE, INFINITE);

	//all vessels, who are in Eilat port, have finished their duties in this port
	cleanupGlobalData(numOfVess, numOfCranes);

	CloseHandle(ReadHandle);
	CloseHandle(WriteHandle);

	fprintf(stderr, "%s Eilat port - Exiting..... \n", currentTime());
	return 0;
}

char* currentTime() {
	time_t current_time;
	struct tm* time_info;

	time(&current_time);
	time_info = localtime(&current_time);

	strftime(timeString, sizeof(timeString), "[%H:%M:%S]", time_info);
	return timeString;
}

int threadSleepTime()
{
	srand((unsigned int)time(NULL));
	 int sleep = (rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME + 1)) + MIN_SLEEP_TIME;
	return sleep;
}

int isPrime(int num)
{
	for (int i = 2; i < num; i++)
	{
		if ((num % i) == 0)
			return 1;
	}
	return 0;
}


DWORD WINAPI vesselSail(PVOID Param) {
	int lane;
	int VesselID = *(int*)Param;
	int exitADT = 0;
	//sleep
	Sleep(threadSleepTime());
	char msgback[BUFFER_SIZE] = { 0 };

	//enter Id to unloading queue, include barrier and ADT.
	Unloading_Quay(VesselID);
	fprintf(stderr, "%s vessel %d exit from Unloading Quay\n", currentTime(), VesselID);
	WaitForSingleObject(mutexExitFromADT, INFINITE);
	exitADT++;
	if (exitADT == numOfCranes)
	{
		exitADT = 0;

		for (int i = 0; i < numOfCranes; i++) {
			if (!ReleaseSemaphore(semBarrIn, 1, NULL)) {
				fprintf(stderr, "Eilat port: semBarrIn::Unexpected error releasing semaphore Barrier\n");
				return 1;
			}
		}
	}
	if (!ReleaseMutex(mutexExitFromADT)) {
		printf("Eilat port: vesselSail::Unexpected error mutexExitFromADT.V()\n");
		return 1;
	}

	Sleep(threadSleepTime());


	lane = enterVessInterchange(VesselID);

	//Error Handling
	if (lane == -1)
	{
		printf("Eilat port: Unexpected Error Thread %d Entering\n", VesselID);
		return 1;
	}

	// sail back to Haifa port
	fprintf(stderr, "%s Eilat port: vessel %d entering canal: Red sea => Med. sea\n", currentTime(), VesselID);
	Sleep(threadSleepTime());



	if (!exitVessInterchange(lane, VesselID))
	{
		printf("Eilat port: Unexpected Error Thread %d Exiting\n", VesselID);
		return 1;
	}

	fprintf(stderr, "%s Eilat port:  vessel %d exiting canal: Red sea => Med. sea\n", currentTime(), VesselID);
	WaitForSingleObject(mutex, INFINITE);

	_itoa(VesselID, msgback, DECIMAL);
	if (!WriteFile(WriteHandle, msgback, BUFFER_SIZE, &written, NULL)) {
		fprintf(stderr, "Eilat port: Error writing to pipe\n");
		return 1;
	}
	if (!ReleaseMutex(mutex)) {
		fprintf(stderr, "Eilat port: Error::Vessel %d while releasing mutex\n", VesselID);
		return 1;
	}

	return 0;
}

int RandomNumOfCranes(int amountOfvessels)
{
	srand((unsigned int)time(NULL));
	int random = ((rand()) % (amountOfvessels - 2)) + 2;
	while (amountOfvessels % random != 0)
		random = ((rand()) % (amountOfvessels - 2)) + 2;
	return random;
}

DWORD WINAPI operateCrane(PVOID Param) {
	int craneID = *(int*)Param;
	fprintf(stderr, "%s Eilat port Crane %d is created\n", currentTime(), craneID);
	int numOfRounds = (numOfVess / numOfCranes);
	//fprintf(stderr, "%s Eilat port - numOfRounds : %d\n", currentTime(), numOfRounds);
	for (int i = 0; i < numOfRounds; i++)
	{
		WaitForSingleObject(semCrane[craneID - 1], INFINITE);
		unloading(craneID);
		Sleep(threadSleepTime());
	}
	return 0;
}

bool initGlobalVariables(int numOfVess, int numOfCranes) {

	for (int i = 0; i < numOfVess; i++)
	{
		//fprintf(stderr, "i = %d \n", i);
		semVess[i] = CreateSemaphore(NULL, 0, 1, NULL);
		//fprintf(stderr, "create new semaphore \n");
		if (semVess[i] == NULL)
		{
			fprintf(stderr, "Eilat port: main::Unexpected Error in semVess semaphore Creation\n");
			return false;
		}
	}
	for (int i = 0; i < numOfCranes; i++)
	{
		//fprintf(stderr, "i = %d \n", i);
		semCrane[i] = CreateSemaphore(NULL, 0, 1, NULL);
		//fprintf(stderr, "create new semaphore \n");
		if (semCrane[i] == NULL)
		{
			fprintf(stderr, "Eilat port: main::Unexpected Error in semCranes semaphore Creation\n");
			return false;
		}
	}

	mutex = CreateMutex(NULL, FALSE, NULL);
	if (mutex == NULL)
	{
		printf("Eilat port: Main::Unexpected Error in Mutex Creation\n");
		return false;
	}

	mutexEnterAdt = CreateMutex(NULL, FALSE, NULL);
	if (mutexEnterAdt == NULL)
	{
		printf("Eilat port: Main::Unexpected Error in MutexEnterAdt Creation\n");
		return false;
	}

	semBarrOut = CreateMutex(NULL, FALSE, NULL);
	if (semBarrOut == NULL)
	{
		printf("Eilat port: Main::Unexpected Error in semBarrierOut Creation\n");
		return false;
	}

	semBarrIn = CreateMutex(NULL, FALSE, NULL);
	if (semBarrIn == NULL)
	{
		printf("Eilat port: Main::Unexpected Error in semBarrierIn Creation\n");
		return false;
	}

	mutexUnload = CreateMutex(NULL, FALSE, NULL);
	if (mutexUnload == NULL)
	{
		printf("Eilat port: Main::Unexpected Error in MutexUnload Creation\n");
		return false;
	}


	mutexExitFromADT = CreateMutex(NULL, FALSE, NULL);
	if (mutexExitFromADT == NULL)
	{
		printf("Eilat port: Main::Unexpected Error in MutexExitFromADT Creation\n");
		return false;
	}

	lanesSemaphore = CreateSemaphore(NULL, LANE_NUM, LANE_NUM, NULL);
	if (lanesSemaphore == NULL)
	{
		printf("Eilat port: main::Unexpected Error in lanesSemaphore Creation\n");
		return 1;
	}

	//Creating the Mutex Semaphore
	lnMutex = CreateMutex(NULL, FALSE, NULL);
	if (lnMutex == NULL)
	{
		printf("Eilat port: main::Unexpected Error in lnMutex Creation\n");
		return 1;
	}

	return true;
}

void Unloading_Quay(int vessId) {
	EnterToBarrier(vessId);
	EnterToADT(vessId);
	// the current vessel exit from ADT
	//it will take some time...
	Sleep(threadSleepTime());
	fprintf(stderr, "%s Eilat Port: Vessel: %d - exit from ADT.\n", currentTime(), vessId);
}

void EnterToBarrier(int vessel_Id)
{
	//The conditions under which the first M vessels to arrive must be released immediately -
	//the unloading wharf, completely vacant and the number of vessels waiting in the Barrier is at least M (=numOfCranes).

	fprintf(stderr, "%s Eilat Port: Vessel: %d - entered to Barrier.\n", currentTime(), vessel_Id);
	WaitForSingleObject(semBarrIn, INFINITE); //all vessels need to wait until they turn to enter to ADT
	WaitForSingleObject(mutex, INFINITE);
	BarrierCounter++;
	if (!ReleaseMutex(mutex)) {
		printf("Eilat port: barrier::Unexpected error mutex.V()\n");
		return 1;
	}
	WaitForSingleObject(mutex, INFINITE);
	if (BarrierCounter >= numOfCranes && ADTCounter == 0)
	{
		BarrierCounter = 0;
		for (int i = 0; i < numOfCranes; i++) {
			if (!ReleaseSemaphore(semBarrOut, 1, NULL)) {
				fprintf(stderr, "Eilat port: semBarrOut::Unexpected error releasing semaphore Barrier\n");
				return 1;
			}
		}
	}
	if (!ReleaseMutex(mutex)) {
		printf("Eilat port: barrier::Unexpected error mutex.V()\n");
		return 1;
	}

	WaitForSingleObject(semBarrOut, INFINITE);
}


void EnterToADT(int vessId) {
	//Simultaneous insertion of M vessels each time the platform ADT is vacated.
	fprintf(stderr, "%s Eilat Port : Vessel %d entered to ADT.\n", currentTime(), vessId);
	Sleep(threadSleepTime());
	WaitForSingleObject(mutexEnterAdt, INFINITE);
	if ((ADTCounter + 1) > (numOfCranes) || (ADTCounter) == (numOfCranes))
	{
		ADTCounter = 0;
	}
	freeCranes[ADTCounter] = vessId;
	fprintf(stderr, "%s ADT - Vessel: %d unloads the cargo in front of crane: %d \n", currentTime(), freeCranes[ADTCounter], ADTCounter + 1); //ADTConter+1 cause caren id need to start from 1..
	Sleep(threadSleepTime());
	ADTCounter++;
	if (!ReleaseMutex(mutexEnterAdt)) {
		printf("Eilat port: EnterToADT::Unexpected error mutexAdt.V()\n");
		return 1;
	}
	vesselsCargoWeight[vessId - 1] = randomCargoWight();
	fprintf(stderr, "%s unloading - cargo of vessels %d is %d tons \n", currentTime(), vessId, vesselsCargoWeight[vessId - 1]);
	Sleep(threadSleepTime());
	if (!ReleaseSemaphore(semCrane[ADTCounter - 1], 1, NULL)) {
		fprintf(stderr, "Eilat port: Error::EnterToADT semCrane while releasing vessel number %d.\n", vessId);
		return 1;
	}
	WaitForSingleObject(semVess[(vessId - 1)], INFINITE);
}

void unloading(int craneId)
{
	int vessId;
	WaitForSingleObject(mutexUnload, INFINITE); // Wait CraneDB is free for changing
	vessId = freeCranes[craneId - 1];
	fprintf(stderr, "%s Eilat Port: Crane %d unloaded cargo of weight %d tons, from vessel number %d.\n", currentTime(), craneId, vesselsCargoWeight[vessId - 1], vessId);
	Sleep(threadSleepTime());
	fprintf(stderr, "%s Eilat Port: Crane %d done to unloaded cargo \n", currentTime(), craneId);
	// empty cargo from freeCrane array
	for (int i = 0; i < numOfCranes; i++) {
		if (freeCranes[i] == vessId) {
			freeCranes[i] = -1;
			break;
		}
	}
	if (!ReleaseSemaphore(semVess[(vessId - 1)], 1, NULL))
		fprintf(stderr, "Eilat port: Error::Releasing Vessel %d semaphore.\n", vessId);
	if (!ReleaseMutex(mutexUnload))
		printf("Eilat port: Release mutexUnload error: %d\n", GetLastError());

}

int randomCargoWight()
{
	srand((unsigned int)time(NULL));
	int cargo;
	return cargo = (rand() % (MAX_CARGO - MIN_CARGO + 1)) + MIN_CARGO;
}

void cleanupGlobalData(int numOfVess, int numOfCranes)
{
	for (int i = 0; i < numOfVess; i++) {
		CloseHandle(vesselsArr[i]);
	}
	fprintf(stderr, "%s Eilat port: all vessel threads are done \n", currentTime());
	for (int i = 0; i < numOfCranes; i++) {
		CloseHandle(CranesArr[i]);
	}
	fprintf(stderr, "%s Eilat Port: All Crane Threads are done\n", currentTime());
	CloseHandle(mutex);
	CloseHandle(mutexUnload);
	CloseHandle(mutexEnterAdt);
	CloseHandle(mutexExitFromADT);
	CloseHandle(lnMutex);
	CloseHandle(lanesSemaphore);
	CloseHandle(semBarrIn);
	CloseHandle(semBarrOut);
	for (int i = 0; i < numOfVess; i++)
		CloseHandle(semVess[i]);
	for (int i = 0; i < numOfCranes; i++)
		CloseHandle(semCrane[i]);
	//free all pointers.
	free(vesselsArr);
	free(vesselsID);
	free(semVess);
	free(vesselsCargoWeight);
	free(cranesId);
	free(CranesArr);
	free(semCrane);
	free(freeCranes);
}

//Init  to -1 ----> lan is free
void initArray()
{
	int i;
	for (i = 0; i < LANE_NUM; i++)
		interchangeLanes[i] = -1;
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
		printf("Eilat port: enterVessInterchange::Unexpected error lnMutex.V()\n");


	if (availableLane >= LANE_NUM)
	{
		printf("Eilat port: enterVessInterchange::Unexpected ERROR!!!\n");
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
		printf("Eilat port: exitVessInterchange::Unexpected ERROR!!!\n");
		ret = False;
	}
	else
		interchangeLanes[lane] = -1;


	//lnMutex.V() exiting Critical Section
	if (!ReleaseMutex(lnMutex))
		printf("Eilat port: exitVessInterchange::Unexpected error lnMutex.V()\n");

	//lanesSemaphore.V() to signal a Train waiting for a lane on this Semaphore
	//Must be called after DB is updated, as otherwise you might signal someone before DB is updated and it won't find an 
	//available lane in DB to update!
	if (!ReleaseSemaphore(lanesSemaphore, 1, NULL))
		printf("Eilat port: exitVessInterchange::Unexpected error lanesSemaphore.V()\n");

	return ret;

}
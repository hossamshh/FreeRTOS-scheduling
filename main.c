#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "tm4c123gh6pm.h"

#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_types.h"

#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/rom.h"
#include "driverlib/uart.h"
#include "utils/uartstdio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define GPIO_PA0_U0RX           0x00000001
#define GPIO_PA1_U0TX           0x00000401

#define N												5
#define TST											1
#define TSTslice								50*TST
#define LATEST_ARRIVAL_TIME 		15
#define MAX_COMPUTATION_TIME		8
#define MAX_PERIOD_MULTIPLIER 	17
#define MAX_STR_LEN							30

struct tprams{
	char index;
	char btime;
};

int TSTticks = 0;

int n;

int* TA;
int* TC;
int* TP[2];

bool safeMode = true;

xTaskHandle* THs;
xQueueHandle myQueue;

char* stringToPrint[] = {"Task 1 is running!", "Task 2 is running!", "Task 3 is running!", "Task 4 is running!", "Task 5 is running!"};

void ConfigureUART(void) {
	//
	// Enable the GPIO Peripheral used by the UART.
	//
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

	//
	// Enable UART0
	//
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

	//
	// Configure GPIO Pins for UART mode.
	//
	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

	//
	// Use the internal 16MHz oscillator as the UART clock source.
	//
	UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);

	//
	// Initialize the UART for console I/O.
	//
	UARTStdioConfig(0, 115200, 16000000);
}

void merge(int* arr[], int l, int m, int r) { 
	int i, j, k; 
	int n1 = m - l + 1; 
	int n2 = r - m; 
	/* create temp arrays */
	int* L[2];
	L[0] = pvPortMalloc(n1*sizeof(int));
	L[1] = pvPortMalloc(n1*sizeof(int));
	int* R[2];
	R[0] = pvPortMalloc(n2*sizeof(int));
	R[1] = pvPortMalloc(n2*sizeof(int));
	/* Copy data to temp arrays L[] and R[] */
	for (i = 0; i < n1; i++) {
		L[0][i] = arr[0][l + i];
		L[1][i] = arr[1][l + i];
	}
	for (j = 0; j < n2; j++) {
		R[0][j] = arr[0][m + 1+ j];
		R[1][j] = arr[1][m + 1+ j];
	}
	/* Merge the temp arrays back into arr[l..r]*/
	i = 0; // Initial index of first subarray 
	j = 0; // Initial index of second subarray 
	k = l; // Initial index of merged subarray 
	while (i < n1 && j < n2) { 
		if (L[1][i] <= R[1][j]) { 
			arr[1][k] = L[1][i]; 
			arr[0][k] = L[0][i];
			i++; 
		} 
		else { 
			arr[1][k] = R[1][j]; 
			arr[0][k] = R[0][j];
			j++; 
		} 
		k++; 
	} 
	/* Copy the remaining elements of L[], if there 
	are any */
	while (i < n1) { 
		arr[1][k] = L[1][i]; 
		arr[0][k] = L[0][i];
		i++; 
		k++; 
	} 
	/* Copy the remaining elements of R[], if there 
	are any */
	while (j < n2) { 
		arr[1][k] = R[1][j]; 
		arr[0][k] = R[0][j];
		j++; 
		k++; 
	}
	vPortFree(L[0]);
	vPortFree(L[1]);
	vPortFree(R[0]);
	vPortFree(R[1]);
} 

void mergeSort(int* arr[], int l, int r) { 
	if (l < r) { 
		// Same as (l+r)/2, but avoids overflow for 
		// large l and h 
		int m = l+(r-l)/2; 
		// Sort first and second halves 
		mergeSort(arr, l, m); 
		mergeSort(arr, m+1, r); 
		merge(arr, l, m, r); 
	} 
}  


void initRandomNumbers(){
	srand(34);
	n = rand() % N;
	UARTprintf("\n");
	UARTprintf("Number of Tasks n: %d\n", n);
	UARTprintf("TA\tTC\tTP\tTask\n");
	
	TA = pvPortMalloc(n*sizeof(int));
	TC = pvPortMalloc(n*sizeof(int));
	TP[0] = pvPortMalloc(n*sizeof(int));
	TP[1] = pvPortMalloc(n*sizeof(int));
	
	for(int i=0; i<n; i++){
		TA[i] = (rand() % (LATEST_ARRIVAL_TIME-1)) + 1;
		TC[i] = (rand() % (MAX_COMPUTATION_TIME-1)) + 1;
		TP[0][i] = i;
		if(safeMode)
			TP[1][i] = ((rand() % (MAX_PERIOD_MULTIPLIER-3)) + 3)*TC[i];
		else
			TP[1][i] = ((rand() % 7) + 3)*TC[i];

		UARTprintf("%d\t%d\t%d\t%d\n", TA[i], TC[i], TP[1][i], i+1);
	}
}

bool testSchedulablity(){
	int cpuU = 0;
	for(int i=0; i<n; i++)
		cpuU += (TC[TP[0][i]]*10000)/TP[1][i];
	if(cpuU < 10000)
		UARTprintf("CPU Utilization = 0.%d\n", cpuU);
	else {
		int n = cpuU-cpuU/10000;
		UARTprintf("CPU Utilization = %d.%d\n", n, cpuU-n);
	}
	return cpuU < 7000;
}

void printArray(int* A[], int size) { 
	int i; 
	for (i=0; i < size; i++) {
		UARTprintf("%d %d\n", A[0][i], A[1][i]); 
	}
}

void UARTprintGateKeeper(){
	char* message;
	portBASE_TYPE xStatus;
	while(1){
		char c[5];
		char a[] = "TST: ";
		sprintf(c, "%d", TSTticks);
		strcat(a, c);
		UARTprintf(a);
		UARTprintf("\n");
		TSTticks++;

		xStatus = xQueueReceive(myQueue, &message, 0);
		if(xStatus == pdPASS){
			UARTprintf(message);
			UARTprintf("\n");
		}

		vTaskDelay(TSTslice);
	}
}

void myTask(void *pvParameters){
	struct tprams* prams = pvParameters;
	while(1){
		portBASE_TYPE xStatus = xQueueSendToBack(myQueue, &(stringToPrint[prams->index]), 0);
		if(xStatus != pdPASS){
			UARTprintf("Queue error\n");
		}
		vTaskDelay(TP[1][prams->btime]*TSTslice);
	}
}

void myScheduler(void *pvParameters){
	struct tprams tpram;
	while(1){
		for(int i=0; i<n; i++){
			int index = TP[0][i];
			if(TSTticks == TA[index]){
				tpram.index = index;
				tpram.btime = i;
				xTaskCreate(myTask, NULL, 128, (void*)&tpram, n-index+2, NULL);
			}
		}
		vTaskDelay(TSTslice);
	}
}


int main(){
  SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);
  ConfigureUART();

	initRandomNumbers();
	mergeSort(TP, 0, n-1);
	printArray(TP, n);
	
	if(testSchedulablity()){
		UARTprintf("Schedulablity check is ok\nTasks will run\n");
		myQueue = xQueueCreate(n+1, sizeof(char*));
		xTaskCreate(UARTprintGateKeeper, NULL, 128, NULL, 2, NULL);
		xTaskCreate(myScheduler, NULL, 128, NULL, 1, NULL);
		
		vTaskStartScheduler();
	}
	
	return 0;
}
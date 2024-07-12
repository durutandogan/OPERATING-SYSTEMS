#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdbool.h>  
#include <string.h>   
 /****************************************************************************** 
  pthread_sleep takes an integer number of seconds to pause the current thread 
  original by Yingwu Zhu
  updated by Muhammed Nufail Farooqi
  *****************************************************************************/
int pthread_sleep (int seconds)
{
    pthread_mutex_t mutex;
    pthread_cond_t conditionvar;
    struct timespec timetoexpire;
    if(pthread_mutex_init(&mutex,NULL))
    {
        return -1;
    }
    if(pthread_cond_init(&conditionvar,NULL))
    {
        return -1;
    }
    struct timeval tp;
    //When to expire is an absolute time, so get the current time and add //it to our delay time
    gettimeofday(&tp, NULL);
    timetoexpire.tv_sec = tp.tv_sec + seconds; 
    timetoexpire.tv_nsec = tp.tv_usec * 1000; 

    pthread_mutex_lock (&mutex);
    int res =  pthread_cond_timedwait(&conditionvar, &mutex, &timetoexpire); //thread sleep for time
    
    pthread_mutex_unlock(&mutex);
    
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&conditionvar);

    //Upon successful completion, a value of zero shall be returned
    return res;

}

// a struct for planes containing necessary components 
typedef struct {
  int id;
  bool isLanding;
  time_t requestTime;
} Plane;

// initialization of variables
pthread_mutex_t mutex; // exclusive lock
pthread_cond_t condVar; 
int runwayAvailable = 1; // lock availability for the single runaway 
Plane* landingQueue[256]; // a queue to store landing plane structs
Plane* takeOffQueue[256]; // a queue to store departing plane structs
int landingCount = 0; // to see the number of planes which are landing
int takeOffCount = 0; // to see the number of planes which are departing
//Logging files
FILE* planeLog;
FILE* towerLog;
//min, max time for t
int min_t = 1;
int max_t = 5;

struct timeval simulationStart;

void* controlTower(void* arg){

    int simulationTime = *((int*)arg);
    struct timeval now;
    gettimeofday(&now, NULL);
    time_t startTime = now.tv_sec; //initialize start time
    
    Plane *p = NULL; // initialize the null plane that will be used in the control tower 
    
    // run the simulation until the end of the specified time
    while(now.tv_sec - startTime < simulationTime){
          //printf("Control tower locking mutex\n");
          pthread_mutex_lock(&mutex); //lock mutex
          //printf("Control tower locked mutex\n"); 
        
          //waits until runway is available (critical section available)
          while(runwayAvailable && landingCount== 0 && takeOffCount == 0) {
                printf("Control tower waiting\n");
                pthread_cond_wait(&condVar, &mutex);
          }
          
          gettimeofday(&now, NULL);
          time_t current_time = now.tv_sec - startTime; //get current time
          
          int t = (rand() % (max_t - min_t + 1)) + min_t; //randomly assign t seconds in the specified interval

          // below part handles the given instruction:
          // The control tower favors the landing planes and lets the waiting planes in the air to land until one of following conditions hold
          // (a) No more planes is waiting to land,
          // (b) 5 planes or more on the ground are lined up to take off

          // to avoid starvation to the planes in the air, we added a threshold waiting time for the landing processes
          if (landingCount > 0 && (current_time - (landingQueue[0]->requestTime)) > 5) {

              p = landingQueue[0]; // get the current plane form the landing queue
              printf("Prioritized Plane landing with id %d\n", p->id);
              printf("For plane %d, %ld seconds of time has passed\n", p->id, current_time - p->requestTime);
              
              // update the landing queue
              for (int i = 0; i < landingCount -1 ; i++){
                  landingQueue[i] = landingQueue[i+1];
              }
              landingCount--;
              
              printf("Plane %d landing.\n", p->id);
        
              runwayAvailable = 0; //lock granted
        
              pthread_mutex_unlock(&mutex);
          
              pthread_sleep(2*t); //2t seconds to land
          
              pthread_mutex_lock(&mutex);
          
              printf("Current takeoff queue count: %d\n", takeOffCount); // used for debugging purposes 

              // logging for landing
              time_t runwayTime = now.tv_sec - simulationStart.tv_sec;
              int turnaround = runwayTime - p->requestTime;
              
              // formatting and directing the log outputs to a file
              fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "E", p->requestTime, runwayTime, turnaround);
              fflush(planeLog);
              
              runwayAvailable = 1; //available again
              
              printf("Plane %d landed.\n", p->id);

              
          }
         
          // condition b, if 5 or more planes are on the ground waiting for take off prioritize them for take off 
          if (takeOffCount >= 5){
              printf("Take Off prioritized\n");
              p = takeOffQueue[0];

               // update the take off queue
              for (int i = 0; i < takeOffCount - 1; i++){
                  takeOffQueue[i] = takeOffQueue[i + 1];
              }
              takeOffCount--;
              
              printf("Current takeoff queue count: %d\n", takeOffCount); 
              printf("Plane %d taking off.\n", p->id);
        
              runwayAvailable = 0; // lock granted
        
              pthread_mutex_unlock(&mutex);
          
              pthread_sleep(2*t); // 2t seconds to land
          
              pthread_mutex_lock(&mutex);
              
              //Logging for take off
              time_t runwayTime = now.tv_sec - simulationStart.tv_sec;
              int turnaround = runwayTime - p->requestTime;
              //Logging for emergency planes
              if ( ((p->id) % 2) == 0){
                    fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "L", p->requestTime, runwayTime, turnaround);
              }
              else{
                    fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "D", p->requestTime, runwayTime, turnaround);
              }
              fflush(planeLog);
              runwayAvailable = 1; //available again
              
              printf("Plane %d took off.\n", p->id);
          }
          
          // this part deals with the cases where there are no violations to the priority cases
          // in normal conditions prioritize landing planes over departuring ones 
          else {
          //Prioritize landing planes, when there are no specifications
          if (landingCount > 0){
              p = landingQueue[0]; 
          
          for (int i = 0 ;i < landingCount - 1 ; i++){
              landingQueue[i] = landingQueue[i+1];
          }
          landingCount--; 
          
          printf("Plane %d landing.\n", p->id);
        
          runwayAvailable = 0; // lock granted
        
          pthread_mutex_unlock(&mutex);
          
          pthread_sleep(2*t); // 2t seconds to land
          
          pthread_mutex_lock(&mutex);
          
          runwayAvailable = 1; // available again
          printf("Current takeoff queue count: %d\n", takeOffCount);

          //Logging for landing
          time_t runwayTime = now.tv_sec - simulationStart.tv_sec;
          int turnaround = runwayTime - p->requestTime;
          if ( ((p->id) % 2) == 0){
                    fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "L", p->requestTime, runwayTime, turnaround);
              }
              else{
                    fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "D", p->requestTime, runwayTime, turnaround);
              }
          fflush(planeLog);
          printf("Plane %d landed.\n", p->id);
       }
        
       // Take Off case
       else if (takeOffCount > 0) {
          p = takeOffQueue[0];
          
          for (int i = 0 ;i < takeOffCount - 1; i++){
              takeOffQueue[i] = takeOffQueue[i+1];
          }
          
          takeOffCount--;
          printf("Current takeoff queue count: %d\n", takeOffCount);
          printf("Plane %d taking off.\n", p->id);
          runwayAvailable = 0; //lock granted
          
          pthread_mutex_unlock(&mutex);
          
          pthread_sleep(2*t);  // 2t seconds to take off
          
          pthread_mutex_lock(&mutex);
          
          runwayAvailable = 1;
          
          //Logging for takeoff
          time_t runwayTime = now.tv_sec - simulationStart.tv_sec;
          int turnaround = runwayTime - p->requestTime;
          if ( ((p->id) % 2) == 0){
                    fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "L", p->requestTime, runwayTime, turnaround);
              }
              else{
                    fprintf(planeLog, "%-8d | %-7s | %-13ld | %-12ld | %-15d\n", p->id, "D", p->requestTime, runwayTime, turnaround);
              }
          fflush(planeLog);
          printf("Plane %d took off.\n", p->id);
      }
      }
    
    pthread_cond_broadcast(&condVar); //alert other planes to land/takeoff
    
    pthread_mutex_unlock(&mutex);
    
  }
  printf("current queue counts, landing: %d, take off: %d\n", landingCount, takeOffCount);
  return NULL;  
}

void* plane(void* arg){ 
    //planes are threads, they need airline (locks) to land or take off
    int id = *((int*)arg); // unique ids for planes
    free(arg);
    bool isLanding = rand() % 2; // randomly assign conditions to the plane
    
    struct timeval now;
    gettimeofday(&now, NULL);
    time_t requestTime = now.tv_sec - simulationStart.tv_sec;
    
    // create the plane struct and assign determined values for it's fields
    Plane* p = (Plane*) malloc(sizeof(Plane));
    p->id = id;
    p->isLanding = isLanding;
    p->requestTime = requestTime; // to use it with logging
    
    pthread_mutex_lock(&mutex);
    
    // according to the type of the plane, assign them to the landing or departure queues

    if(isLanding) {
        landingQueue[landingCount++] = p;
        printf("Plane %d wants to land.\n", p->id);
    }
    else {
        takeOffQueue[takeOffCount++] = p;
        printf("Plane %d wants to take off.\n", p->id);
        printf("Plane %d added to takeoff queue. Current takeoff count: %d\n", p->id, takeOffCount);

    }
    
    pthread_mutex_unlock(&mutex);
    pthread_cond_signal(&condVar); //signal to the control tower
    
    return NULL;
}


// main to execute all
int main(int argc, char* argv[]){
    
    gettimeofday(&simulationStart, NULL);
    
    //sample log files for 60 sec simulation for p=0.5
    
    int simulationTime = atoi(argv[2]);
    float probability = atof(argv[4]); 
    int interval = atoi(argv[6]);
    
    printf("Starting simulation with simulation time=%d and probability=%.2f\n", simulationTime, probability); // inform the user 
    
    struct timeval simStartTime;
    gettimeofday(&simStartTime, NULL);
    
    // command line argument -s to indicate the total simulation time (e.g. -s 200) and -p for probability
    for (int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-s") == 0) {
        simulationTime = atoi(argv[i + 1]);
        i++;  
    }
    else if (strcmp(argv[i], "-p") == 0) {
        probability = atof(argv[i + 1]);
        i++;
    }
    else if (strcmp(argv[i], "-n") == 0) {
        interval = atoi(argv[i + 1]);
        i++;
    }
}

        
      // open log files
      planeLog = fopen("planes.log", "w");
      towerLog = fopen("tower.log", "w");
      
      if (planeLog == NULL || towerLog == NULL) {
          perror("error opening files");
          return -1;
      }
      
      //headers for planes.log
      fprintf(planeLog, "%-8s | %-7s | %-13s | %-12s | %-15s\n", "PlaneID", "Status", "Request Time", "Runway Time", "Turnaround Time");
      fflush(planeLog);
      
      srand(time(NULL));
       
      // initialize control tower and mutex
      pthread_t tower;
      pthread_mutex_init(&mutex, NULL);
      pthread_cond_init(&condVar, NULL);
        
      // create control tower
      pthread_create(&tower, NULL, controlTower, &simulationTime);
        
      // create planes with probability p
      for (int i = 0; i < simulationTime ; i++){
  
          if (((float)rand() / RAND_MAX) < probability) {
              
             
              int* id = malloc(sizeof(int));
              *id = i;
              printf("Creating a new plane with id %d\n", *id);
              
              pthread_t planeThread;
              pthread_create(&planeThread, NULL, plane, id);
              pthread_detach(planeThread);
          
          }
            
          pthread_sleep(1); // new plane at every second
        
          // Log snapshot at intervals
          if (i % interval == 0) {
            pthread_mutex_lock(&mutex);
            fprintf(towerLog, "At %d sec ground: ", i);
            for (int j = 0; j < takeOffCount; j++) {
                fprintf(towerLog, "%d ", takeOffQueue[j]->id);
            }
            fprintf(towerLog, "\nAt %d sec air: ", i);
            for (int j = 0; j < landingCount; j++) {
                fprintf(towerLog, "%d ", landingQueue[j]->id);
            }
            fprintf(towerLog, "\n");
            fflush(towerLog); 
            pthread_mutex_unlock(&mutex);
        }
      }
        
      pthread_join(tower, NULL); // for the tower to be the main thread  
      pthread_mutex_destroy(&mutex);
      pthread_cond_destroy(&condVar);
      
      fclose(planeLog);
      fclose(towerLog);
      return 0;
        
  }
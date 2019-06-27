#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>
/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */
/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */
/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *intersectionSem;
#define MAX_THREADS 10

typedef struct Vehicles
{
  Direction origin;
  Direction destination;
} Vehicle;

static struct cv *intersectionCV;
static struct lock *intersectionLock;
static struct array *intersectionCar;

bool right_turn(Direction origin, Direction destination);
bool intersection_check(Vehicle *V1, Vehicle *V2);

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  /*intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }*/
  intersectionCV = cv_create("intersectionCV");
  if(intersectionCV == NULL) {
    panic("could not create intersection CV");
  }

  intersectionLock = lock_create("intersectionLock");
  if(intersectionLock == NULL){
    panic("could not create a intersectionLock");
  }

  intersectionCar = array_create();
  array_init(intersectionCar);
  if(intersectionCar == NULL){
    panic("could not create intersection car array");
  }

  return;
}
/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  //KASSERT(intersectionSem != NULL);
  //sem_destroy(intersectionSem);
  KASSERT(intersectionCV != NULL);
  cv_destroy(intersectionCV);

  KASSERT(intersectionLock != NULL);
  lock_destroy(intersectionLock);

  KASSERT(intersectionCar != NULL);
  array_cleanup(intersectionCar);
  array_destroy(intersectionCar);


}

bool
right_turn(Direction origin, Direction destination) 
{
  return ((origin == north && destination == west) ||
         (origin == south && destination == east) ||
         (origin == east && destination == north) ||
         (origin == west && destination == south));
}

bool
intersection_check(Vehicle *V1, Vehicle *V2)
{
    if(V1->origin == V2->origin) {
      return true;
    } else if (V1->origin == V2->destination && V1->destination == V2->origin) {
      return true;
    } else if ((V1->destination != V2->destination && right_turn(V2->origin, V2->destination))) {
      return true;
    } else {
      return false;
    }

    return 0;

}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */
void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  //(void)origin;  /* avoid compiler complaint about unused parameter */
  //(void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //P(intersectionSem);
  KASSERT(intersectionCV != NULL);
  KASSERT(intersectionLock != NULL);
  KASSERT(intersectionCar != NULL);

  //create a new vehicle
  Vehicle *V;
  V = kmalloc(sizeof(Vehicle));
  V->origin = origin;
  V->destination = destination;
	kprintf("Incoming:%d %d \n",origin,destination);
  lock_acquire(intersectionLock);

  for(unsigned int i = 0; i < array_num(intersectionCar); i++){
      Vehicle *existingVehicle; 
      existingVehicle = array_get(intersectionCar, i);
      if(!intersection_check(existingVehicle,V)) {
        while(array_num(intersectionCar) > 0){
          cv_wait(intersectionCV,intersectionLock);
        }
      }
  }

  array_add(intersectionCar,V,NULL);

  lock_release(intersectionLock);

}
/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */
void
intersection_after_exit(Direction origin, Direction destination) 
{
  KASSERT(intersectionCV != NULL);
  KASSERT(intersectionLock != NULL);
  KASSERT(intersectionCar != NULL);

  lock_acquire(intersectionLock);

  kprintf("Exiting:%d %d \n",origin,destination);

  for(unsigned int i=0; i<array_num(intersectionCar); i++) {
    Vehicle *existingVehicle; 
    existingVehicle = array_get(intersectionCar, i);
    if(existingVehicle->origin == origin && existingVehicle->destination == destination) {
      array_remove(intersectionCar,i);
      cv_broadcast(intersectionCV,intersectionLock);

      break;
    }
  }

  lock_release(intersectionLock);

  /* replace this default implementation with your own implementation */
  //(void)origin;  /* avoid compiler complaint about unused parameter */
  //(void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //V(intersectionSem);
}

//
//  COMP1927 Assignment 1 - Memory Suballocator
//  allocator.c ... implementation
//
//  Created by Liam O'Connor on 18/07/12.
//  Modified by John Shepherd in August 2014
//  Copyright (c) 2012-2014 UNSW. All rights reserved.
//
//  Modified by David Chacon and Michael Liu
//  In August 2014

#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define HEADER_SIZE    sizeof(struct free_list_header)  
#define MAGIC_FREE     0xDEADBEEF
#define MAGIC_ALLOC    0xBEEFDEAD

#define TRUE 0
#define FALSE 1
#define MAX_SIZE 4294967295

typedef unsigned char byte;
typedef u_int32_t vlink_t;
typedef u_int32_t vsize_t;
typedef u_int32_t vaddr_t;

typedef struct free_list_header {
   u_int32_t magic;           // ought to contain MAGIC_FREE
   vsize_t size;              // # bytes in this block (including header)
   vlink_t next;              // memory[] index of next free block
   vlink_t prev;              // memory[] index of previous free block
} free_header_t;

// Global data

static byte *memory = NULL;   // pointer to start of suballocator memory
static vaddr_t free_list_ptr; // index in memory[] of first block in free list
static vsize_t memory_size;   // number of bytes malloc'd in memory[]

//Local functions
static vsize_t power_of_two(u_int32_t number);
static free_header_t *reduceMemory (free_header_t *list, u_int32_t size);
static void checkCorruption(free_header_t *current);
static free_header_t *getHeader(vaddr_t index);
static vaddr_t getIndex(free_header_t* current);
static void checkMerge(free_header_t* chunk);
static void merge(free_header_t* chunk);
static int validMerge(vaddr_t index, vsize_t size);

void sal_init(u_int32_t size) {
   //checking if hasn't been initialised yet
   if ((memory == NULL) && (size < MAX_SIZE)) {      
   
   
      //ensures input is a a power of two
      u_int32_t allocated_size = power_of_two(size);
      memory = malloc(allocated_size * sizeof(byte));
      
      if (memory == NULL) {
         fprintf(stderr, "sal_init: insufficient memory");
         abort();
      }
      
      //used to clear all memory to 0 - 
      int i;
      for (i=0; i<memory_size; i++){
         memory[i] = 0;
      }
      
      //setting global variables
      memory_size = allocated_size;
      free_list_ptr = 0;
      
      //setting up header to use memory malloced
      free_header_t *free_list = getHeader(free_list_ptr);
      free_list->magic = MAGIC_FREE;
      free_list->size = allocated_size;
      free_list->next = free_list->prev = 0;
   }
}

//Will make sure malloced area is power of two - returns same number if power of two or next power of two higher than input
static u_int32_t power_of_two(u_int32_t number){
   u_int32_t rounded = 1;
   while (rounded < number){
      rounded = rounded*2;
   }
   return rounded;
}

void *sal_malloc(u_int32_t n){
   if ((n + HEADER_SIZE <= memory_size/2) && (n>0)){
      //sets a pointer to the start of the list for easier reference
      free_header_t *startList = getHeader(free_list_ptr);
      free_header_t *current = startList;
      free_header_t *foundMemory = startList;
            
      //checks corruption of header - will do everytime pointer is moved
      checkCorruption(current);

      //checks if theres only 1 area in memory that there is enough room in it
      //if only one area left and size is 32, then it can't be split and hence can never be given away 
      if ((startList == getHeader(startList->next)) && (startList->size/2 < n+HEADER_SIZE)){
         return NULL;
      }
      
      foundMemory->size = startList->size;      
      foundMemory->next = startList->next;
      foundMemory->prev = startList->prev;
      current = getHeader(current->next);
            
      checkCorruption(current);
      int bestMemorySize = memory_size;
      
      //looks for the smallest amount of available memory
      while (current != startList){
         if ((current->size >= n+HEADER_SIZE) && (current->size <= bestMemorySize)){
            foundMemory = current;
            foundMemory->size = current->size;
            foundMemory->next = current->next;
            foundMemory->prev = current->prev;
            bestMemorySize = current->size;
         }
         current = getHeader(current->next);
         checkCorruption(current);
         }
      
      //checks if there is memory of the right size
      if ((foundMemory->size < (n + HEADER_SIZE))){
         return NULL;
      } else {
         if ((foundMemory->size/2) >= (n + HEADER_SIZE)){
            foundMemory = reduceMemory(foundMemory, n);
         }
         if (foundMemory == startList){
            free_list_ptr = getIndex(getHeader(startList->next));
         }
         
         foundMemory->magic = MAGIC_ALLOC;
                 
         //removes the foundMemory from the free list
         current = getHeader(foundMemory->prev);
         current->next = foundMemory->next;
         current = getHeader(foundMemory->next);
         current->prev = foundMemory->prev;
         
         foundMemory->next = foundMemory->prev = -1;         
      }
      return ((void *)((byte *)foundMemory + HEADER_SIZE));
   } else {
      return NULL;
   }
}

//This function will half the memory and allocate new the header region halfway in the region passed in, splitting the region into two.
free_header_t *reduceMemory (free_header_t *list, u_int32_t size){

   u_int32_t divideSize = (list->size / 2);
   
   //sets up pointers to before and after the whole undivided list
   free_header_t *middleList = (free_header_t*)((byte *)list + divideSize);
  // printf("next should be at %d", list->next);
   free_header_t *afterList = getHeader(list->next);
   
   //setting header of new region
   middleList->magic = MAGIC_FREE;
   middleList->size = divideSize;
   middleList->next = getIndex(afterList);
   middleList->prev = getIndex(list);
   
   //setting the headers around the new list
   list->size = divideSize;
   list->next = getIndex(middleList);
   afterList->prev = getIndex(middleList);
   
   checkCorruption(list);
   checkCorruption(middleList);
   checkCorruption(afterList);
   
   if ((middleList->size / 2) >= (size + HEADER_SIZE)){
      middleList = reduceMemory(middleList, size);
   } 

   return middleList;
   
}

//checks for corruption in the headers that are altered
void checkCorruption(free_header_t *current){
   if (current->magic != MAGIC_FREE){
      fprintf(stderr, "Memory corruption detected! current->magic = %d current->next = %d current->prev = %d current->size = %d\n",
               current->magic, current->next, current->prev, current->size);
      abort();
   }
}

//returns the pointer moved forward by the amount request in bytes
static free_header_t *getHeader(vaddr_t index){
   free_header_t *header = (free_header_t*)((byte *)memory + index);
   return header;
}

//returns the index of the free_header passed in
static vaddr_t getIndex(free_header_t* current){
   vaddr_t newIndex = (vaddr_t)((byte *)current - memory);
   return newIndex;
}

void sal_free(void *object)
{
   if (object != NULL) {
      //assigning header to address given minus the size of header
      free_header_t *chunk = (free_header_t *)((byte*)object - HEADER_SIZE);
      
      //checks the region is actually malloced
      if (chunk->magic != MAGIC_ALLOC) {
         fprintf(stderr, "Attempt to free non-allocated memory");
         abort();   
      }
      
      //traverse the free list starting at free_list_ptr
      //and pick the first chunk located after the one given
      free_header_t* current = getHeader(free_list_ptr);
      
      //set new chunks magic to free for use
      chunk->magic = MAGIC_FREE;
      
      while ((current < chunk) && (current->next != free_list_ptr)) {
         current = getHeader(current->next);
         checkCorruption(current);
      }
      
      //may need to be inserted at end of list, since doubly linked I can do this
      if (current < chunk){
         current = getHeader(current->next);
      }
      
      //move back to make inserting after easier
      current = getHeader(current->prev);
      
      //insert after current to keep in order
      chunk->next = current->next;
      chunk->prev = getIndex(current);
      
      //adjust pointers around it
      current->next = getIndex(chunk);
      current = getHeader(chunk->next);
      checkCorruption(current);
      current->prev = getIndex(chunk);
   
      current = getHeader(free_list_ptr);
      
      //keeps free_list_ptr at front-most area
      if (getIndex(chunk) < getIndex(current)){
         free_list_ptr = getIndex(chunk);
      }
      
      checkCorruption(current);
      
      checkMerge(chunk);
   }
}

//This function will check if a merge is possible for the area freed before
//May be called again with same merged region to recheck for me merging opportunities
void checkMerge(free_header_t* chunk){
   
   free_header_t *current = getHeader(chunk->next);
   checkCorruption(current);
   
   //will check if the next node in the free_list is next it as well as if it is of the same size
   //the first one checks if the free_list_ptr is not the next otherwise you would be wrapping around forwards
   //the second one checks if the free_list_ptr is not the chunk otherwise you would wrap around backwards
   
   if ((chunk->size == (chunk->next - getIndex(chunk))) && (chunk->size == current->size) && (chunk->next != free_list_ptr)){
      if (validMerge(getIndex(chunk), chunk->size) == TRUE){   
         merge(chunk);
      }
   } else {
      current = getHeader(chunk->prev);
      if ((current->size == (current->next - getIndex(current))) && (current->size == chunk->size) && (getIndex(chunk) != free_list_ptr)){
         if (validMerge(getIndex(current), current->size) == TRUE){   
            merge(current);
         }   
      }
   }
}

//This function will merge together two adjacent regions
void merge(free_header_t* chunk){
   
   free_header_t* afterChunk = getHeader(chunk->next);
   chunk->next = afterChunk->next;
   
   checkCorruption(chunk);
   checkCorruption(afterChunk);
   
   //removes the header of the chunk to be merged
   afterChunk->next = afterChunk->prev = afterChunk->magic = afterChunk->size = 0;
   
   //reroutes pointers and sets the new size
   afterChunk = getHeader(chunk->next);
   afterChunk->prev = getIndex(chunk);
   chunk->size = chunk->size*2;
   
   checkCorruption(chunk);
   checkCorruption(afterChunk);
   
   //calls checkMerge again as we may be able to merge with another adjacent region
   checkMerge(chunk);
}

//This function will take a look at the index of the item that will have another item merged into it
//By checking the index will still be correct for the memory size you can determine if its plausible to merge
int validMerge(vaddr_t index, vsize_t size){
   
   int valid;
   int newIndex = index;
   
   //used to check if memory of double its size will work at the current index
   vsize_t newSize = size*2;
   while (newIndex < memory_size){
      newIndex += newSize;
   }
   
   //if the index ends up at the exact memory size it passes, otherwise the merge will cause some regions to never be able to be merged
   //into one whole block again.
   if (newIndex == memory_size){
      valid = TRUE;
   } else {
      valid = FALSE;
   }
   return valid;
}

void sal_end(void)
{
   //checks the memory has actually been malloced
   if (memory != NULL){
      free(memory);
      memory = NULL;
   } else {
      fprintf(stderr, "sal_end: memory not allocated");
      abort();
   }
}

void sal_stats(void)
{
   // Optional, but useful
   printf("sal_stats\n");
    // we "use" the global variables here
    // just to keep the compiler quiet
   memory = memory;
   free_list_ptr = free_list_ptr;
   memory_size = memory_size;
}



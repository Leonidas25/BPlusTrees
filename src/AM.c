#include "AM.h"
#include "bf.h"
#include "defn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALL_OR_DIE(call)     \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK) {      \
      BF_PrintError(code);    \
      exit(code);             \
    }                         \
  }

int AM_errno = AME_OK;

AM_page_t **OpenIndex;
Scan_page_t **OpenScans;


void AM_Init() {

  OpenIndex = (AM_page_t**) malloc(BF_MAX_OPEN_FILES*sizeof(AM_page_t*));
  OpenScans = (Scan_page_t**) malloc(BF_MAX_OPEN_FILES*sizeof(Scan_page_t*));

  for(int i=0; i<BF_MAX_OPEN_FILES; i++){
    OpenIndex[i] = NULL;
    OpenScans[i] = NULL;
  }

  CALL_OR_DIE(BF_Init(MRU));

  return;
}


int AM_CreateIndex(char *fileName, 
                 char attrType1, 
                 int attrLength1, 
                 char attrType2, 
                 int attrLength2) {

  /*------------------------------------------------------------------------------------------------------------------------*/
  /* BLOCK 0 (INITIAL) FORMAT: AM File recogniser: 6 bytes -> 333333, Attribute Type 1 : sizeof(attrType1) (or attrLength1),*/
  /* Attribute Length 1: sizeof(attrLength1), Attribute Type 2 : sizeof(attrType2), Attribute Length 2 : sizeof(attrLength2)*/
  /* and finally the pointer to the root block : sizeof(int) (4 bytes)                                                      */
  /*------------------------------------------------------------------------------------------------------------------------*/

  if(attrType1=='i'||attrType1=='f'){
    if(attrLength1!=4){
      AM_errno = AME_TYPES_LENGTH;
      return AME_TYPES_LENGTH;
    }
  }

  if(attrType2=='i'||attrType2=='f'){
    if(attrLength2!=4){
      AM_errno = AME_TYPES_LENGTH;
      return AME_TYPES_LENGTH;
    }
  }

  if(attrType1=='c'){
    if(attrLength1<1 || attrLength1 > 255){
      AM_errno = AME_TYPES_LENGTH;
      return AME_TYPES_LENGTH;
    }
  }

  if(attrType2=='c'){
    if(attrLength2<1 || attrLength2 > 255){
      AM_errno = AME_TYPES_LENGTH;
      return AME_TYPES_LENGTH;
    }
  }

  int fileDesc;
  BF_Block *block;
  char* data;

  BF_Block_Init(&block);

  CALL_OR_DIE(BF_CreateFile(fileName));

  CALL_OR_DIE(BF_OpenFile(fileName, &fileDesc));

  CALL_OR_DIE(BF_AllocateBlock(fileDesc, block));

  data = BF_Block_GetData(block);
  memset(data, '3', 6);
  memcpy(data+6, &attrType1, sizeof(attrType1));
  memcpy(data+6+sizeof(attrType1), &attrLength1, sizeof(attrLength1));
  memcpy(data+6+sizeof(attrType1)+sizeof(attrLength1), &attrType2, sizeof(attrType2));
  memcpy(data+6+sizeof(attrType1)+sizeof(attrLength1)+sizeof(attrType2), &attrLength2, sizeof(attrLength2));
  memset(data+6+sizeof(attrType1)+sizeof(attrLength1)+sizeof(attrType2)+sizeof(attrLength2), 'n', 1);

  BF_Block_SetDirty(block);
  CALL_OR_DIE(BF_UnpinBlock(block));
  CALL_OR_DIE(BF_CloseFile(fileDesc));
  BF_Block_Destroy(&block);

  return AME_OK;
}


int AM_DestroyIndex(char *fileName) {

  if(remove(fileName)!=0){
    AM_errno = AME_DELETE;
    return AME_DELETE;
  }

  return AME_OK;
}


int AM_OpenIndex (char *fileName) {

  BF_Block *block;
  int *fileDesc = malloc(sizeof(int));

  BF_Block_Init(&block);

  CALL_OR_DIE(BF_OpenFile(fileName, fileDesc));
  CALL_OR_DIE(BF_GetBlock(*fileDesc, 0, block));
  char* data = BF_Block_GetData(block);

  /* If the first 6 bytes of the data of the 0 block are not 333333 then it is not an AM file */

  if(atoi(data) != 333333){
    AM_errno = AME_FILE;
    return AME_FILE;
  }


  AM_page_t *temp = (AM_page_t*)malloc(sizeof(AM_page_t));

  temp->fileDesc = *fileDesc;
  memcpy(&temp->attrType1, data+6, sizeof(temp->attrType1)); 
  memcpy(&temp->attrLength1, data+6+sizeof(temp->attrType1), sizeof(temp->attrLength1)); 
  memcpy(&temp->attrType2, data+6+sizeof(temp->attrType1)+sizeof(temp->attrLength1), sizeof(temp->attrType2)); 
  memcpy(&temp->attrLength2, data+6+sizeof(temp->attrType1)+sizeof(temp->attrLength1)+sizeof(temp->attrType2), sizeof(temp->attrLength2)); 


  CALL_OR_DIE(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  int j=0;
  while(OpenIndex[j]!=NULL && j<BF_MAX_OPEN_FILES){
    j++;
  }

  if(j==BF_MAX_OPEN_FILES){
    AM_errno = AME_OPEN_FULL;
    return AME_OPEN_FULL;
  }
  else{
    OpenIndex[j] = temp;
  }

  return j;
}


int AM_CloseIndex (int fileDesc) {

  if(OpenIndex[fileDesc]==NULL){
    AM_errno = AME_NO_OPENFILE;
    return AME_NO_OPENFILE;
  }

  CALL_OR_DIE(BF_CloseFile(OpenIndex[fileDesc]->fileDesc));

  OpenIndex[fileDesc] = NULL;

  return AME_OK;
}


int AM_InsertEntry(int fileDesc, void *value1, void *value2) {


  /*------------------------------------------------------------------------------------------------------------------------*/
  /*------------------------------------------------INDEX BLOCKS STRUCTURE--------------------------------------------------*/
  /* | 1 CHAR = 'i' | INT = PARENT BLOCK ID | INT = NUMBER OF RECORDS (NR) | INT = 1st pointer | ATTRIBUTE TYPE 1 = 1st key */
  /* | INT = 2nd pointer | ATTRIBUTE TYPE 1 = 2nd key | .... | ATTRIBUTE TYPE 1 = NR key | INT = NR+1 pointer (LAST)        */
  /*------------------------------------------------------------------------------------------------------------------------*/

  /*-------------------------------------------------------------------------------------------------------------------------------*/
  /*---------------------------------------------DATA  BLOCKS STRUCTURE------------------------------------------------------------*/
  /* | 1 CHAR = 'd' | INT = PARENT BLOCK ID | INT = NEXT DATA BLOCK ID | INT = NUMBER OF RECORDS (NR) | ATTRIBUTE TYPE 1 = 1st key */
  /* | ATTRIBYTE TYPE 2 = 1st value | ATTRIBUTE TYPE 1 = 2nd key | .... | ATTRIBUTE TYPE 1 = NR key | ATTRIBUTE TYPE 2 = NR value  */
  /*-------------------------------------------------------------------------------------------------------------------------------*/


  int fd = OpenIndex[fileDesc]->fileDesc;
  BF_Block *block, *root_block, *block1, *block2, *parent, *block3, *temp_block;
  char* data, *data1, *data2, *rdata, *next, *temp_string, *key_string, *pdata, *root_data, *data3, *temp_temp_string;
  int root, block_size, block1ID, block2ID, j, i, selected_block, temp_int, key_int, total_size, block1_size, block2_size, block_num, parentID, parent_size, counter, new_parent_id;
  float temp_float, key_float;
  char* current;
  void *temp1, *temp2, *temp3;

  BF_Block_Init(&block);
  BF_Block_Init(&root_block);
  BF_Block_Init(&block1);
  BF_Block_Init(&block2);
  BF_Block_Init(&block3);
  BF_Block_Init(&parent);
  BF_Block_Init(&temp_block);

  AM_page_t *cur = OpenIndex[fileDesc];

  temp_string = (char*)malloc(cur->attrLength1);
  key_string = (char*)malloc(cur->attrLength1);

  temp1 = malloc(cur->attrLength1);
  temp3 = malloc(cur->attrLength1);
  if(cur->attrLength2<4){
    temp2 = malloc(4);
  }
  else{
    temp2 = malloc(cur->attrLength2);
  }


  CALL_OR_DIE(BF_GetBlock(fd, 0, block));
  data = BF_Block_GetData(block);

  /* If there is no root_block, then allocate one. This can only happen after the insertion of the first data pair. */

  if(*(data+6+sizeof(cur->attrType1)+sizeof(cur->attrLength1)+sizeof(cur->attrType2)+sizeof(cur->attrLength2))=='n'){
    memset(data+6+sizeof(cur->attrType1)+sizeof(cur->attrLength1)+sizeof(cur->attrType2)+sizeof(cur->attrLength2), 1, sizeof(int)); //The root is now the block with id number = 1
    CALL_OR_DIE(BF_AllocateBlock(fd, root_block));      // Allocate the next block(with id = 1 since we have only 1 block - with id=0)
    rdata = BF_Block_GetData(root_block);
    memset(rdata, 'i', sizeof(char));                                               // index block
    memset(rdata+sizeof(char), 0, sizeof(int));                                     // parent block (0 since root)
    memset(rdata+sizeof(char)+sizeof(int), 0, sizeof(int));                         // number of keys
                                                        // right pointer block
    CALL_OR_DIE(BF_AllocateBlock(fd, block1));

    data1 = BF_Block_GetData(block1);
    memset(data1, 'd', sizeof(char));                                               // data block
    memset(data1+sizeof(char), 1, sizeof(int));                                     // parent block
    memset(data1+sizeof(char)+sizeof(int), -1, sizeof(int));                         // following block number
    memset(data1+sizeof(char)+sizeof(int)+sizeof(int), 0, sizeof(int));             // number of records


    memset(rdata+sizeof(char)+sizeof(int)+sizeof(int), 2, sizeof(int));                                    // left pointer pointing to block no 2


    BF_Block_SetDirty(block1);
    //BF_Block_SetDirty(block2);
    BF_Block_SetDirty(root_block);
    CALL_OR_DIE(BF_UnpinBlock(block1));
    //CALL_OR_DIE(BF_UnpinBlock(block2));
    CALL_OR_DIE(BF_UnpinBlock(root_block));
  }

  /* We have at least a root for sure. Now search for the appropriate data block */


  root = *(data+6+sizeof(cur->attrType1)+sizeof(cur->attrLength1)+sizeof(cur->attrType2)+sizeof(cur->attrLength2));
  BF_GetBlock(fd, root, block1);
  data1 = BF_Block_GetData(block1);
  current = data1+sizeof(char)+sizeof(int)+sizeof(int);

  /* We need different cases depending on the Attribute Type to be able to perform comparisons */

  /* At this point we have a loop that keeps going deeper into the tree depending on the value given and the */
  /* values at the nodes of the trees until it comes across a data block                                     */

  while(*data1!='d'){

    block_size = *(data1+sizeof(char)+sizeof(int));

    switch(cur->attrType1){

      case INTEGER:
        memcpy(&temp_int, value1, cur->attrLength1);
        for(j=0; j<block_size; j++){
          memcpy(&key_int, (current+sizeof(int)), cur->attrLength1);
          if(temp_int < key_int){
            selected_block = *current;
            break;
          }
          else if(temp_int == key_int){
            selected_block = *(current+(sizeof(int))+cur->attrLength1);
            break;
          }
          else{
            current += sizeof(int)+cur->attrLength1;
          }
        }
        if(j==block_size){
          selected_block = *current;
        }
        break;

      case STRING:
        memcpy(temp_string, value1, cur->attrLength1);
        for(j=0; j<block_size; j++){
          memcpy(key_string, (current+sizeof(int)), cur->attrLength1);
          if(strcmp(temp_string, key_string)<0){
            selected_block = *(current);
            break;
          }
          else if(strcmp(temp_string, key_string)==0){
            selected_block = *(current+sizeof(int)+cur->attrLength1);
            break;
          }
          else{
            current += sizeof(int)+cur->attrLength1;
          }
        }
        if(j==block_size){ 
          selected_block = *current;
        }
        break;

      case FLOAT:
        memcpy(&temp_float, value1, cur->attrLength1);
        for(j=0; j<block_size; j++){
          memcpy(&key_float, current+sizeof(int), cur->attrLength1);
          if(temp_float < key_float){
            selected_block = *current;
            break;
          }
          else if(temp_float == key_float){
            selected_block = *(current+(sizeof(int))+cur->attrLength1);
            break;
          }
          else{
            current += sizeof(int)+cur->attrLength1;
          }
        }
        if(j==block_size){
          selected_block = *current;
        }
        break;

    }

    CALL_OR_DIE(BF_GetBlockCounter(fd, &block_num));
    BF_Block_SetDirty(block1);
    CALL_OR_DIE(BF_UnpinBlock(block1));
    CALL_OR_DIE(BF_GetBlock(fd, selected_block, block1));
    data1 = BF_Block_GetData(block1);
    current = data1+sizeof(char)+sizeof(int)+sizeof(int);

  }

  /* The selected_block is the appropriate one. We need to place the data into the 'selected_block' block */

  block_size = *(data1+sizeof(char)+sizeof(int)+sizeof(int));         // 'd'+pid+nid and then the number of records

  total_size = sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block_size*(cur->attrLength1+cur->attrLength2));  //total size of the block so far

  if(BF_BLOCK_SIZE - cur->attrLength1 - cur->attrLength2 >= total_size){  // There is enough space in this specific data block

    memset(data1+sizeof(char)+sizeof(int)+sizeof(int), ++block_size, sizeof(int));

    memcpy(data1+total_size, value1, cur->attrLength1);
    memcpy(data1+total_size+cur->attrLength1, value2, cur->attrLength2);


    /* We have simply inserted the pair at the right block */
    /* Now we need to sort the block */
    /* Every time an insertion of any kind is done inside to tree, then this specific block need to be sorted */

    current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);

    switch(cur->attrType1){

      case INTEGER:

        current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
        for(i=0; i<block_size; i++){

          for(j=0; j<(block_size-1); j++){

            temp_int = *current;
            key_int = *(current+cur->attrLength1+cur->attrLength2);


            if(temp_int > key_int){

              memcpy(temp1, current, cur->attrLength1);
              memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

              memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
              memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

              memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
              memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);
            }

            current += cur->attrLength1 + cur->attrLength2;

          }

        current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
        }

        break;

      case STRING:

        current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
        for(i=0; i<block_size; i++){

          for(j=0; j<(block_size-1); j++){

            memcpy(temp_string, current, cur->attrLength1);
            memcpy(key_string, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

            if(strcmp(temp_string, key_string)>0){

              memcpy(temp1, current, cur->attrLength1);
              memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

              memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
              memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

              memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
              memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

            }

            current += cur->attrLength1 + cur->attrLength2;

          }

        current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
        }
        break;

      case FLOAT:
        for(i=0; i<block_size; i++){

          for(j=0; j<(block_size-1); j++){

            memcpy(&temp_float, current, cur->attrLength1);
            memcpy(&key_float, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

            if(temp_float > key_float){

              memcpy(temp1, current, cur->attrLength1);
              memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

              memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
              memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

              memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
              memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

            }

            current += cur->attrLength1 + cur->attrLength2;

          }

        current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
        }
        break;
    }

    /* And we're done. The record was successfully inserted into the desired block and then it was sorted from smallest to bigest */
  }
  else{

    /*------------------------------------------------------------------------------------------------------------------------*/
    /*-------The selected block is full and cannot receive another pair. So we need to split it to 2 different blocks---------*/
    /*------------------------------------------------------------------------------------------------------------------------*/

    CALL_OR_DIE(BF_AllocateBlock(fd, block2));
    data2 = BF_Block_GetData(block2);
    CALL_OR_DIE(BF_GetBlockCounter(fd, &block_num));
    block2ID = block_num-1;

    memset(data2, 'd', sizeof(char));
    memcpy(data2+sizeof(char), data1+sizeof(char), sizeof(int));
    memcpy(data2+sizeof(char)+sizeof(int), data1+sizeof(char)+sizeof(int), sizeof(int));
    memset(data1+sizeof(char)+sizeof(int), block2ID, sizeof(int));

    block1_size = block_size/2;
    block2_size = (block_size+1)/2;

    memset(data1+sizeof(char)+sizeof(int)+sizeof(int), ++block1_size, sizeof(int));
    memset(data2+sizeof(char)+sizeof(int)+sizeof(int), block2_size, sizeof(int));

    /* At this point we need to figure in which block of the two we need to put the new value. After we place it, we also need to sort this block */

    /* --------------------------------------------------------------------------------------*/
    /* ----------------------New value placement and block sorting---------------------------*/
    /* --------------------------------------------------------------------------------------*/

    switch(cur->attrType1){

      case INTEGER:
        memcpy(&temp_int, data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), sizeof(int));
        memcpy(&key_int, value1, sizeof(int));

        if(temp_int > key_int){     // Need to put the new value in the first block

          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), block2_size*(cur->attrLength1+cur->attrLength2));
          memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), value1, cur->attrLength1);
          memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2)+cur->attrLength1, value2, cur->attrLength2);

          current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the first block

          for(i=0; i<block1_size; i++){

            for(j=0; j<(block1_size-1); j++){

              temp_int = *current;
              key_int = *(current+cur->attrLength1+cur->attrLength2);

              if(temp_int > key_int){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

              }

              current += cur->attrLength1 + cur->attrLength2;

            }

          current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }

        }
        else{     // Need to put the new value in the second block

          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size)*(cur->attrLength1+cur->attrLength2), (block2_size-1)*(cur->attrLength1+cur->attrLength2));
          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), value1, cur->attrLength1);
          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2)+cur->attrLength1, value2, cur->attrLength2);

          current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the second block

          for(i=0; i<block2_size; i++){

            for(j=0; j<(block2_size-1); j++){

              temp_int = *current;
              key_int = *(current+cur->attrLength1+cur->attrLength2);

              if(temp_int > key_int){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

              }
              temp_int = *current;
              key_int = *(current+cur->attrLength1+cur->attrLength2);

              current += cur->attrLength1 + cur->attrLength2;

            }

            current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   
          }

        }

        break;

      case STRING:
        memcpy(temp_string, data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), cur->attrLength1);
        memcpy(key_string, value1, cur->attrLength1);

        if(strcmp(temp_string, key_string)>0){     // Need to put the new value in the first block

          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), block2_size*(cur->attrLength1+cur->attrLength2));
          memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), value1, cur->attrLength1);
          memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2)+cur->attrLength1, value2, cur->attrLength2);

          current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the first block

          for(i=0; i<block1_size; i++){

            for(j=0; j<(block1_size-1); j++){

              memcpy(temp_string, current, cur->attrLength1);
              memcpy(key_string, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

              if(strcmp(temp_string, key_string)>0){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

              }

              current += cur->attrLength1 + cur->attrLength2;

            }

          current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }

        }
        else{     // Need to put the new value in the second block

          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size)*(cur->attrLength1+cur->attrLength2), (block2_size-1)*(cur->attrLength1+cur->attrLength2));
          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), value1, cur->attrLength1);
          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2)+cur->attrLength1, value2, cur->attrLength2);

          current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the second block

          for(i=0; i<block2_size; i++){

            for(j=0; j<(block2_size-1); j++){

              memcpy(temp_string, current, cur->attrLength1);
              memcpy(key_string, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

              if(strcmp(temp_string, key_string)>0){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

              }

              current += cur->attrLength1 + cur->attrLength2;

            }

          current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }

        }
        break;

      case FLOAT:
        memcpy(&temp_float, data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), cur->attrLength1);
        memcpy(&key_float, value1, cur->attrLength1);

        if(temp_float > key_float){     // Need to put the new value in the first block

          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), block2_size*(cur->attrLength1+cur->attrLength2));
          memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), value1, cur->attrLength1);
          memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2)+cur->attrLength1, value2, cur->attrLength2);

          current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the first block

          for(i=0; i<block1_size; i++){

            for(j=0; j<(block1_size-1); j++){

              memcpy(&temp_float, current, cur->attrLength1);
              memcpy(&key_float, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

              if(temp_float > key_float){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

              }

              current += cur->attrLength1 + cur->attrLength2;

            }

          current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }

        }
        else{     // Need to put the new value in the second block

          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size)*(cur->attrLength1+cur->attrLength2), (block2_size-1)*(cur->attrLength1+cur->attrLength2));
          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2), value1, cur->attrLength1);
          memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+cur->attrLength2)+cur->attrLength1, value2, cur->attrLength2);

          current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the second block

          for(i=0; i<block2_size; i++){

            for(j=0; j<(block2_size-1); j++){

              memcpy(&temp_float, current, cur->attrLength1);
              memcpy(&key_float, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

              if(temp_float > key_float){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

              }

              current += cur->attrLength1 + cur->attrLength2;

            }

          current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }

        }
        break;

    }

/* --------------------------------------------------------------------------------------*/
/* ------------------------New value placed and block sorted-----------------------------*/
/* --------------------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------------------*/
/*--------Need to place the first key value of the second block in the parent block------*/
/*---------------------------------------------------------------------------------------*/

    parentID = *(data1+sizeof(char));

    CALL_OR_DIE(BF_GetBlock(fd, parentID, parent));
    pdata = BF_Block_GetData(parent);
    block_size = *(pdata+sizeof(char)+sizeof(int));
    block1_size = block_size/2;
    block2_size = (block_size+1)/2;
    block1ID = selected_block;


    memcpy(temp1, data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), cur->attrLength1);      // The value to be placed on the higher level
    memcpy(&temp_int, data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), cur->attrLength1);      // The value to be placed on the higher level


    while(BF_MAX_OPEN_FILES - (block_size*(sizeof(int)+cur->attrLength1)+sizeof(int)) < cur->attrLength1+sizeof(int)){    // There is no room at the parent block

      /*-----------------------------------------------------------------------------------------------------------------*/
      /*-----Since there is no room, we need to keep spliting the parent blocks until one with enough space is found-----*/
      /*-----------------------------------------------------------------------------------------------------------------*/

      CALL_OR_DIE(BF_AllocateBlock(fd, block3));
      data3 = BF_Block_GetData(block3);
      CALL_OR_DIE(BF_GetBlockCounter(fd, &block_num));

      memset(data3, 'i', sizeof(char));
      memcpy(data3+sizeof(char), pdata+sizeof(char), sizeof(int));
      memset(data3+sizeof(char)+sizeof(int), block2_size, sizeof(int));
      memset(pdata+sizeof(char)+sizeof(int), block1_size, sizeof(int));

      /*------------------------------------------------------------------------------------------------------------*/
      /*---------------------Need to see in which of the two blocks we need to put the new value--------------------*/
      /*------------------------------------------------------------------------------------------------------------*/

      counter=0;              
      current = pdata+sizeof(char)+sizeof(int)+sizeof(int);     
      temp_int = *current;


      while(temp_int!=block1ID){

        current += cur->attrLength1+sizeof(int);
        temp_int = *current;
        counter++; 

      }

      if(counter<block1_size){     // Need to place it in the first block

        memcpy(data3+sizeof(char)+sizeof(int)+sizeof(int), pdata+sizeof(char)+sizeof(int)+sizeof(int)+((block1_size)*(cur->attrLength1+sizeof(int))), block2_size*(cur->attrLength1+sizeof(int))+sizeof(int));
        memcpy(temp3, pdata+sizeof(char)+sizeof(int)+sizeof(int)+((block1_size-1)*(cur->attrLength1+sizeof(int)))+sizeof(int), cur->attrLength1);
        memcpy(pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+sizeof(int)), temp1, cur->attrLength1);
        memset(pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block1_size-1)*(cur->attrLength1+sizeof(int))+cur->attrLength1, block2ID, sizeof(int));

        current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the first block

        switch(cur->attrType1){

          case INTEGER:

            for(i=0; i<block1_size; i++){

              for(j=0; j<(block1_size-1); j++){

                temp_int = *current;
                key_int = *(current+cur->attrLength1+sizeof(int));

                if(temp_int > key_int){

                  memcpy(temp1, current, cur->attrLength1);
                  memcpy(temp2, current+cur->attrLength1, sizeof(int));

                  memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                  memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                  memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                  memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

                }

                current += cur->attrLength1 + sizeof(int);

              }

            current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
            }
            break;

          case STRING:

            for(i=0; i<block1_size; i++){

              for(j=0; j<(block1_size-1); j++){

                memcpy(temp_string, current, cur->attrLength1);
                memcpy(key_string, current+cur->attrLength1+sizeof(int), cur->attrLength1);

                if(strcmp(temp_string, key_string)>0){

                  memcpy(temp1, current, cur->attrLength1);
                  memcpy(temp2, current+cur->attrLength1, sizeof(int));

                  memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                  memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                  memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                  memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

                }

                current += cur->attrLength1 + sizeof(int);

              }

            current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
            }
            break;

          case FLOAT:

            for(i=0; i<block1_size; i++){

              for(j=0; j<(block1_size-1); j++){

                memcpy(&temp_float, current, sizeof(float));
                memcpy(&key_float, current+cur->attrLength1+sizeof(int), cur->attrLength1);

                if(temp_float > key_float){

                  memcpy(temp1, current, cur->attrLength1);
                  memcpy(temp2, current+cur->attrLength1, sizeof(int));

                  memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                  memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                  memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                  memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

                }

                current += cur->attrLength1 + sizeof(int);

              }

            current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
            }
            break;
        }


        current = data3+sizeof(char)+sizeof(int)+sizeof(int);

        for(i=0;i<=block2_size;i++){

          temp_int = *current;
          CALL_OR_DIE(BF_GetBlock(fd, temp_int, temp_block));
          temp_temp_string = BF_Block_GetData(temp_block);
          memset(temp_temp_string+sizeof(char), block2ID, sizeof(int));
          BF_Block_SetDirty(temp_block);
          CALL_OR_DIE(BF_UnpinBlock(temp_block));
          current += cur->attrLength1+sizeof(int);

        }


      }
      else{       // Need to place it in the second block

        memcpy(data3+sizeof(char)+sizeof(int)+sizeof(int), pdata+sizeof(char)+sizeof(int)+sizeof(int)+((block1_size+1)*(cur->attrLength1+sizeof(int)))+cur->attrLength1, (block2_size-1)*(cur->attrLength1+sizeof(int))+sizeof(int));
        memcpy(temp3, pdata+sizeof(char)+sizeof(int)+sizeof(int)+((block1_size)*(cur->attrLength1+sizeof(int))), cur->attrLength1);
        memcpy(data3+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block2_size-1)*(cur->attrLength1+sizeof(int)), temp1, cur->attrLength1);
        memset(data3+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block2_size-1)*(cur->attrLength1+sizeof(int))+cur->attrLength1, block2ID, sizeof(int));

        current = data3+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the second block

        switch(cur->attrType1){

          case INTEGER:

            for(i=0; i<block2_size; i++){

              for(j=0; j<(block2_size-1); j++){

                temp_int = *current;
                key_int = *(current+cur->attrLength1+sizeof(int));

                if(temp_int > key_int){

                  memcpy(temp1, current, cur->attrLength1);
                  memcpy(temp2, current+cur->attrLength1, sizeof(int));

                  memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                  memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                  memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                  memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

                }

                current += cur->attrLength1 + sizeof(int);

              }

            current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
            }
            break;

          case STRING:

            for(i=0; i<block2_size; i++){

              for(j=0; j<(block2_size-1); j++){

                memcpy(temp_string, current, cur->attrLength1);
                memcpy(key_string, current+cur->attrLength1+sizeof(int), cur->attrLength1);

                if(strcmp(temp_string, key_string)>0){

                  memcpy(temp1, current, cur->attrLength1);
                  memcpy(temp2, current+cur->attrLength1, sizeof(int));

                  memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                  memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                  memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                  memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

                }

                current += cur->attrLength1 + sizeof(int);

              }

            current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
            }
            break;

          case FLOAT:

            for(i=0; i<block2_size; i++){

              for(j=0; j<(block2_size-1); j++){

                memcpy(&temp_float, current, sizeof(float));
                memcpy(&key_float, current+cur->attrLength1+sizeof(int), cur->attrLength1);

                if(temp_float > key_float){

                  memcpy(temp1, current, cur->attrLength1);
                  memcpy(temp2, current+cur->attrLength1, sizeof(int));

                  memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                  memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                  memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                  memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

                }

                current += cur->attrLength1 + sizeof(int);

              }

            current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
            }
            break;
        }

        current = data3+sizeof(char)+sizeof(int)+sizeof(int);

      }

      current = data3+sizeof(char)+sizeof(int)+sizeof(int);

      for(i=0;i<=block2_size;i++){

        temp_int = *current;
        CALL_OR_DIE(BF_GetBlock(fd, temp_int, temp_block));
        temp_temp_string = BF_Block_GetData(temp_block);
        memset(temp_temp_string+sizeof(char), (block_num-1), sizeof(int));
        BF_Block_SetDirty(temp_block);
        CALL_OR_DIE(BF_UnpinBlock(temp_block));
        current += cur->attrLength1+sizeof(int);

      }

      temp1 = temp3;
      new_parent_id = parentID;
      parentID = *(pdata+sizeof(char));
      CALL_OR_DIE(BF_GetBlockCounter(fd, &block2ID));
      block2ID--;
      BF_Block_SetDirty(parent);
      CALL_OR_DIE(BF_UnpinBlock(parent));
      BF_Block_SetDirty(block3);
      CALL_OR_DIE(BF_UnpinBlock(block3));
      CALL_OR_DIE(BF_GetBlock(fd, parentID, parent));
      pdata = BF_Block_GetData(parent);
      block_size = *(pdata+sizeof(char)+sizeof(int));
      block1_size = block_size/2;
      block2_size = (block_size+1)/2;

      if(atoi(pdata) == 333333){
        /* The parent block is block 0 and therefore we currently are at the root block and we need to split it */
        break;
      }

    }

    if(atoi(pdata)==333333){        // We have reached the root, need to create new root

      CALL_OR_DIE(BF_AllocateBlock(fd, block3));
      data3 = BF_Block_GetData(block3);
      memset(data3, 'i', sizeof(char));
      memset(data3+sizeof(char), 0, sizeof(int));
      memset(data3+sizeof(char)+sizeof(int), 1, sizeof(int));
      memset(data3+sizeof(char)+sizeof(int)+sizeof(int), new_parent_id, sizeof(int));
      memcpy(data3+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), temp1, cur->attrLength1);
      memset(data3+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+cur->attrLength1, block2ID, sizeof(int));
      BF_Block_SetDirty(block3);
      CALL_OR_DIE(BF_UnpinBlock(block3));

      CALL_OR_DIE(BF_GetBlockCounter(fd, &block_num));
      block_num--;

      CALL_OR_DIE(BF_GetBlock(fd, block1ID, temp_block));       // Change parents of two children
      temp_temp_string = BF_Block_GetData(temp_block);
      memset(temp_temp_string+sizeof(char), block_num, sizeof(int));
      BF_Block_SetDirty(temp_block);
      CALL_OR_DIE(BF_UnpinBlock(temp_block));
      
      CALL_OR_DIE(BF_GetBlock(fd, block2ID, temp_block));
      temp_temp_string = BF_Block_GetData(temp_block);
      memset(temp_temp_string+sizeof(char), block_num, sizeof(int));
      BF_Block_SetDirty(temp_block);
      CALL_OR_DIE(BF_UnpinBlock(temp_block));

      memset(data+6+sizeof(cur->attrType1)+sizeof(cur->attrLength1)+sizeof(cur->attrType2)+sizeof(cur->attrLength2), block_num, sizeof(int));       // Change the root in the initial block 0


    }

    else{

      /*-----------------------------------------------------------------------------------*/
      /*---We have found enough space in the parent block and can therefore place it in----*/
      /*-----------------------------------------------------------------------------------*/

      block_size = *(pdata+sizeof(char)+sizeof(int));

      block_size++;

      memcpy(pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block_size-1)*(cur->attrLength1+sizeof(int)), temp1, cur->attrLength1);
      memset(pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(block_size-1)*(cur->attrLength1+sizeof(int))+cur->attrLength1, block2ID, sizeof(int));

      current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);   // Sort the first block

      switch(cur->attrType1){

        case INTEGER:

          for(i=0; i<block_size; i++){

            for(j=0; j<(block_size-1); j++){

              temp_int = *current;
              key_int = *(current+cur->attrLength1+sizeof(int));

              if(temp_int > key_int){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, sizeof(int));

                memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

              }

              current += cur->attrLength1 + sizeof(int);

            }

          current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }
          break;

        case STRING:

          for(i=0; i<block_size; i++){

            for(j=0; j<(block_size-1); j++){

              memcpy(temp_string, current, cur->attrLength1);
              memcpy(key_string, current+cur->attrLength1+sizeof(int), cur->attrLength1);

              if(strcmp(temp_string, key_string)>0){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, sizeof(int));

                memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

              }

              current += cur->attrLength1 + sizeof(int);

            }

          current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }
          break;

        case FLOAT:

          for(i=0; i<block_size; i++){

            for(j=0; j<(block_size-1); j++){

              memcpy(&temp_float, current, sizeof(float));
              memcpy(&key_float, current+cur->attrLength1+sizeof(int), cur->attrLength1);

              if(temp_float > key_float){

                memcpy(temp1, current, cur->attrLength1);
                memcpy(temp2, current+cur->attrLength1, sizeof(int));

                memcpy(current, current+cur->attrLength1+sizeof(int), cur->attrLength1);
                memcpy(current+cur->attrLength1, current+cur->attrLength1+sizeof(int)+cur->attrLength1, sizeof(int));

                memcpy(current+cur->attrLength1+sizeof(int), temp1, cur->attrLength1);
                memcpy(current+cur->attrLength1+sizeof(int)+cur->attrLength1, temp2, sizeof(int));

              }

              current += cur->attrLength1 + sizeof(int);

            }

          current = pdata+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
          }
          break;
      }


      memset(pdata+sizeof(char)+sizeof(int), block_size, sizeof(int));

      /*--------------------------------------------------------------------------------------------------------*/
      /*---------------------------------------Duplicates Handling----------------------------------------------*/
      /*--------------------------------------------------------------------------------------------------------*/
      
      int size1 = *(data1 + sizeof(char)+ sizeof(int) + sizeof(int));
      int size2 = *(data2 + sizeof(char) +sizeof(int) + sizeof(int));
      char* to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size1-1)*(cur->attrLength1 + cur->attrLength2);
      char* to_compare2 = data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int);
      int duplicates = 1;
      int pid;

      switch(cur->attrType1) {
        case INTEGER:
          temp_int = *to_compare1;
          key_int = *to_compare2;

          while (duplicates) {
            temp_int = *to_compare1;
            key_int = *to_compare2;
            if (temp_int == key_int) {
              to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int);
              temp_int = *to_compare1;

              if (temp_int == key_int) {
                memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(size1)*(cur->attrLength1 + cur->attrLength2), to_compare2,cur->attrLength1 + cur->attrLength2);

                memcpy(data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int), data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size2-1)*(cur->attrLength1 + cur->attrLength2), cur->attrLength1 + cur->attrLength2);
                
                size1++;
                size2--;
                memset(data1+sizeof(char)+sizeof(int)+sizeof(int), size1, sizeof(int));
                memset(data2+sizeof(char)+sizeof(int)+sizeof(int), size2, sizeof(int));
              }
              else {
                memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(size2)*(cur->attrLength1 + cur->attrLength2), to_compare1+(size1-1)*(cur->attrLength1 + cur->attrLength2), cur->attrLength1 + cur->attrLength2);


                size1--;
                size2++;
                memset(data1+sizeof(char)+sizeof(int)+sizeof(int), size1, sizeof(int));
                memset(data2+sizeof(char)+sizeof(int)+sizeof(int), size2, sizeof(int));
              }

              current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
              for(i=0; i<size2; i++){

                for(j=0; j<(size2-1); j++){

                  temp_int = *current;
                  key_int = *(current+cur->attrLength1+cur->attrLength2);


                  if(temp_int > key_int){

                    memcpy(temp1, current, cur->attrLength1);
                    memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                    memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                    memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                    memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                    memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);
                  }

                  current += cur->attrLength1 + cur->attrLength2;

                }

              current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
              }

              to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size1-1)*(cur->attrLength1 + cur->attrLength2);
            }
            else
              duplicates = 0;
          }

          pid = *(data2 + sizeof(char));
          CALL_OR_DIE(BF_GetBlock(fd, pid, temp_block));

          current = BF_Block_GetData(temp_block);
          current += sizeof(char) + sizeof(int) + sizeof(int);

          while (*current == block2ID) {

            pid = *(current-sizeof(int)-sizeof(int));
            CALL_OR_DIE(BF_UnpinBlock(temp_block));

            CALL_OR_DIE(BF_GetBlock(fd, pid, temp_block));
            current = BF_Block_GetData(temp_block);
            current += sizeof(char) + sizeof(int) + sizeof(int);
          }

          while (*current != block2ID)
            current += sizeof(int) + (cur->attrLength1);

          current -= cur->attrLength1;
          memcpy(current, data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), cur->attrLength1);
          CALL_OR_DIE(BF_UnpinBlock(temp_block));

          break;

        case FLOAT:
          memcpy(&temp_float, to_compare1, sizeof(float));
          memcpy(&key_float, to_compare2, sizeof(float));

          while (duplicates) {
            memcpy(&temp_float, to_compare1, sizeof(float));
            memcpy(&key_float, to_compare2, sizeof(float));

            if (temp_float == key_float) {
              to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int);
              temp_int = *to_compare1;

              if (temp_float == key_float) {          
                memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(size1)*(cur->attrLength1 + cur->attrLength2), to_compare2,cur->attrLength1 + cur->attrLength2);

                memcpy(data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int), data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size2-1)*(cur->attrLength1 + cur->attrLength2), cur->attrLength1 + cur->attrLength2);
                
                size1++;
                size2--;
                memset(data1+sizeof(char)+sizeof(int)+sizeof(int), size1, sizeof(int));
                memset(data2+sizeof(char)+sizeof(int)+sizeof(int), size2, sizeof(int));
              }
              else {
                memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(size2)*(cur->attrLength1 + cur->attrLength2), to_compare1+(size1-1)*(cur->attrLength1 + cur->attrLength2), cur->attrLength1 + cur->attrLength2);

                size1--;
                size2++;
                memset(data1+sizeof(char)+sizeof(int)+sizeof(int), size1, sizeof(int));
                memset(data2+sizeof(char)+sizeof(int)+sizeof(int), size2, sizeof(int));
              }

              current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
              for(i=0; i<size2; i++){

                for(j=0; j<(size2-1); j++){

                  memcpy(&temp_float, current, cur->attrLength1);
                  memcpy(&key_float, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

                  if(temp_float > key_float){

                    memcpy(temp1, current, cur->attrLength1);
                    memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                    memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                    memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                    memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                    memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

                  }

                  current += cur->attrLength1 + cur->attrLength2;

                }

              to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size1-1)*(cur->attrLength1 + cur->attrLength2);
              }
            }
            else {
              duplicates = 0;
            }
          }

          pid = *(data2 + sizeof(char));
          CALL_OR_DIE(BF_GetBlock(fd, pid, temp_block));

          current = BF_Block_GetData(temp_block);
          current += sizeof(char) + sizeof(int) + sizeof(int);

          while (*current == block2ID) {

            pid = *(current-sizeof(int)-sizeof(int));
            CALL_OR_DIE(BF_UnpinBlock(temp_block));

            CALL_OR_DIE(BF_GetBlock(fd, pid, temp_block));
            current = BF_Block_GetData(temp_block);
            current += sizeof(char) + sizeof(int) + sizeof(int);
          }

          while (*current != block2ID)
            current += sizeof(int) + (cur->attrLength1);

          current -= cur->attrLength1;
          memcpy(current, data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), cur->attrLength1);
          CALL_OR_DIE(BF_UnpinBlock(temp_block));

          break;

        case STRING:
          memcpy(temp_string, to_compare1, cur->attrLength1);
          memcpy(key_string, to_compare2, cur->attrLength1);

          while (duplicates) {
            memcpy(temp_string, to_compare1, cur->attrLength1);
            memcpy(key_string, to_compare2, cur->attrLength1);
            if (strcmp(temp_string, key_string)==0) {
              to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int);
              temp_int = *to_compare1;

              if (strcmp(temp_string, key_string)==0) {
                memcpy(data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(size1)*(cur->attrLength1 + cur->attrLength2), to_compare2,cur->attrLength1 + cur->attrLength2);

                memcpy(data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int), data2 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size2-1)*(cur->attrLength1 + cur->attrLength2), cur->attrLength1 + cur->attrLength2);
                
                size1++;
                size2--;
                memset(data1+sizeof(char)+sizeof(int)+sizeof(int), size1, sizeof(int));
                memset(data2+sizeof(char)+sizeof(int)+sizeof(int), size2, sizeof(int));
              }
              else {
                memcpy(data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int)+(size2)*(cur->attrLength1 + cur->attrLength2), to_compare1+(size1-1)*(cur->attrLength1 + cur->attrLength2), cur->attrLength1 + cur->attrLength2);

                size1--;
                size2++;
                memset(data1+sizeof(char)+sizeof(int)+sizeof(int), size1, sizeof(int));
                memset(data2+sizeof(char)+sizeof(int)+sizeof(int), size2, sizeof(int));
              }

              current = data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
              for(i=0; i<size2; i++){

                for(j=0; j<(size2-1); j++){

                  memcpy(temp_string, current, cur->attrLength1);
                  memcpy(key_string, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);

                  if(strcmp(temp_string, key_string)>0){

                    memcpy(temp1, current, cur->attrLength1);
                    memcpy(temp2, current+cur->attrLength1, cur->attrLength2);

                    memcpy(current, current+cur->attrLength1+cur->attrLength2, cur->attrLength1);
                    memcpy(current+cur->attrLength1, current+cur->attrLength1+cur->attrLength2+cur->attrLength1, cur->attrLength2);

                    memcpy(current+cur->attrLength1+cur->attrLength2, temp1, cur->attrLength1);
                    memcpy(current+cur->attrLength1+cur->attrLength2+cur->attrLength1, temp2, cur->attrLength2);

                  }

                  current += cur->attrLength1 + cur->attrLength2;

                }

              current = data1+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int);
              }

              to_compare1 = data1 + sizeof(char)+ sizeof(int) + sizeof(int) + sizeof(int) + (size1-1)*(cur->attrLength1 + cur->attrLength2);
            }
            else 
              duplicates = 0;
          }

          pid = *(data2 + sizeof(char));
          CALL_OR_DIE(BF_GetBlock(fd, pid, temp_block));

          current = BF_Block_GetData(temp_block);
          current += sizeof(char) + sizeof(int) + sizeof(int);

          while (*current == block2ID) {

            pid = *(current-sizeof(int)-sizeof(int));
            CALL_OR_DIE(BF_UnpinBlock(temp_block));

            CALL_OR_DIE(BF_GetBlock(fd, pid, temp_block));
            current = BF_Block_GetData(temp_block);
            current += sizeof(char) + sizeof(int) + sizeof(int);
          }

          while (*current != block2ID)
            current += sizeof(int) + (cur->attrLength1);

          current -= cur->attrLength1;
          memcpy(current, data2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(int), cur->attrLength1);
          CALL_OR_DIE(BF_UnpinBlock(temp_block));

          break;
      }

/*----------------------------------------------------------------------------------------------------------------------------*/
/*-------------------------------------------------------Duplicates Handled---------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------------------------*/

    }



    BF_Block_SetDirty(parent);
    CALL_OR_DIE(BF_UnpinBlock(parent));

    BF_Block_SetDirty(block2);
    CALL_OR_DIE(BF_UnpinBlock(block2));
  }

  BF_Block_SetDirty(block1);
  CALL_OR_DIE(BF_UnpinBlock(block1));

  BF_Block_SetDirty(block);
  CALL_OR_DIE(BF_UnpinBlock(block));

  BF_Block_Destroy(&block);
  BF_Block_Destroy(&root_block);
  BF_Block_Destroy(&block1);
  BF_Block_Destroy(&block2);
  BF_Block_Destroy(&block3);
  BF_Block_Destroy(&parent);
  BF_Block_Destroy(&temp_block);


  return AME_OK;
}

/* This function returns a pointer based on the data of the OpenScans for the scanDesc */
/* THE FIRST RECORD HAS POSITION 0 IN THE BLOCK, NOT 1*/
/* SO, IF A POINTER SHOWS RIGHT AFTER THE LAST RECORD, rec_num==number_of_records_in_the_block*/
void *AM_FindNextEntry(int scanDesc)
{
  if(OpenScans[scanDesc]==NULL)
  {
    AM_errno=AME_SCAN_NOT_OPENED;
    return NULL;
  }
  if(OpenScans[scanDesc]->op==1)/* for EQUAL */
  /* if op is EQUAL, we have only 1 block to check */
  {
      char *r;
      BF_Block *block;
      BF_Block_Init(&block);

      CALL_OR_DIE( BF_GetBlock(OpenIndex[OpenScans[scanDesc]->fd]->fileDesc,OpenScans[scanDesc]->bl_num,block) );
      r=BF_Block_GetData(block);/* we get a pointer to the block we need*/
      int number= *(r+sizeof(char)+sizeof(int)*2);/*number of records in block*/
      if(number==OpenScans[scanDesc]->rec_num)/* case we reach the end of the records in the block */
      {
        CALL_OR_DIE(BF_UnpinBlock(block));
        AM_errno=AME_EOF;
        return NULL;
      }
      /* algorithm to reach the record with position rec_num */
      r+=sizeof(char)+sizeof(int)*3;
      r+= OpenScans[scanDesc]->rec_num * (OpenIndex[OpenScans[scanDesc]->fd]->attrLength1 + OpenIndex[OpenScans[scanDesc]->fd]->attrLength2);
      r+= OpenIndex[OpenScans[scanDesc]->fd]->attrLength1;
      /* if we run this function again, it has to be about the next record */
      OpenScans[scanDesc]->rec_num++;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return r;
  }
  else if(OpenScans[scanDesc]->op==2)/* for NOT_EQUAL */
  {
    char *r;
    BF_Block *block;
    BF_Block_Init(&block);

    CALL_OR_DIE( BF_GetBlock(OpenIndex[OpenScans[scanDesc]->fd]->fileDesc,OpenScans[scanDesc]->bl_num,block) );
    r=BF_Block_GetData(block); /* we get a pointer to the block we need */
    int next= *(r+sizeof(char)+sizeof(int)); /*number of the next block, it has value -1 if we have the last block */
    int number= *(r+sizeof(char)+sizeof(int)*2);/* number of records in the block */

    /* allgorithm to reach the record with position rec_num */
    r+=sizeof(char)+sizeof(int)*3;
    r+= OpenScans[scanDesc]->rec_num * (OpenIndex[OpenScans[scanDesc]->fd]->attrLength1 + OpenIndex[OpenScans[scanDesc]->fd]->attrLength2);

    if(number==OpenScans[scanDesc]->rec_num)/* case we reach the end of the block */
    {
      if(next == -1 )/* if its the last data block, leave */
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
      /* otherwise, go to the beggining of the next block */
      OpenScans[scanDesc]->bl_num=next;
      OpenScans[scanDesc]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return (void*) AM_FindNextEntry(scanDesc);
    }
    /* based on the type of the key, check if we have a record that we need */
    /* if we dont, run the same function for the next record */
    int key_int; float key_float,temp_float; char *temp_string = (char*) malloc(OpenIndex[OpenScans[scanDesc]->fd]->attrLength1); char *key_string = (char*) malloc(OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
    if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1 == INTEGER)
    {
      memcpy(&key_int, OpenScans[scanDesc]->banned, sizeof(int));
      if(key_int==*r)
      {
        OpenScans[scanDesc]->rec_num++;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return AM_FindNextEntry(scanDesc);
      }
    }
    else if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1 == FLOAT)
    {
      memcpy(&key_float, OpenScans[scanDesc]->banned, sizeof(float));
      memcpy(&temp_float, r, sizeof(float));
      if(key_float==temp_float)
      {
        OpenScans[scanDesc]->rec_num++;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return AM_FindNextEntry(scanDesc);
      }
    }
    else if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1==STRING)
    {
      memcpy(key_string, OpenScans[scanDesc]->banned, OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
      memcpy(temp_string,r, OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
      if(strcmp(key_string,temp_string)==0)
      {
        OpenScans[scanDesc]->rec_num++;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return AM_FindNextEntry(scanDesc);
      }
    }

    r+=OpenIndex[OpenScans[scanDesc]->fd]->attrLength1;
    /* if we run this function again, it has to be about the next record */
    OpenScans[scanDesc]->rec_num++;
    CALL_OR_DIE( BF_UnpinBlock(block) );
    return r;
  }
  else if(OpenScans[scanDesc]->op==3)/*for LESS_THAN */
  {
    char *r;
    BF_Block *block;
    BF_Block_Init(&block);

    CALL_OR_DIE( BF_GetBlock(OpenIndex[OpenScans[scanDesc]->fd]->fileDesc,OpenScans[scanDesc]->bl_num,block) );
    r=BF_Block_GetData(block); /* we get a pointer to the block we need */
    int next= *(r+sizeof(char)+sizeof(int));/*number of the next block, it has value -1 if we have the last block */
    int number= *(r+sizeof(char)+sizeof(int)*2);/* number of records in the block */

    /* algorithm to reach the record with position rec_num */
    r+=sizeof(char)+sizeof(int)*3;
    r+= OpenScans[scanDesc]->rec_num * (OpenIndex[OpenScans[scanDesc]->fd]->attrLength1 + OpenIndex[OpenScans[scanDesc]->fd]->attrLength2);

    if(number==OpenScans[scanDesc]->rec_num)/* case we reach the end of the block */
    {
      if(next == -1 )/* if its the last data block, leave */
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
      /* otherwise, go to the beggining of the next block */
      OpenScans[scanDesc]->bl_num=next;
      OpenScans[scanDesc]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return (void*) AM_FindNextEntry(scanDesc);
    }
    /* based on the type of the key, check if we have a record that we need */
    /* if we dont, run the same function for the next record */
    /* THE OPERATOR IS <= BEQAUSE WE CHECK NOT(>)*/
    int key_int; float key_float,temp_float; char *temp_string = (char*) malloc(OpenIndex[OpenScans[scanDesc]->fd]->attrLength1); char *key_string = (char*) malloc(OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
    if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1 == INTEGER)
    {
      memcpy(&key_int, OpenScans[scanDesc]->banned, sizeof(int));
      if(key_int<=*r)
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
    }
    else if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1 == FLOAT)
    {
      memcpy(&key_float, OpenScans[scanDesc]->banned, sizeof(float));
      memcpy(&temp_float, r, sizeof(float));
      if(key_float<=temp_float)
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
    }
    else if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1==STRING)
    {
      memcpy(key_string, OpenScans[scanDesc]->banned, OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
      memcpy(temp_string,r, OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
      if((strcmp(key_string,temp_string)==0)||(strcmp(key_string,temp_string)<0))
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
    }

    r+=OpenIndex[OpenScans[scanDesc]->fd]->attrLength1;
    /* if we run this function again, it has to be about the next record */
    OpenScans[scanDesc]->rec_num++;
    CALL_OR_DIE( BF_UnpinBlock(block) );
    return r;
  }
  else if(OpenScans[scanDesc]->op==4) /* for GREATER_THAN */
  {
    char *r;
    BF_Block *block;
    BF_Block_Init(&block);

    CALL_OR_DIE( BF_GetBlock(OpenIndex[OpenScans[scanDesc]->fd]->fileDesc,OpenScans[scanDesc]->bl_num,block) );
    r=BF_Block_GetData(block);/* we get a pointer to the block we need */
    int next= *(r+sizeof(char)+sizeof(int));/*number of the next block, it has value -1 if we have the last block */
    int number= *(r+sizeof(char)+sizeof(int)*2);/* number of records in the block */


    r+=sizeof(char)+sizeof(int)*3;
    r+= OpenScans[scanDesc]->rec_num * (OpenIndex[OpenScans[scanDesc]->fd]->attrLength1 + OpenIndex[OpenScans[scanDesc]->fd]->attrLength2);

    if(number==OpenScans[scanDesc]->rec_num)/* case we reach the end of the block */
    {
      if(next == -1 )/* if its the last data block, leave */
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
      /* otherwise, go to the beggining of the next block */
      OpenScans[scanDesc]->bl_num=next;
      OpenScans[scanDesc]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return (void*) AM_FindNextEntry(scanDesc);
    }

    r+=OpenIndex[OpenScans[scanDesc]->fd]->attrLength1;
    /* if we run this function again, it has to be about the next record */
    OpenScans[scanDesc]->rec_num++;
    CALL_OR_DIE( BF_UnpinBlock(block) );
    return r;

  }
  else if(OpenScans[scanDesc]->op==5)/* for LESS_THAN or EQUAL */
  /* the logic here is the same as op==3, with the only difference at the operator while checking(< instead of <=) */
  {
    char *r;
    BF_Block *block;
    BF_Block_Init(&block);

    CALL_OR_DIE( BF_GetBlock(OpenIndex[OpenScans[scanDesc]->fd]->fileDesc,OpenScans[scanDesc]->bl_num,block) );
    r=BF_Block_GetData(block);
    int next= *(r+sizeof(char)+sizeof(int));
    int number= *(r+sizeof(char)+sizeof(int)*2);


    r+=sizeof(char)+sizeof(int)*3;
    r+= OpenScans[scanDesc]->rec_num * (OpenIndex[OpenScans[scanDesc]->fd]->attrLength1 + OpenIndex[OpenScans[scanDesc]->fd]->attrLength2);

    if(number==OpenScans[scanDesc]->rec_num)
    {
      if(next == -1 )
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
      OpenScans[scanDesc]->bl_num=next;
      OpenScans[scanDesc]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return (void*) AM_FindNextEntry(scanDesc);
    }

    int key_int; float key_float,temp_float; char *temp_string = (char*) malloc(OpenIndex[OpenScans[scanDesc]->fd]->attrLength1); char *key_string = (char*) malloc(OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
    if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1 == INTEGER)
    {
      memcpy(&key_int, OpenScans[scanDesc]->banned, sizeof(int));
      if(key_int<*r)
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
    }
    else if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1 == FLOAT)
    {
      memcpy(&key_float, OpenScans[scanDesc]->banned, sizeof(float));
      memcpy(&temp_float, r, sizeof(float));
      if(key_float<temp_float)
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
    }
    else if(OpenIndex[OpenScans[scanDesc]->fd]->attrType1==STRING)
    {
      memcpy(key_string, OpenScans[scanDesc]->banned, OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
      memcpy(temp_string,r, OpenIndex[OpenScans[scanDesc]->fd]->attrLength1);
      if(strcmp(key_string,temp_string)<0)
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
        return NULL;
      }
    }

    r+=OpenIndex[OpenScans[scanDesc]->fd]->attrLength1;

    OpenScans[scanDesc]->rec_num++;
    CALL_OR_DIE( BF_UnpinBlock(block) );
    return r;
  }
  else if(OpenScans[scanDesc]->op==6)/*for GREATER_THAN or EQUAL */
  /* the logic here is exactly the same as op==4 */
  {
    char *r;
      BF_Block *block;
      BF_Block_Init(&block);

     CALL_OR_DIE( BF_GetBlock(OpenIndex[OpenScans[scanDesc]->fd]->fileDesc,OpenScans[scanDesc]->bl_num,block) );
      r=BF_Block_GetData(block);
      int next= *(r+sizeof(char)+sizeof(int));
      int number= *(r+sizeof(char)+sizeof(int)*2);


      r+=sizeof(char)+sizeof(int)*3;
    r+= OpenScans[scanDesc]->rec_num * (OpenIndex[OpenScans[scanDesc]->fd]->attrLength1 + OpenIndex[OpenScans[scanDesc]->fd]->attrLength2);

    if(number==OpenScans[scanDesc]->rec_num)
    {
      if(next == -1 )
      {
        AM_errno=AME_EOF;
        CALL_OR_DIE( BF_UnpinBlock(block) );
          return NULL;
      }
      OpenScans[scanDesc]->bl_num=next;
      OpenScans[scanDesc]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return (void*) AM_FindNextEntry(scanDesc);
    }

    r+=OpenIndex[OpenScans[scanDesc]->fd]->attrLength1;

    OpenScans[scanDesc]->rec_num++;
    CALL_OR_DIE( BF_UnpinBlock(block) );
    return r;
  }
}

void One(int FileDesc,void *value, int i)/* for op==1 */
{
  char *data, *data0;
  BF_Block *block0, *block;
  BF_Block_Init(&block0); BF_Block_Init(&block);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,0,block0) );
  data0=BF_Block_GetData(block0);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,*(data0+6*sizeof(char)+sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)),block) );
  data=BF_Block_GetData(block);
  CALL_OR_DIE( BF_UnpinBlock(block0) );
  /* we got the data of the root block, with the help of block number 0 */
  int key_int;
  float temp_float, key_float;
  char *temp_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1); char *key_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1);

  int number,j,current_block;
  /*we start searching for the data block we need */
  while(1)
  {
    if((*data)=='i')
    {
      number= *(data+sizeof(char)+sizeof(int));/*number of keys in the block */
      data+=sizeof(char)+sizeof(int)*2;/* we go to the first pointer of a block*/
      /* based on the type of the key, we start searching until we find the block we need */
      for(j=0;j<number;j++)
      {
        if(OpenIndex[FileDesc]->attrType1 == INTEGER)
        {
          memcpy(&key_int, value, sizeof(int));
          if((key_int) < *(data+sizeof(int)) ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
        else if(OpenIndex[FileDesc]->attrType1==FLOAT)
        {
          memcpy(&key_float, value, sizeof(float));
          memcpy(&temp_float, data+sizeof(int), sizeof(float));
          if( key_float < temp_float ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
        else if(OpenIndex[FileDesc]->attrType1==STRING)
        {
          memcpy(key_string, value, OpenIndex[FileDesc]->attrLength1);
          memcpy(temp_string,data+sizeof(int), OpenIndex[FileDesc]->attrLength1);
          if(strcmp(key_string, temp_string) < 0 ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
      }
      /* we found the block we need */
      current_block=*data;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,current_block,block) );
      data=BF_Block_GetData(block);
    }
    else
    {
      /*now, we start searching for the record of the block we need to print */
      number= *(data+sizeof(char)+sizeof(int)*2);/*number of records in the block */
      OpenScans[i]->bl_num=current_block;
      data+=sizeof(char)+sizeof(int)*3;/*we go to the first record of the block */
      /* THE FIRST RECORD HAS POSITION 0 IN THE BLOCK, NOT 1*/
      /* SO, IF A POINTER SHOWS RIGHT AFTER THE LAST RECORD, rec_num==number_of_records_in_the_block*/
      OpenScans[i]->rec_num=0;
      for(j=0;j<number;j++)
      {
        if(OpenIndex[FileDesc]->attrType1 == INTEGER)
        {
          memcpy(&key_int, value, sizeof(int));
          if(key_int == *data)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
        else if(OpenIndex[FileDesc]->attrType1 == FLOAT)
        {
          memcpy(&temp_float, value, sizeof(float));
          memcpy(&key_float, data, sizeof(float));
          if(temp_float == key_float)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
        else if(OpenIndex[FileDesc]->attrType1 == STRING)
        {
          memcpy(temp_string, value, OpenIndex[FileDesc]->attrLength1);
          memcpy(key_string, data, OpenIndex[FileDesc]->attrLength1);
          if(strcmp(temp_string, key_string) == 0)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
      }
      /*in case we dont find any record that matches, we return a pointer after the last record of the block */
      free(temp_string); free(key_string);
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return;
    }
  }
}

void Two(int FileDesc,void *value, int i)/* for op==2 */
{
  /* we get the root block, same logic as function One*/
  char *data, *data0;
  BF_Block *block0, *block;
  BF_Block_Init(&block0); BF_Block_Init(&block);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,0,block0) );
  data0=BF_Block_GetData(block0);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,*(data0+6*sizeof(char)+sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)),block) );
  data=BF_Block_GetData(block);
  CALL_OR_DIE( BF_UnpinBlock(block0) );

  int key_int;
  float temp_float, key_float;
  char *temp_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1); char *key_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1);

  OpenScans[i]->banned=value;

  int j,number,current_block;

  while(1)
  {
    /* we get the first data block(block with lowest key values*/
    if(*data=='i')
    {
      current_block= *(data+sizeof(char)+sizeof(int)*2);
      CALL_OR_DIE( BF_UnpinBlock(block) );
      CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,current_block,block) );
      data=BF_Block_GetData(block);
    }
    /* and return a pointer to the first record of the first data block */
    else
    {
      OpenScans[i]->bl_num=current_block;
      OpenScans[i]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return;
    }
  }
}
void Three(int FileDesc,void *value, int i)/* for op==3 */
{
  /* we get the root block, same logic as function One*/
  char *data, *data0;
  BF_Block *block0, *block;
  BF_Block_Init(&block0); BF_Block_Init(&block);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,0,block0) );
  data0=BF_Block_GetData(block0);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,*(data0+6*sizeof(char)+sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)),block) );
  data=BF_Block_GetData(block);
  CALL_OR_DIE( BF_UnpinBlock(block0) );

  int key_int;
  float temp_float, key_float;
  char *temp_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1); char *key_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1);

  OpenScans[i]->banned=value;

  int j,number,current_block;

  while(1)
  {
    /* we get the first data block(block with lowest key values*/
    if(*data=='i')
    {
      current_block= *(data+sizeof(char)+sizeof(int)*2);
      CALL_OR_DIE( BF_UnpinBlock(block) );
      CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,current_block,block) );
      data=BF_Block_GetData(block);
    }
    /* and return a pointer to the first record of the first data block */
    else
    {
      OpenScans[i]->bl_num=current_block;
      OpenScans[i]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return;
    }
  }
}
void Four(int FileDesc,void *value, int i)/* for op==4 */
/*exactly the same logic with function One, the only thing that changes is the operator while searching the data blocks for the corect record*/
{
  char *data, *data0;
  BF_Block *block0, *block;
  BF_Block_Init(&block0); BF_Block_Init(&block);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,0,block0) );
  data0=BF_Block_GetData(block0);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,*(data0+6*sizeof(char)+sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)),block) );
  data=BF_Block_GetData(block);
  CALL_OR_DIE( BF_UnpinBlock(block0) );

  int key_int;
  float temp_float, key_float;
  char *temp_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1); char *key_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1);

  int number,j,current_block;
  while(1)
  {
    if((*data)=='i')
    {
      number= *(data+sizeof(char)+sizeof(int));
      data+=sizeof(char)+sizeof(int)*2;
      for(j=0;j<number;j++)
      {
        if(OpenIndex[FileDesc]->attrType1 == INTEGER)
        {
          memcpy(&key_int, value, sizeof(int));
          if((key_int) < *(data+sizeof(int)) ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
        else if(OpenIndex[FileDesc]->attrType1==FLOAT)
        {
          memcpy(&key_float, value, sizeof(float));
          memcpy(&temp_float, data+sizeof(int), sizeof(float));
          if( key_float < temp_float ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
        else if(OpenIndex[FileDesc]->attrType1==STRING)
        {
          memcpy(key_string, value, OpenIndex[FileDesc]->attrLength1);
          memcpy(temp_string,data+sizeof(int), OpenIndex[FileDesc]->attrLength1);
          if(strcmp(key_string, temp_string) < 0 ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
      }
      current_block=*data;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,current_block,block) );
      data=BF_Block_GetData(block);
    }
    else
    {
      number= *(data+sizeof(char)+sizeof(int)*2);
      OpenScans[i]->bl_num=current_block;
      data+=sizeof(char)+sizeof(int)*3;
      OpenScans[i]->rec_num=0;
      for(j=0;j<number;j++)
      {
        if(OpenIndex[FileDesc]->attrType1 == INTEGER)
        {
          memcpy(&key_int, value, sizeof(int));
          if(key_int < *data)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
        else if(OpenIndex[FileDesc]->attrType1 == FLOAT)
        {
          memcpy(&temp_float, value, sizeof(float));
          memcpy(&key_float, data, sizeof(float));
          if(temp_float < key_float)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
        else if(OpenIndex[FileDesc]->attrType1 == STRING)
        {
          memcpy(temp_string, value, OpenIndex[FileDesc]->attrLength1);
          memcpy(key_string, data, OpenIndex[FileDesc]->attrLength1);
          if(strcmp(temp_string, key_string) < 0)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
      }
      free(temp_string); free(key_string);
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return;
    }
  }
}
void Five(int FileDesc,void *value, int i) /* for op==5 */
/* exactly the same logic for function three, nothing changes, the actual changes are in FindNextEntry */
{
  char *data, *data0;
  BF_Block *block0, *block;
  BF_Block_Init(&block0); BF_Block_Init(&block);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,0,block0) );
  data0=BF_Block_GetData(block0);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,*(data0+6*sizeof(char)+sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)),block) );
  data=BF_Block_GetData(block);
  CALL_OR_DIE( BF_UnpinBlock(block0) );

  int key_int;
  float temp_float, key_float;
  char *temp_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1); char *key_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1);

  OpenScans[i]->banned=value;

  int j,number,current_block;

  while(1)
  {
    if(*data=='i')
    {
      current_block= *(data+sizeof(char)+sizeof(int)*2);
      CALL_OR_DIE( BF_UnpinBlock(block) );
      CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,current_block,block) );
      data=BF_Block_GetData(block);
    }
    else
    {
      OpenScans[i]->bl_num=current_block;
      OpenScans[i]->rec_num=0;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return;
    }
  }
}
void Six(int FileDesc,void *value, int i)/* for op==6 */
/*exactly the same logic with function One, the only thing that changes is the operator while searching the data blocks for the corect record*/
{
  char *data, *data0;
  BF_Block *block0, *block;
  BF_Block_Init(&block0); BF_Block_Init(&block);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,0,block0) );
  data0=BF_Block_GetData(block0);

  CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,*(data0+6*sizeof(char)+sizeof(char)+sizeof(int)+sizeof(char)+sizeof(int)),block) );
  data=BF_Block_GetData(block);
  CALL_OR_DIE( BF_UnpinBlock(block0) );

  int key_int;
  float temp_float, key_float;
  char *temp_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1); char *key_string = (char*) malloc(OpenIndex[FileDesc]->attrLength1);

  int number,j,current_block;
  while(1)
  {
    if((*data)=='i')
    {
      number= *(data+sizeof(char)+sizeof(int));
      data+=sizeof(char)+sizeof(int)*2;
      for(j=0;j<number;j++)
      {
        if(OpenIndex[FileDesc]->attrType1 == INTEGER)
        {
          memcpy(&key_int, value, sizeof(int));
          if((key_int) < *(data+sizeof(int)) ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
        else if(OpenIndex[FileDesc]->attrType1==FLOAT)
        {
          memcpy(&key_float, value, sizeof(float));
          memcpy(&temp_float, data+sizeof(int), sizeof(float));
          if( key_float < temp_float ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
        else if(OpenIndex[FileDesc]->attrType1==STRING)
        {
          memcpy(key_string, value, OpenIndex[FileDesc]->attrLength1);
          memcpy(temp_string,data+sizeof(int), OpenIndex[FileDesc]->attrLength1);
          if(strcmp(key_string, temp_string) < 0 ) break;
          else data+=sizeof(int) + OpenIndex[FileDesc]->attrLength1;
        }
      }
      current_block=*data;
      CALL_OR_DIE( BF_UnpinBlock(block) );
      CALL_OR_DIE( BF_GetBlock(OpenIndex[FileDesc]->fileDesc,current_block,block) );
      data=BF_Block_GetData(block);
    }
    else
    {
      number= *(data+sizeof(char)+sizeof(int)*2);
      OpenScans[i]->bl_num=current_block;
      data+=sizeof(char)+sizeof(int)*3;
      OpenScans[i]->rec_num=0;
      for(j=0;j<number;j++)
      {
        if(OpenIndex[FileDesc]->attrType1 == INTEGER)
        {
          memcpy(&key_int, value, sizeof(int));
          if(key_int <= *data)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
        else if(OpenIndex[FileDesc]->attrType1 == FLOAT)
        {
          memcpy(&temp_float, value, sizeof(float));
          memcpy(&key_float, data, sizeof(float));
          if(temp_float <= key_float)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
        else if(OpenIndex[FileDesc]->attrType1 == STRING)
        {
          memcpy(temp_string, value, OpenIndex[FileDesc]->attrLength1);
          memcpy(key_string, data, OpenIndex[FileDesc]->attrLength1);
          if(strcmp(temp_string, key_string) <= 0)
          {
            data+=OpenIndex[FileDesc]->attrLength1;
            free(temp_string); free(key_string);
            CALL_OR_DIE( BF_UnpinBlock(block) );
            return;
          }
          else
          {
            data+= OpenIndex[FileDesc]->attrLength1 + OpenIndex[FileDesc]->attrLength2;
            (OpenScans[i]->rec_num)++;
          }
        }
      }
      free(temp_string); free(key_string);
      CALL_OR_DIE( BF_UnpinBlock(block) );
      return;
    }
  }
}


int AM_OpenIndexScan(int FileDesc, int op, void *value)
{
  if(OpenIndex[FileDesc]==NULL)/* We check if the file exists */
  {
    AM_errno=AME_NO_OPEN_FILE_SCAN;
    return AM_errno;
  }
  if(op<1 || op>6){
    AM_errno = AME_WRONG_OPERATOR;
    return AME_WRONG_OPERATOR;
  }
  Scan_page_t *temp= (Scan_page_t*) malloc(sizeof(Scan_page_t));
  temp->fd=FileDesc;
  temp->op=op;

  int i;
  for(i=0;i<MAXSCANS;i++)
  {
    if(OpenScans[i] == NULL)/* At the first free place, create a scan with the given arguements*/
    {
      OpenScans[i]=temp;
      break;
    }
  }
  if(i==20)/* we check if the scan array is full */
  {
    AM_errno=AME_FULL_SCANS;
    return AME_FULL_SCANS;
  }

  if(op==1) One(FileDesc,value,i);
  else if(op==2) Two(FileDesc,value,i);
  else if(op==3) Three(FileDesc,value,i);
  else if(op==4) Four(FileDesc,value,i);
  else if(op==5) Five(FileDesc,value,i);
  else if(op==6) Six(FileDesc,value,i);

  return i;
}


int AM_CloseIndexScan(int scanDesc)
{
  OpenScans[scanDesc]=NULL;
  return AME_OK;
}



void AM_PrintError(char *errString) {
  printf("%s\n", errString);
  switch(AM_errno){
    case AME_EOF:
      printf("Reached End Of File.\n");
      break;
    case AME_DELETE:
      printf("Error deleting the file.\n");
      break;
    case AME_FILE:
      printf("This is not an AM File.\n");
      break;
    case AME_NO_OPENFILE:
      printf("There is no open file with this specific file descriptor.\n");
      break;
    case AME_OPEN_FULL:
      printf("Cannot open the file as there are already max open files. Please close one.\n");
      break;
    case AME_TYPES_LENGTH:
      printf("The given type length does not match the given type ('i', 'f' == 4 && 1<='c'=<255)\n");
      break;
    case AME_NO_OPEN_FILE_SCAN:
      printf("The given file descriptor does not represent an open file.\n");
      break;
    case AME_FULL_SCANS:
      printf("There is no room for another scan. Please close one first.\n");
      break;
    case AME_SCAN_NOT_OPENED:
      printf("There is no open scan with the given file descriptor.\n");
      break;
    case AME_WRONG_OPERATOR:
      printf("Wrong operator inserted. Please give an operator between 1 and 6.\n");
      break;
    default:
      printf("Other Error.\n");
  }
}

void AM_Close() {

  for(int i=0; i<BF_MAX_OPEN_FILES; i++){
    free(OpenIndex[i]);
    free(OpenScans[i]);
  }

  free(OpenIndex);
  free(OpenScans);

  CALL_OR_DIE(BF_Close());

  return;
  
}


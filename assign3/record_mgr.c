#include "dberror.h"
#include "storage_mgr.h"
#include "buffer_mgr.h"
#include "record_mgr.h"
#include "tables.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include <math.h>


// helpers


char *copyString(char *_string) {
  char *string = newStr(strlen(_string)); // TODO free it[count, multiple maybe]
  strcpy(string, _string);
  return string;
}


void freeSchemaObjects(int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int *keyAttrs) {
  int i;
  for (i = 0; i < numAttr; i++) {
    free(attrNames[i]);
  }
  free(attrNames);
  free(dataTypes);
  free(typeLength);
  free(keyAttrs);
}


int getSchemaStringLength(char *string) {
  int length;
  int formatLen = 8;
  char intString[formatLen + 1];
  memcpy(&intString[0], string, formatLen);
  intString[formatLen] = '\0';
  length = (int) strtol(intString, NULL, 16);
  return length;
}


char * stringifySchema(Schema *schema) {
  char *string = newStr(PAGE_SIZE - 1); // -1 because newStr adds 1 byte for \0 terminator // TODO free it[count, multiple maybe]
  // placeholder for schema length
  int formatLen = 8;
  char *format = "%08x";
  char intString[formatLen + 1];// +1 for \0 terminator
  sprintf(&intString[0], format, 0); // placeholder for schema length
  strcat(string, intString);
  strcat(string, DELIMITER);
  sprintf(&intString[0], format, schema->numAttr);
  strcat(string, intString);
  strcat(string, DELIMITER);
  int i;
  for (i = 0; i < schema->numAttr; i++) {
    strcat(string, schema->attrNames[i]);
    strcat(string, DELIMITER);
    sprintf(&intString[0], format, schema->dataTypes[i]);
    strcat(string, intString);
    strcat(string, DELIMITER);
    sprintf(&intString[0], format, schema->typeLength[i]);
    strcat(string, intString);
    strcat(string, DELIMITER);
  }
  sprintf(&intString[0], format, schema->keySize);
  strcat(string, intString);
  strcat(string, DELIMITER);
  i = 0;
  for (i = 0; i < schema->keySize; i++) {
    sprintf(&intString[0], format, schema->keyAttrs[i]);
    strcat(string, intString);
    if (i < schema->keySize - 1) { // no delimiter for the last value
      strcat(string, DELIMITER);
    }
  }
  int length = (int) strlen(string);
  length++; // +1 for \0 terminator
  if (length > PAGE_SIZE) {
    // TODO throw error
  }
  sprintf(&intString[0], format, length);
  memcpy(string, &intString, formatLen);
  return string;
}


Schema * parseSchema(char *_string) {
  int length = getSchemaStringLength(_string);
  char *string = newCharArr(length); // TODO free it, Done below
  memcpy(string, _string, length);
  char *token;
  token = strtok(string, DELIMITER); // ignore it's already the length of schema
  token = strtok(NULL, DELIMITER);
  int numAttr = (int) strtol(token, NULL, 16);
  char **attrNames = newArray(char *, numAttr); // TODO free it, Done in freeSchemaObjects
  DataType *dataTypes = newArray(DataType, numAttr); // TODO free it, Done in freeSchemaObjects
  int *typeLength = newIntArr(numAttr); // TODO free it, Done in freeSchemaObjects
  int i;
  for (i = 0; i < numAttr; i++) {
    token = strtok(NULL, DELIMITER);
    attrNames[i] = copyString(token); // TODO free it, Done in freeSchemaObjects

    token = strtok(NULL, DELIMITER);
    dataTypes[i] = (DataType) strtol(token, NULL, 16);

    token = strtok(NULL, DELIMITER);
    typeLength[i] = (int) strtol(token, NULL, 16);
  }
  token = strtok(NULL, DELIMITER);
  int keySize = (int) strtol(token, NULL, 16);
  int *keyAttrs = newIntArr(keySize); // TODO free it, Done in freeSchemaObjects
  for (i = 0; i < keySize; i++) {
    token = strtok(NULL, DELIMITER);
    keyAttrs[i] = (int) strtol(token, NULL, 16);
  }
  Schema *s = createSchema(numAttr, attrNames, dataTypes, typeLength, keySize, keyAttrs);
  freeSchemaObjects(numAttr, attrNames, dataTypes, typeLength, keyAttrs);
  free(string);
  return s;
}


int getAttrStartingPosition(Schema *schema, int attrNum) {
  int position = 0;
  int i;
  for (i = 0; i < attrNum; i++) {
    switch (schema->dataTypes[i]) {
      case DT_INT:
        position += sizeof(int);
        break;
      case DT_FLOAT:
        position += sizeof(float);
        break;
      case DT_STRING:
        position += schema->typeLength[i] + 1; // +1 for \0 terminator
        break;
      case DT_BOOL:
        position += sizeof(bool);
        break;
    }
  }
  return position;
}


void printSchema(Schema *schema) {
  char del;
  printf("{\n\tnumAttr : %d,\n\tattrs : [\n", schema->numAttr);
  int i;
  for (i = 0; i < schema->numAttr; i++) {
    del = (i < schema->numAttr - 1) ? ',' : ' ';
    printf("\t\t[%s,%d,%d]%c\n", schema->attrNames[i], (int) schema->dataTypes[i], schema->typeLength[i], del);
  }
  printf("\t],\n\tkeySize : %d,\n\tkeyAttrs : [\n", schema->keySize);
  for (i = 0; i < schema->keySize; i++) {
    del = (i < schema->keySize - 1) ? ',' : ' ';
    printf("\t\t%d%c\n", schema->keyAttrs[i], del);
  }
  printf("\t]\n}\n\n");
}


void printRecord(Schema *schema, Record * record) {
  char del;
  Value *val;
  int i;
  RC err;
  printf("\n{\n");
  for (i = 0; i < schema->numAttr; i ++) {
    del = (i < schema->numAttr - 1) ? ',' : ' ';
    if ((err = getAttr(record, schema, i, &val))) {
      // TODO throw err
    }
    switch (val->dt) {
      case DT_INT:
        printf("\t%s : %d%c\n", schema->attrNames[i], val->v.intV, del);
        break;
      case DT_FLOAT:
        printf("\t%s : %g%c\n", schema->attrNames[i], val->v.floatV, del);
        break;
      case DT_BOOL:
        if (val->v.boolV) {
          printf("\t%s : true%c\n", schema->attrNames[i], del);
        }
        else {
          printf("\t%s : false%c\n", schema->attrNames[i], del);
        }
        break;
      case DT_STRING:
        printf("\t%s : \"%s\"%c\n", schema->attrNames[i], val->v.stringV, del);
        free(val->v.stringV);
        break;
    }
    free(val);
  }
  printf("}\n\n");
}


int getRecordSizeInBytes(Schema *schema, bool withTerminators) {
  int size = 0;
  int i;
  for (i = 0; i < schema->numAttr; i++) {
    switch (schema->dataTypes[i]) {
      case DT_INT:
        size += sizeof(int);
        break;
      case DT_FLOAT:
        size += sizeof(float);
        break;
      case DT_STRING:
        size += schema->typeLength[i];
        if (withTerminators) {
          size++; // +1 for \0 terminator
        }
        break;
      case DT_BOOL:
        size += sizeof(bool);
        break;
    }
  }
  return size;
}


void setBit(char *bitMap, int bitIdx) {
  int byteIdx = bitIdx / NUM_BITS;
  int bitSeq = NUM_BITS - (bitIdx % NUM_BITS);
  bitMap[byteIdx] = bitMap[byteIdx] | 1 << (bitSeq - 1);
}


int getUnsetBitIndex(char *bitMap, int bitmapSize) {
  int bytesCount = 0;
  unsigned char byte;
  while(bitmapSize - bytesCount) {
    byte = bitMap[bytesCount];
    if (byte < UCHAR_MAX) {
      int i = NUM_BITS;
      while(byte & (1 << (i - 1))) {
        --i;
      }
      return (bytesCount * NUM_BITS) + (NUM_BITS - i);
    }
    bytesCount++;
  }
  return -1;
}


RC getEmptyPage(RM_TableData *rel, int *pageNum) {
  RC err;
  BM_BufferPool *bm = (BM_BufferPool *) rel->mgmtData;
  BM_PageHandle *page = new(BM_PageHandle); // TODO free it, Done below
  if ((err = pinPage(bm, page, 0))) { // page zero contains the header
    free(page);
  }

  int schemaLen = getSchemaStringLength(page->data);
  char *ptr;
  int res;
  if (schemaLen < PAGE_SIZE) {
    ptr = page->data;
    ptr += schemaLen;
    res = getUnsetBitIndex(ptr, PAGE_SIZE - schemaLen);
    if (res >= 0) {
      *(pageNum) = res;
      free(page);
      return RC_OK;
    }
  }

  // not found in page 0
  if ((err = pinPage(bm, page, 1))) { // page one contains the rest of bitMap
    free(page);
  }
  ptr = page->data;
  res = getUnsetBitIndex(ptr, PAGE_SIZE); // no schema on page 1
  free(page);
  if (res >= 0) {
    *(pageNum) = res + ((PAGE_SIZE - schemaLen) * 8); // add the bits that are in the page 0
    return RC_OK;
  }
  return RC_RM_LIMIT_EXCEEDED; // no more space on that file
}


//functionality


RC initRecordManager (void *mgmtData) {
  // TODO
  initStorageManager();
  return RC_OK;
}


RC shutdownRecordManager () {
  // TODO
  return RC_OK;
}


RC createTable (char *name, Schema *schema) {
  // TODO check if not already exists. // TODO downcase the name
  RC err;
  if ((err = createPageFile(name))) {
    return err; // TODO THROW maybe because nothing can be dont after this point
  }

  // No need to ensureCapacity, because creating a file already ensures one block, we dont need more for now.
  // TODO check if schemaString is less than pageSize, else (find a new solution)
  char *schemaString = stringifySchema(schema);
  SM_FileHandle fileHandle;
  if ((err = openPageFile(name, &fileHandle))) {
    return err;
  }
  if ((err = writeBlock(0, &fileHandle, schemaString))) {
    free(schemaString);
    return err;
  }
  free(schemaString);
  if ((err = closePageFile(&fileHandle))) {
    return err;
  }
  return RC_OK;
}


RC openTable (RM_TableData *rel, char *name) {
  // TODO checl all errors and free resources on error or throw it. // TODO downcase name
  RC err;
  BM_BufferPool *bm = new(BM_BufferPool); // TODO free it
  BM_PageHandle *pageHandle = new(BM_PageHandle); // TODO free it, Done below
  if ((err = initBufferPool(bm, name, PER_TBL_BUF_SIZE, RS_LRU, NULL))) {
    return err;
  }
  if ((err = pinPage(bm, pageHandle, 0))) {
    return err;
  }
  Schema *schema = parseSchema(pageHandle->data);
  rel->name = copyString(name); // TODO free it, Done in closeTable
  rel->schema = schema;
  int recordSize = getRecordSizeInBytes(schema, true);
  int pageSize = PAGE_SIZE - PAGE_HEADER_LEN;
  rel->maxSlotsPerPage = floor((pageSize * NUM_BITS) / (recordSize * NUM_BITS + 1)); // pageSize = (X * recordSize) + (X / NUM_BITS)
  rel->slotsBitMapSize = ceil(rel->maxSlotsPerPage / NUM_BITS);
  rel->mgmtData = bm;
  if ((err = unpinPage(bm, pageHandle))) {
    return err;
  }
  free(pageHandle);
  return RC_OK;
}


RC closeTable (RM_TableData *rel) {
  BM_BufferPool *bm = (BM_BufferPool *) rel->mgmtData;
	RC err;
  if ((err = shutdownBufferPool(bm))) {
    return err;
  }
  free(rel->name);
  if ((err = freeSchema(rel->schema))) {
    return err;
  }
  free(rel->mgmtData);
  return RC_OK;
}


Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys) {
  Schema *schema = new(Schema); // TODO free it, Done in freeSchema
  schema->numAttr = numAttr;
  schema->attrNames = newArray(char *, numAttr); // TODO free it, Done in freeSchema
  schema->dataTypes = newArray(DataType, numAttr); // TODO free it, Done in freeSchema
  schema->typeLength = newIntArr(numAttr); // TODO free it, Done in freeSchema
  int i;
  int len;
  for (i = 0; i < numAttr; i++) {
    schema->attrNames[i] = copyString(attrNames[i]); // TODO free it, Done in freeSchema
    schema->dataTypes[i] = dataTypes[i];
    switch (dataTypes[i]) {
      case DT_INT:
        len = sizeof(int);
        break;
      case DT_FLOAT:
        len = sizeof(float);
        break;
      case DT_BOOL:
        len = sizeof(bool);
        break;
      case DT_STRING:
        len = typeLength[i];
        break;
    }
    schema->typeLength[i] = len;
  }
  schema->keySize = keySize;
  schema->keyAttrs = newIntArr(keySize); // TODO free it, Done in freeSchema
  for (i = 0; i < keySize; i++) {
    schema->keyAttrs[i] = keys[i];
  }
  return schema;
}


RC freeSchema (Schema *schema) {
  freeSchemaObjects(schema->numAttr, schema->attrNames, schema->dataTypes, schema->typeLength, schema->keyAttrs);
  free(schema);
  return RC_OK;
}


int getRecordSize (Schema *schema) {
  return getRecordSizeInBytes(schema, false);
}


RC deleteTable (char *name) {
  RC err = destroyPageFile(name);
  // TODO more descriptive error
  return err;
}


RC createRecord (Record **record, Schema *schema) {
  int size = getRecordSizeInBytes(schema, true);
  Record *r = new(Record); // TODO free it, Done in freeRecord
  r->data = newCharArr(size); // TODO free it, Done in freeRecord
  *record = r;
  return RC_OK;
}


RC freeRecord (Record *record) {
  free(record->data);
  free(record);
  return RC_OK;
}


RC setAttr (Record *record, Schema *schema, int attrNum, Value *value) {
  int position = getAttrStartingPosition(schema, attrNum);
  char *ptr = record->data;
  ptr += position;
  int size;
  switch(value->dt) {
    case DT_INT:
      if (value->v.intV > INT_MAX || value->v.intV < INT_MIN) {
        return RC_RM_LIMIT_EXCEEDED;
      }
      size = sizeof(int);
      memcpy(ptr, &value->v.intV, size);
      break;
    case DT_FLOAT:
      if (value->v.floatV > FLT_MAX || value->v.floatV < FLT_MIN) {
        return RC_RM_LIMIT_EXCEEDED;
      }
      size = sizeof(float);
      memcpy(ptr, &value->v.floatV, size);
      break;
    case DT_STRING:
      if (strlen(value->v.stringV) > schema->typeLength[attrNum]) {
        return RC_RM_LIMIT_EXCEEDED;
      }
      size = schema->typeLength[attrNum] + 1; // +1 for \0 terminator
      memcpy(ptr, value->v.stringV, size);
      ptr[size - 1] = '\0'; // for safety
      break;
    case DT_BOOL:
      if (value->v.boolV != true && value->v.boolV != false) {
        return RC_RM_LIMIT_EXCEEDED;
      }
      size = sizeof(bool);
      memcpy(ptr, &value->v.boolV, size);
      break;
    default :
      return RC_RM_UNKOWN_DATATYPE;
  }
  return RC_OK;
}


RC getAttr (Record *record, Schema *schema, int attrNum, Value **value) {
  Value *val = new(Value); // TODO free it
  int position = getAttrStartingPosition(schema, attrNum);
  int size;
  char *ptr = record->data;
  ptr += position;
  val->dt = schema->dataTypes[attrNum];
  switch (schema->dataTypes[attrNum]) {
    case DT_INT:
      size = sizeof(int);
      memcpy(&val->v.intV, ptr, size);
      break;
    case DT_FLOAT:
      size = sizeof(float);
      memcpy(&val->v.floatV, ptr, size);
      break;
    case DT_STRING:
      size = schema->typeLength[attrNum];
      val->v.stringV = newStr(size); // TODO free it
      memcpy(val->v.stringV, ptr, size + 1); // +1 for \0 terminator
      break;
    case DT_BOOL:
      size = sizeof(bool);
      memcpy(&val->v.boolV, ptr, size);
      break;
  }
  *value = val;
  return RC_OK;
}


RC insertRecord (RM_TableData *rel, Record *record) {
  RC err;
  BM_BufferPool *bm = (BM_BufferPool *) rel->mgmtData;
  BM_PageHandle *page = new(BM_PageHandle); // TODO free it, Done below
  int recordSize = getRecordSizeInBytes(rel->schema, true);
  int pageNum, slotNum;
  short totalSlots;
  if ((err = getEmptyPage(rel, &pageNum))) {
    return err;
  }
  pageNum += TABLE_HEADER_PAGES_LEN;
  if ((err = pinPage(bm, page, pageNum))) {
    free(page);
    return err;
  }
  char *ptr = page->data;
  memcpy(&totalSlots, ptr, sizeof(short)); // may use later
  ptr += BYTES_SLOTS_COUNT;
  memcpy(&slotNum, ptr, BYTES_SLOTS_COUNT); // copy nextSlot pointer

  // TODO dont skip, and deal with slotNum
  if (true || slotNum < 0) { // means the nextSlot doesnot know where to write
    ptr = page->data + PAGE_HEADER_LEN;
    printf("---OLD_BITS--- %d\n", (int)(unsigned char) ptr[0]);
    slotNum = getUnsetBitIndex(ptr, rel->slotsBitMapSize);
  }

  if (slotNum < 0 || slotNum >= rel->maxSlotsPerPage) { // -1 means not found and it is a problem
    // TODO THROW, it should not go over maxSlot
  }

  printf("---SLOT--- %d\n", slotNum);
  ptr = page->data; // pointer to bytes array
  totalSlots++;
  memcpy(ptr, &totalSlots, sizeof(short)); // writing the totalSlots
  printf("---NEW_TOTAL--- %d\n", totalSlots);
  ptr = page->data + PAGE_HEADER_LEN;
  setBit(ptr, slotNum); // setting the slot bits
  printf("---NEW_BITS--- %d\n", (int)(unsigned char) ptr[0]);
  // TODO set the next free slot
  int position = PAGE_HEADER_LEN + rel->slotsBitMapSize + (recordSize * slotNum);
  ptr = page->data + position;
  memcpy(ptr, record->data, recordSize);
  if ((err = markDirty(bm,page))) {
    free(page);
    return err;
  }
  if ((err = unpinPage(bm, page))) {
    free(page);
    return err;
  }
  free(page);
  record->id.page = pageNum;
  record->id.slot = slotNum;
  return RC_OK;
}


RC getRecord (RM_TableData *rel, RID id, Record *record) {
  RC err;
  BM_BufferPool *bm = (BM_BufferPool *) rel->mgmtData;
  BM_PageHandle *page = new(BM_PageHandle); // TODO free it, Done below
  int recordSize = getRecordSizeInBytes(rel->schema, true);
  int pageNum = record->id.page;
  int slotNum = record->id.slot;
  pageNum += TABLE_HEADER_PAGES_LEN;
  if ((err = pinPage(bm, page, pageNum))) {
    free(page);
    return err;
  }
  int position = PAGE_HEADER_LEN + rel->slotsBitMapSize + (recordSize * slotNum);
  char *ptr = page->data; // pointer to bytes array
  ptr += position;
  memcpy(record->data, ptr, recordSize);
  if ((err = unpinPage(bm, page))) {
    free(page);
    return err;
  }
  free(page);
  return RC_OK;
}


int main(int argc, char *argv[]) {
  int a = 4;
  char **b = newArray(char *, a);
  b[0] = "SH";
  b[1] = "WE";
  b[2] = "EL";
  b[3] = "AN";
  DataType *c = newArray(DataType, a);
  c[0] = DT_STRING;
  c[1] = DT_INT;
  c[2] = DT_BOOL;
  c[3] = DT_FLOAT;
  int *d = newIntArr(a);
  d[0] = 1000;
  d[1] = 0;
  d[2] = 0;
  d[3] = 0;
  int e = 2;
  int *f = newIntArr(e);
  f[0] = 1;
  f[1] = 3;
  printf("\n\n");
  initRecordManager(NULL);
  Schema *s = createSchema(a, b, c, d, e, f);
  printf("1.1st schema : ");
  printSchema(s);
//  printf("1.1st schema string : %s\n", stringifySchema(s));
//  printf("1.1st schema record size : %d\n\n", getRecordSize(s));
  printf("------------------------------------------------------------------------------------------\n");
  Record *record;
  createRecord(&record, s);
  Value *val = new(Value);
  val->dt = DT_STRING;
  val->v.stringV = copyString("56fs.58274283748237483278947238947932874fkdjshfjhsdjh sejkhfksnkchsfclalcbaewfgaewkkcaew");
  setAttr(record, s, 0, val);
  free(val->v.stringV);
  val->dt = DT_INT;
  val->v.intV = 65;
  setAttr(record, s, 1, val);
  val->dt = DT_BOOL;
  val->v.boolV = true;
  setAttr(record, s, 2, val);
  val->dt = DT_FLOAT;
  val->v.floatV = 56.56;
  setAttr(record, s, 3, val);
  printf("---------- %s\n", "record");
  printRecord(s, record);
  printf("------------------------------------------------------------------------------------------\n");
  free(val);
//  printf("1.2st schema : ");
//  printSchema(s);
//  printf("1.2st schema string : %s\n", stringifySchema(s));
//  printf("1.2st schema record size : %d\n\n", getRecordSize(s));
//  printf("------------------------------------------------------------------------------------------\n");
  createTable("shweelan", s);
  RM_TableData *rel = new(RM_TableData);
  openTable(rel, "shweelan");
//  printf("rel.1 schema : ");
//  printSchema(rel->schema);
  insertRecord(rel, record);
  Record *recordRestored;
  createRecord(&recordRestored, s);
  recordRestored->id.page = 0;
  recordRestored->id.slot = 0;
  getRecord(rel, recordRestored->id, recordRestored);
  printf("---------- %s\n", "recordRestored");
  printRecord(rel->schema, recordRestored);
//  printf("rel.1 schema string : %s\n", stringifySchema(rel->schema));
//  printf("rel.1 schema record size : %d\n\n", getRecordSize(rel->schema));
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  insertRecord(rel, recordRestored);
  printf("------------------------------------------------------------------------------------------\n");
  printf("%d\n", insertRecord(rel, recordRestored));
  closeTable(rel);
  deleteTable("shweelan");
//  printf("1.3st schema : ");
//  printSchema(s);
//  char *ss = stringifySchema(s);
//  printf("1.3st schema string : %s\n", ss);
//  printf("1.3st schema record size : %d\n\n", getRecordSize(s));
//  printf("------------------------------------------------------------------------------------------\n");
//  Schema *ns = parseSchema(ss);
//  printf("2.1nd schema : ");
//  printSchema(ns);
//  char *nss = stringifySchema(ns);
//  printf("2.1nd schema string : %s\n", nss);
//  printf("2.1nd schema record size : %d\n\n", getRecordSize(ns));
//  printf("------------------------------------------------------------------------------------------\n");
//  printf("\n");
//  freeSchema(s);
//  freeSchema(ns);
//  free(ss);
//  free(nss);
  free(b);
  free(c);
  free(d);
  free(f);
  return 0;
}
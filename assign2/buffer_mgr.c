#include "buffer_mgr.h"
#include <stdlib.h>
#include <string.h>


// helpers

PageFrameInfo * initFrame() {
  PageFrameInfo *frame = malloc(sizeof(PageFrameInfo));
  frame->pageData = malloc(sizeof(char) * PAGE_SIZE); // TODO free it .. Done in destroyFrame
  frame->pageNum = NO_PAGE;
  frame->fixCount = 0;
  frame->dirty = false;
  return frame;
}


void destroyFrame(PageFrameInfo *frame) {
  free(frame->pageData);
  free(frame);
  // TODO remove from hashmap if it is there .. Done in replacement
  // TODO remove from linked list .. Done in replacement
}


PageFrameInfo *getFrameByPageNumber(BM_BufferPool *bm, PageNumber pageNum) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  Node *node = (Node *)(hmGet(pmi->hm, pageNum));
  if (node == NULL) {
    return NULL;
  }
  PageFrameInfo *frame = (PageFrameInfo *)(node->data);
  return frame;
}


void setDirty(BM_BufferPool *bm, PageFrameInfo *frame, bool flag) {
  frame->dirty = flag;
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  pmi->statistics->dirtyFlags[frame->statisticsPosition] = flag;
}


void incDecFixCount(BM_BufferPool *bm, PageFrameInfo *frame, bool isInc) {
  int i = -1;
  if (isInc) {
    i = 1;
  }
  frame->fixCount += i;
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  pmi->statistics->fixCounts[frame->statisticsPosition] += i;
}


void setStatistics(BM_BufferPool *bm, PageFrameInfo *frame, int position) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  pmi->statistics->pageNumbers[position] = frame->pageNum;
  pmi->statistics->dirtyFlags[position] = frame->dirty;
  pmi->statistics->fixCounts[position] = frame->fixCount;
  frame->statisticsPosition = position;
}


RC writeDirtyPage(BM_BufferPool *bm, PageFrameInfo *frame) {
  //Write dirty page into memory
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  RC writeStatus = writeBlock(frame->pageNum,  pmi->fHandle, frame->pageData);
  if (writeStatus != RC_OK) {
    return writeStatus;
  }
  pmi->statistics->writeIO++;
  setDirty(bm, frame, false);
  return RC_OK;
}


RC replacement(BM_BufferPool *bm, PageFrameInfo *frame, PageNumber pageNum) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  RC err = readBlock(pageNum, pmi->fHandle, frame->pageData);
  if (err != RC_OK) {
    return err;
  }
  pmi->statistics->readIO++;
  int position;
  if (pmi->statistics->readIO > bm->numPages) {
    PageFrameInfo *toReplace = NULL;
    // here goes the replacement
    Node *tmp = pmi->head;
    while(true) {
      toReplace = (PageFrameInfo *)(tmp->data);
      if (toReplace->fixCount == 0) {
        break;
      }
      tmp = tmp->next;
      if (tmp == NULL) {
        return RC_NO_SPACE_IN_POOL;
      }
    }
    position = toReplace->statisticsPosition;
    if (toReplace->dirty) {
      RC err = writeDirtyPage(bm, toReplace);
      if (err != RC_OK) {
        return err;
      }
    }
    // remove from linked list
    if (tmp == pmi->head || tmp == pmi->tail) {
      if (tmp == pmi->head) {
        pmi->head = tmp->next;
        pmi->head->previous = NULL;
      }
      if (tmp == pmi->tail) {
        pmi->tail = tmp->previous;
        pmi->tail->next = NULL;
      }
    }
    else {
      tmp->previous->next = tmp->next;
      tmp->next->previous = tmp->previous;
    }
    hmDelete(pmi->hm, toReplace->pageNum);
    destroyFrame(toReplace);
    free(tmp);
  }
  else {
    position = pmi->statistics->readIO - 1;
  }
  frame->pageNum = pageNum;
  Node *node = malloc(sizeof(Node)); // TODO free it .. Done in replacement
  node->next = NULL;
  node->previous = NULL;
  node->data = frame;
  if (pmi->head == NULL && pmi->tail == NULL) {
    pmi->head = node;
    pmi->tail = node;
  }
  else {
    pmi->tail->next = node;
    node->previous = pmi->tail;
    pmi->tail = node;
  }
  hmInsert(pmi->hm, frame->pageNum, node); // TODO free using hmDelete() .. Done in replacement
  setStatistics(bm, frame, position);
  return RC_OK;
}


RC fifo(BM_BufferPool *bm, PageFrameInfo *frame, PageNumber pageNum) {
  if (frame->pageNum != NO_PAGE) {
    return RC_OK;
  }
  return replacement(bm, frame, pageNum);
}


RC lru(BM_BufferPool *bm, PageFrameInfo *frame, PageNumber pageNum) {
  if (frame->pageNum != NO_PAGE) {
    PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
    // shuffle the linked list
    Node *node = (Node *)(hmGet(pmi->hm, pageNum));
    if (node == pmi->tail) {
      return RC_OK;
    }
    if (node == pmi->head) {
      // removing node from head
      pmi->head = node->next;
      pmi->head->previous = NULL;
    }
    else {
      // removing node from tail
      node->previous->next = node->next;
      node->next->previous = node->previous;
    }
    // attaching node to tail
    node->next = NULL;
    node->previous = pmi->tail;
    pmi->tail->next = node;
    pmi->tail = node;
    return RC_OK;
  }
  else {
    return replacement(bm, frame, pageNum);
  }
}


RC execStrategy(BM_BufferPool *bm, PageFrameInfo *frame, PageNumber pageNum) {
  if (bm->strategy == RS_FIFO) {
    return fifo(bm, frame, pageNum);
  }
  if (bm->strategy == RS_LRU) {
    return lru(bm, frame, pageNum);
  }
  return RC_STRATEGY_NOT_SUPPORTED;
}


//functionality

RC initBufferPool(BM_BufferPool *const bm, const char *const pageFileName, const int numPages, ReplacementStrategy strategy, void *stratData) {
  SM_FileHandle *fHandle = malloc(sizeof(SM_FileHandle));// TODO free it .. Done in shutdownBufferPool
  char *fname = malloc(sizeof(char) * strlen(pageFileName));// TODO free it .. Done in shutdownBufferPool
  strcpy(fname, pageFileName); // copying the const char* const to char*
  RC err = openPageFile(fname, fHandle); // TODO close it .. Done in shutdownBufferPool
  if (err != RC_OK) {
    return err;
  }

  bm->pageFile = fname;
  bm->numPages = numPages;
  bm->strategy = strategy;
  PoolMgmtInfo *pmi = malloc(sizeof(PoolMgmtInfo)); // TODO free it .. Done in shutdownBufferPool
  pmi->hm = hmInit(); // TODO free it, use hmDestroy(hm) .. Done in shutdownBufferPool
  pmi->head = NULL;
  pmi->tail = NULL;
  pmi->fHandle = fHandle;

  pmi->statistics = malloc(sizeof(Statistics)); // TODO free it .. Done in shutdownBufferPool
  pmi->statistics->pageNumbers = malloc(sizeof(int) * bm->numPages); // TODO free it .. Done in shutdownBufferPool
  pmi->statistics->fixCounts = calloc(sizeof(int) * bm->numPages, sizeof(int)); // TODO free it .. Done in shutdownBufferPool
  pmi->statistics->dirtyFlags = calloc(sizeof(int) * bm->numPages, sizeof(int)); // TODO free it .. Done in shutdownBufferPool
  int i;
  for (i = 0; i < bm->numPages; i++) {
    pmi->statistics->pageNumbers[i] = -1;
  }
  pmi->statistics->readIO = 0;
  pmi->statistics->writeIO = 0;

  bm->mgmtData = pmi;
  return RC_OK;
}


RC shutdownBufferPool(BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  Node *tmp = pmi->head;
  Node *deletable;
  PageFrameInfo *frame;
  while(tmp != NULL) {
    frame = (PageFrameInfo *)(tmp->data);
    if (frame->dirty) {
      RC err = writeDirtyPage(bm, frame);
      if (err != RC_OK) {
        return err;
      }
    }
    destroyFrame(frame);
    deletable = tmp;
    tmp = tmp->next;
    free(deletable);
  }
  hmDestroy(pmi->hm);
  RC err =  closePageFile(pmi->fHandle);
  if (err != RC_OK) {
    return err;
  }
  free(pmi->statistics->pageNumbers);
  free(pmi->statistics->fixCounts);
  free(pmi->statistics->dirtyFlags);
  free(pmi->statistics);
  free(pmi->fHandle);
  free(pmi->fHandle->mgmtInfo);
  free(pmi);
  free(bm->pageFile);
  return RC_OK;
}


RC forceFlushPool(BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  Node *tmp = pmi->head;
  PageFrameInfo *frame;
  while (tmp != NULL) {
    frame = (PageFrameInfo *)(tmp->data);
    if (frame->dirty && frame->fixCount == 0) {
      RC err = writeDirtyPage(bm, frame);
      if (err != RC_OK) {
        return err;
      }
    }
    tmp = tmp->next;
  }
  return RC_OK;
}


RC pinPage(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum) {
  PageFrameInfo *frame = getFrameByPageNumber(bm, pageNum);
  if (frame == NULL) {
    frame = initFrame(); // TODO free, use destroyFrame(frame) .. Done in shutdownBufferPool, replacement
  }
  RC err = execStrategy(bm, frame, pageNum);
  if (err != RC_OK) {
    destroyFrame(frame);
    return err;
  }
  incDecFixCount(bm, frame, true); // true means increase
  page->data = frame->pageData;
  page->pageNum = frame->pageNum;
  return RC_OK;
}


RC unpinPage(BM_BufferPool *const bm, BM_PageHandle *const page) {
  PageFrameInfo *frame = getFrameByPageNumber(bm, page->pageNum);
  if (frame == NULL) {
    return RC_ERROR_NO_PAGE;
  }
  incDecFixCount(bm, frame, false); // false means decrease
  return RC_OK;
}


RC markDirty(BM_BufferPool *const bm, BM_PageHandle *const page) {
  PageFrameInfo *frame = getFrameByPageNumber(bm, page->pageNum);
  if (frame == NULL) {
    return RC_ERROR_NO_PAGE;
  }
  setDirty(bm, frame, true);
  return RC_OK;
}


RC forcePage(BM_BufferPool *const bm, BM_PageHandle *const page) {
  PageFrameInfo *frame = getFrameByPageNumber(bm, page->pageNum);
  if (frame == NULL) {
    return RC_ERROR_NO_PAGE;
  }
  if (!frame->dirty) {
    return RC_OK;
  }
  if (frame->fixCount > 0) {
    return RC_ERROR_NOT_FREE_FRAME;
  }
  RC err = writeDirtyPage(bm, frame);
  if (err != RC_OK) {
    return err;
  }
  return RC_OK;
}


PageNumber *getFrameContents (BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  return pmi->statistics->pageNumbers;
}


bool *getDirtyFlags (BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  return pmi->statistics->dirtyFlags;
}


int *getFixCounts (BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  return pmi->statistics->fixCounts;
}


int getNumReadIO (BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  return pmi->statistics->readIO;
}


int getNumWriteIO (BM_BufferPool *const bm) {
  PoolMgmtInfo *pmi = (PoolMgmtInfo *)(bm->mgmtData);
  return pmi->statistics->writeIO;
}

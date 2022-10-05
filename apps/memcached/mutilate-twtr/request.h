#ifndef REQUEST_H
#define REQUEST_H

enum OpType { GET, SET };

typedef struct {
  char key[256];
  int keysize;
  int valsize;
  OpType operation;
} request_t;

#endif // REQUEST_H

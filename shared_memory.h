typedef struct {
    char data[8192];
    char operation[2048];
    char result[65536];
    bool ready;
    bool terminate;
    LWLock lock;
} PandasTask;

#define MAX_TASKS 1024

typedef struct {
    PandasTask tasks[MAX_TASKS];
    int front;
    int rear;
    LWLock lock;
} PandasTaskQueue;

typedef struct {
    PandasTaskQueue queue;
} PandasSharedData; 
#include "sleep.h"

#include <unistd.h>

#define SLEEP_MICROSEC 100000

void my_sleep() {
    usleep(SLEEP_MICROSEC);
}

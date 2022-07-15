#include "server.h"

int main()
{
    Server server;
    server.environment_init();
    server.mainstart();
    return 0;
}
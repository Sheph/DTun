#include "DTun/SignalHandler.h"
#include "DTun/SignalBlocker.h"
#include <iostream>

static void signalHandler(int sig)
{
    std::cout << "Signal " << sig << " received\n";
}

int main(int argc, char* argv[])
{
    DTun::SignalBlocker signalBlocker;

    DTun::SignalHandler sigHandler(&signalHandler);

    return 0;
}

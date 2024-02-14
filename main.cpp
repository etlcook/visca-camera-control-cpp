#include "control-loop.cpp"

std::mutex jIPPTZCommandMutexG;
int zoomCameraCommand;

int main()
{
    int tempCmd = 1;

    std::thread jIPPTZLoop(jIPPTZThread);

    while(1)
    {
        std::cin >> tempCmd;
        jIPPTZCommandMutexG.lock();
        zoomCameraCommand = tempCmd;
        jIPPTZCommandMutexG.unlock();
    }

    jIPPTZLoop.join();
    
    return(0);
}

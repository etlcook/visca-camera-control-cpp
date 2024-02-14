#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <iostream>
#include <mutex>

extern std::mutex jIPPTZCommandMutexG;
extern int zoomCameraCommand;

const double UPDATE_RATE = 2.0;
const char* IP_ADDR = "192.168.100.79";
const int PORT = 52381;

void printCmd(int cmd);

void *jIPPTZThread()
{
  // first, initialize everything
  int sleepTime = (long)(1.0 / UPDATE_RATE * 1.0e6);
  double currentTime, prevTime;
  char *commandString;
  
  //                                      type: cmd   length      sequence number       payload
  const unsigned char zoom_position_1x[17]     = { 0x01,0x00,  0x00,0x09,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x47,0x01,0x00,0x00,0x00,0xff};
  const unsigned char zoom_position_2x[17]     = { 0x01,0x00,  0x00,0x09,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x47,0x02,0x00,0x00,0x00,0xff};
  const unsigned char zoom_position_3_5x[17]   = { 0x01,0x00,  0x00,0x09,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x47,0x03,0x08,0x00,0x00,0xff};

  const unsigned char stabilizer_on[14]        = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x34,0x02,0xff};
  const unsigned char stabilizer_off[14]       = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x34,0x03,0xff};

  const unsigned char zoom_stop[14]            = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x07,0x00,0xff};
  const unsigned char zoom_in[14]              = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x07,0x22,0xff};
  const unsigned char zoom_out[14]             = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x07,0x32,0xff};

  const unsigned char focus_stop[14]           = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x08,0x00,0xff};
  const unsigned char focus_far[14]            = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x08,0x21,0xff};
  const unsigned char focus_near[14]           = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x08,0x31,0xff};
  const unsigned char focus_auto[14]           = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x38,0x02,0xff};
  const unsigned char focus_manual[14]         = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x38,0x03,0xff};

  const unsigned char iris_up[14]              = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x0b,0x02,0xff};
  const unsigned char iris_down[14]            = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x0b,0x03,0xff};
  const unsigned char iris_reset[14]           = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x0b,0x00,0xff}; // I don't think we should use this

  const unsigned char iris_auto[14]            = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x39,0x00,0xff};
  const unsigned char iris_manual[14]          = { 0x01,0x00,  0x00,0x06,  0x00,0x00,0x00,0x01,  0x81,0x01,0x04,0x39,0x03,0xff};

  // Create UDP socket for sending VISCA to the zoom camera
  const char* hostname = IP_ADDR;
  int port = PORT;

  int sock = ::socket(AF_INET, SOCK_DGRAM, 0);

  sockaddr_in destination;
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);
  destination.sin_addr.s_addr = inet_addr(hostname);

  // Flags to make sure we are setting manual/auto modes correctly
  bool currentlyZooming = false;
  bool irisManualMode = false;
  bool focusCurrentlyChanging = false;
  bool focusCurrentlyManual = false;
  
  int previousCmd = 0;
  // This counts the number of times the current command has already been sent (to be able to send the loop into an idle state)
  int currentCmdSequenceCount = 0;
  int currentCommand = 0;

  // New loop:
  while (1)
  {
    // If we can't open a socket, try again (0 is good status, -1 is bad)
    if (sock == -1)
    {
      sock = ::socket(AF_INET, SOCK_DGRAM, 0);
      usleep(100000);
      continue;
    }

    // copy the current command safely
    jIPPTZCommandMutexG.lock();
    currentCommand = zoomCameraCommand;
    jIPPTZCommandMutexG.unlock();
    
    // Keep track of number of times the current command has been sent
    if (currentCommand == previousCmd)
    {
      currentCmdSequenceCount++;
      // keep previousCmd = currentCommand
    }
    else
    {
      currentCmdSequenceCount = 1;
      previousCmd = currentCommand;
    }
    
    // If we've sent the same command 3 times, and it isn't a cmd that needs continuous sending (only iris up and down need continuous),
    // then don't send any more redundant commands, instead loop quickly to react to new commands with lower latency
    if (currentCmdSequenceCount <= 3 || currentCommand == 9 || currentCommand == 10)
    {                     
      // NOTE: this is using continuous zoom in and out. if this is undesirable, will need to implement
      // increasing and decreasing position based zooming. This would be more work because it requires knowledge of the
      // current position, and we aren't currently listening to VISCA feedback from camera

      // First, make sure that we stop continuous zooming if we have switched away from zoom cmds
      if (currentCommand != 1 && currentCommand != 2 && currentCommand != 3 && currentlyZooming)
      {
        ::sendto(sock, zoom_stop, sizeof(zoom_stop), 0, (sockaddr*)&destination, sizeof(destination));
        currentlyZooming = false;
        currentCommand = 1;
      }
      // Also make sure we stop continuous focusing if we have switched away from focus commands
      else if (currentCommand != 5 && currentCommand != 6 && currentCommand != 7 && currentCommand != 8 && focusCurrentlyChanging)
      {
        ::sendto(sock, focus_stop, sizeof(focus_stop), 0, (sockaddr*)&destination, sizeof(destination));
        focusCurrentlyChanging = false;
        currentCommand = 4;
      }
      //  1 = zoom off
      else if (currentCommand == 1) 
      {
        ::sendto(sock, zoom_stop, sizeof(zoom_stop), 0, (sockaddr*)&destination, sizeof(destination));
        currentlyZooming = false;
      }
      //  2 = zoom in continuously
      else if (currentCommand == 2)  
      {
        ::sendto(sock, zoom_in, sizeof(zoom_in), 0, (sockaddr*)&destination, sizeof(destination));
        currentlyZooming = true;
      }
      //  3 = zoom out continuously
      else if (currentCommand == 3)  
      {
        ::sendto(sock, zoom_out, sizeof(zoom_out), 0, (sockaddr*)&destination, sizeof(destination));
        currentlyZooming = true;
      }
      //  4 = focus stop
      else if (currentCommand == 4) 
      {
        ::sendto(sock, focus_stop, sizeof(focus_stop), 0, (sockaddr*)&destination, sizeof(destination));
        focusCurrentlyChanging = false;
      }
      //  5 = focus far continuously
      else if (currentCommand == 5)  
      {
        // Confirm focus is manual first
        if (!focusCurrentlyManual)
        {
          ::sendto(sock, focus_manual, sizeof(focus_manual), 0, (sockaddr*)&destination, sizeof(destination));
          focusCurrentlyManual = true;
          currentCmdSequenceCount--;
          currentCommand = 8;
        }
        else
        {
          ::sendto(sock, focus_far, sizeof(focus_far), 0, (sockaddr*)&destination, sizeof(destination));
          focusCurrentlyChanging = true;
        }
      }
      //  6 = focus near continuously
      else if (currentCommand == 6)  
      {
        // Confirm focus is manual first
        if (!focusCurrentlyManual)
        {
          ::sendto(sock, focus_manual, sizeof(focus_manual), 0, (sockaddr*)&destination, sizeof(destination));
          focusCurrentlyManual = true;
          currentCmdSequenceCount--;
          currentCommand = 8;
        }
        else
        {
          ::sendto(sock, focus_near, sizeof(focus_near), 0, (sockaddr*)&destination, sizeof(destination));
          focusCurrentlyChanging = true;
        }
      }
      //  7 = focus auto
      else if (currentCommand == 7) 
      {
        ::sendto(sock, focus_auto, sizeof(focus_auto), 0, (sockaddr*)&destination, sizeof(destination));
        focusCurrentlyChanging = false;
        focusCurrentlyManual = false;
      }
      //  8 = focus manual
      else if (currentCommand == 8) 
      {
        ::sendto(sock, focus_manual, sizeof(focus_manual), 0, (sockaddr*)&destination, sizeof(destination));
        focusCurrentlyManual = true;
      }
      //  9 = iris up
      else if (currentCommand == 9) 
      {
        // First, ensure iris is in manual mode
        if (!irisManualMode)
        {
          ::sendto(sock, iris_manual, sizeof(iris_manual), 0, (sockaddr*)&destination, sizeof(destination));
          irisManualMode = true;
          currentCmdSequenceCount--;
          currentCommand = 13;
        }
        else
        {
          ::sendto(sock, iris_up, sizeof(iris_up), 0, (sockaddr*)&destination, sizeof(destination));
        }
      }
      //  10 = iris down
      else if (currentCommand == 10) 
      {
        // First, ensure iris is in manual mode
        if (!irisManualMode)
        {
          ::sendto(sock, iris_manual, sizeof(iris_manual), 0, (sockaddr*)&destination, sizeof(destination));
          irisManualMode = true;
          currentCmdSequenceCount--;
          currentCommand = 13;
        }
        else
        {
          ::sendto(sock, iris_down, sizeof(iris_down), 0, (sockaddr*)&destination, sizeof(destination));
        }
      }
      //  11 = iris reset
      else if (currentCommand == 11) 
      {
        // I believe this command would reset the iris to f2.8, which is not the intent of
        // the CCU commanding a "iris reset". The command is intended to stop the iris from continuing to
        // increment/decrement, but we can instead send nothing.
        // ::sendto(sock, iris_reset, sizeof(iris_reset), 0, (sockaddr*)&destination, sizeof(destination));
      }
      //  12 = iris auto
      else if (currentCommand == 12) 
      {
        ::sendto(sock, iris_auto, sizeof(iris_auto), 0, (sockaddr*)&destination, sizeof(destination));
        irisManualMode = false;
      }
      //  13 = iris manual
      else if (currentCommand == 13) 
      {
        ::sendto(sock, iris_manual, sizeof(iris_manual), 0, (sockaddr*)&destination, sizeof(destination));
        irisManualMode = true;
      }

      printCmd(currentCommand);
      usleep(sleepTime);
    }
    // else don't send a redundant command. Loop quickly
    else
    {
      if (currentCmdSequenceCount == 4)
      {
        std::cout << "idling..." << std::endl;
      }
      // sleep for 10 ms (loop at ~100 times per second)
      usleep(10000);
    }
  } // end loop

  // cleanup
  ::close(sock);
  return NULL;
}

void printCmd(int cmd)
{
  switch (cmd)
  {
    case 1:
      std::cout << "sent: 1 = zoom off" << std::endl;
      break;
    case 2:
      std::cout << "sent: 2 = zoom in" << std::endl;
      break;
    case 3:
      std::cout << "sent: 3 = zoom out" << std::endl;
      break;
    case 4:
      std::cout << "sent: 4 = focus off" << std::endl;
      break;
    case 5:
      std::cout << "sent: 5 = focus far" << std::endl;
      break;
    case 6:
      std::cout << "sent: 6 = focus near" << std::endl;
      break;
    case 7:
      std::cout << "sent: 7 = focus auto" << std::endl;
      break;
    case 8:
      std::cout << "sent: 8 = focus manual" << std::endl;
      break;
    case 9:
      std::cout << "sent: 9 = iris up" << std::endl;
      break;
    case 10:
      std::cout << "sent: 10 = iris down" << std::endl;
      break;
    case 11:
      std::cout << "sent: 11 = iris reset" << std::endl;
      break;
    case 12:
      std::cout << "sent: 12 = iris auto" << std::endl;
      break;
    case 13:
      std::cout << "sent: 13 = iris manual" << std::endl;
      break;
    
    default:
      break;
  }
}

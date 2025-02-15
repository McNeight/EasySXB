/*
Copyright (c) 2023 Joe Davisson.

This file is part of EasySXB.

EasySXB is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

EasySXB is distributed in the hope that it will be useful,
state WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with EasySXB; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/

#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#ifndef WIN32
  #include <unistd.h>
  #include <string.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <fcntl.h>
  #include <termios.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>

#include "Dialog.H"
#include "Gui.H"
#include "Terminal.H"

// for Visual Studio
#if defined(_MSC_VER)
#define strcasecmp _stricmp 
#endif

namespace
{
  bool connected = false;
  int flash;
  char buf[4096];
  int buf_pos;

#ifdef WIN32
  HANDLE hserial;
  DCB dcb;
  COMMTIMEOUTS timeouts;
#else
  struct termios term;
  int fd;
  struct timeval tv;
#endif

  // store previous directory paths
  char load_dir[256];

  // extract directory from a path/filename string
  void getDirectory(char *dest, const char *src)
  {
    strncpy(dest, src, sizeof(dest));

    int len = strlen(dest);

    if (len < 2)
      return;

    for (int i = len - 1; i > 0; i--)
    {
      if (dest[i - 1] == '/')
      {
        dest[i] = '\0';
        break;
      }
    }
  }

  void fileError()
  {
    Dialog::message("Error", "Error reading file.\n");
  }

  void delay(const int chars)
  {
    const int timeout = 20;

#ifdef WIN32
    Sleep(timeout + chars);
#else
    usleep((timeout + chars) * 1000);
#endif
  }
}

char Terminal::port_string[256];

void Terminal::connect(int hardware_flow)
{
#ifdef WIN32
  // correct port name
  char buf[4096];
  sprintf(buf, "\\\\.\\%s", port_string);

  hserial = CreateFile(port_string, GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);

  if (hserial == INVALID_HANDLE_VALUE)
  {
    Dialog::message("Error", "Could not open serial port. (CreateFile)");
    return;
  }

  memset(&dcb, 0, sizeof(dcb));
  BOOL ret = GetCommState(hserial, &dcb);

  if (ret == FALSE)
  {
    Dialog::message("Error", "Could not open serial port. (GetCommState)");
    return;
  }

  dcb.DCBlength = sizeof(dcb);
  dcb.BaudRate = CBR_9600;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fOutxCtsFlow = TRUE;

  if (hardware_flow)
    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
  else
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_DISABLE;

  ret = SetCommState(hserial, &dcb);

  if (ret == FALSE)
  {
    CloseHandle(hserial);
    Dialog::message("Error", "Could not open serial port. (SetCommState)");
    return;
  }

  memset(&timeouts, 0, sizeof(timeouts));

  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 20;
  timeouts.ReadTotalTimeoutMultiplier = 1;
  timeouts.WriteTotalTimeoutConstant = 20;
  timeouts.WriteTotalTimeoutMultiplier = 1;

  ret = SetCommTimeouts(hserial, &timeouts);

  if (ret == FALSE)
  {
    CloseHandle(hserial);
    Dialog::message("Error", "Could not open serial port. (SetCommTimeouts)");
    return;
  }

  if (ret == FALSE)
  {
    CloseHandle(hserial);
    Dialog::message("Error", "Could not open serial port. (CloseHandle)");
    return;
  }
#else
  fd = open(port_string, O_RDWR | O_NOCTTY | O_NONBLOCK | O_NDELAY);

  if (fd == -1)
  {
    Dialog::message("Error", "Could not open serial port.");
    return;
  }

  memset(&term, 0, sizeof(term));

  if (hardware_flow)
    term.c_cflag = B9600 | CRTSCTS | CS8 | CREAD | CLOCAL;
  else
    term.c_cflag = B9600 | CS8 | CREAD | CLOCAL;

  term.c_iflag = IGNPAR;
  term.c_oflag = 0;
  term.c_lflag = 0;
  term.c_cc[VTIME] = 1;
  term.c_cc[VMIN] = 1;

  cfsetispeed(&term, B9600);
  cfsetospeed(&term, B9600);
  tcflush(fd, TCIFLUSH);

  int result = tcsetattr(fd, TCSANOW, &term);

  if (result == -1)
  {
    Dialog::message("Error", "tcsetattr() failed.");
    return;
  }

  tv.tv_sec = 0;
  tv.tv_usec = 100000;
#endif

  flash = 0;
  connected = true;

  Gui::append("\n>> Connected to SXB at 9600 baud.\n\n");
  Gui::append("\n");
}

void Terminal::disconnect()
{
  if (connected == true)
  {
#ifdef WIN32
    CloseHandle(hserial);
#else
    close(fd);
#endif
    connected = false;
    Gui::append("\n>> Connection Closed.\n");
    Dialog::message("Disconnected", "Connection Closed.");
  }
}

bool Terminal::isConnected()
{
  return connected;
}

void Terminal::sendChar(char c)
{
#ifdef WIN32
  DWORD bytes;

  if (connected == true)
  {
    // convert carriage return
    if (c == '\n')
      c = 13;

    WriteFile(hserial, &c, 1, &bytes, NULL);
    delay(1);
  }
#else
  if (connected == true)
  {
    // convert carriage return
    if (c == '\n')
      c = 13;

    int temp = write(fd, &c, 1);
    delay(1);
  }
#endif
}

char Terminal::getChar()
{
  char c = ' ';

#ifdef WIN32
  DWORD bytes;

  if (connected == true)
  {
    while (1)
    {
      BOOL temp = ReadFile(hserial, &c, 1, &bytes, NULL);
      delay(1);

      if (temp == 0 || bytes == 0)
        return -1;
      else
        return c;
    }
  }
#else
  if (connected == true)
  {
    while (1)
    {
      int temp = read(fd, &c, 1);
      delay(1);

      if (temp <= 0)
        return -1;
      else
        return c;
    }
  }
#endif
  return c;
}

void Terminal::sendString(const char *s)
{
  if (connected == true)
  {
    memset(buf, 0, sizeof(buf));
    strncpy(buf, s, sizeof(buf));

    for (int i = 0; i < strlen(buf); i++)
    {
      if (buf[i] == '\n')
        buf[i] = 13;
    }

#ifdef WIN32
    DWORD bytes;

    WriteFile(hserial, buf, strlen(buf), &bytes, NULL);
    delay(bytes);
#else
    int bytes = write(fd, buf, strlen(buf));
    delay(bytes);
#endif
  }
}

void Terminal::getResult(char *s)
{
  if (connected == true)
  {
    getData();

    int j = 0;

    for (int i = 0; i < strlen(buf); i++)
    {
      char c = buf[i];

      if (c >= '0' && c <= '9' || c >= 'A' && c <= 'Z' || c == ' ')
        s[j++] = c;
    }

    s[j] = '\0';
  }
}

void Terminal::getData()
{
  memset(buf, 0, sizeof(buf));
  buf_pos = 0;

#ifdef WIN32
  DWORD bytes;

  if (connected == true)
  {
    while (1)
    {
      BOOL temp = ReadFile(hserial, buf + buf_pos, 256, &bytes, NULL);

      if (bytes > 0)
        delay(bytes);

      if (temp == 0 || bytes == 0)
        break;

      buf_pos += bytes;

      if (buf_pos > 2048)
        break;
    }
  }
#else
  int bytes;

  if (connected == true)
  {
    while (1)
    {
      bytes = read(fd, buf + buf_pos, 256);

      if (bytes > 0)
        delay(bytes);

      if (bytes <= 0)
        break;

      buf_pos += bytes;

      if (buf_pos > 2048)
        break;
    }
  }
#endif

  for (int i = 0; i < sizeof(buf); i++)
    if (buf[i] == 13)
      buf[i] = '\n';
}

void Terminal::receive(void *data)
{
  getData();
  Gui::append(buf);

  // cause cursor to flash
  flash++;

  if (flash > 63)
    flash = 0;

  Gui::flashCursor((((flash >> 2) & 1) == 1) ? true : false);

  Fl::repeat_timeout(.10, Terminal::receive, data);
}

void Terminal::changeReg(int reg, int num)
{
  if (connected == false)
    return;

  if (num < 0)
    num = 0;

  char s[256];

  if (Gui::getMode() == Gui::MODE_265)
  {
    switch (reg)
    {
      case REG_PC:
        sprintf(s, "|P%02X:%04X", num >> 16, num & 0xFFFF);
        sendString(s);
        break;
      case REG_A:
        sprintf(s, "|A%04X", num);
        sendString(s);
        break;
      case REG_X:
        sprintf(s, "|X%04X", num);
        sendString(s);
        break;
      case REG_Y:
        sprintf(s, "|Y%04X", num);
        sendString(s);
        break;
      case REG_SP:
        sprintf(s, "|S%04X", num);
        sendString(s);
        break;
      case REG_DP:
        sprintf(s, "|D%04X", num);
        sendString(s);
        break;
      case REG_SR:
        sprintf(s, "|F%02X", num);
        sendString(s);
        break;
      case REG_DB:
        sprintf(s, "|B%02X", num);
        sendString(s);
        break;
    }

    sendString("R");

    if (reg == REG_SR)
      Gui::setToggles(num);
  }
  else if (Gui::getMode() == Gui::MODE_134)
  {
    switch (reg)
    {
      case REG_PC:
        sprintf(s, "A%04X     ", num & 0xFFFF);
        sendString(s);
        break;
      case REG_SR:
        sprintf(s, "A %02X    ", num & 0xFF);
        sendString(s);
        break;
      case REG_A:
        sprintf(s, "A  %02X   ", num);
        sendString(s);
        break;
      case REG_X:
        sprintf(s, "A   %02X  ", num);
        sendString(s);
        break;
      case REG_Y:
        sprintf(s, "A    %02X ", num);
        sendString(s);
        break;
      case REG_SP:
        sprintf(s, "A     %02X", num);
        sendString(s);
        break;
    }

    sendString("R");

    if (reg == REG_SR)
      Gui::setToggles(num);
  }
}

void Terminal::updateRegs()
{
  if (connected == false)
    return;

  char s[256];
  memset(s, 0, sizeof(s));

  if (Gui::getMode() == Gui::MODE_265)
  {
    sendString("| ");
    getResult(s);
    Gui::updateRegs(s);
  }
  else if (Gui::getMode() == Gui::MODE_134)
  {
    sendString("R");
    getResult(s);
    Gui::updateRegs(s);
  }
}

void Terminal::jml(int address)
{
  if (connected == false)
    return;

  if (address < 0)
    address = 0;

  char s[256];

  if (Gui::getMode() == Gui::MODE_265)
  {
    sendString("G");
    sprintf(s, "%02X%04X", address >> 16, address & 0xFFFF);
    sendString(s);
  }
  else if (Gui::getMode() == Gui::MODE_134)
  {
    sendString("G");
    sprintf(s, "%04X", address & 0xFFFF);
    sendString(s);
  }
}

void Terminal::jsl(int address)
{
  if (connected == false)
    return;

  if (address < 0)
    address = 0;

  char s[256];

  if (Gui::getMode() == Gui::MODE_265)
  {
    sendString("J");
    sprintf(s, "%02X%04X", address >> 16, address & 0xFFFF);
    sendString(s);
  }
  else if (Gui::getMode() == Gui::MODE_134)
  {
    sendString("J");
    sprintf(s, "%04X", address & 0xFFFF);
    sendString(s);
  }
}

void Terminal::dump(int address)
{
  if (connected == false)
    return;

  if (address < 0)
    address = 0;

  char s[256];

  sprintf(s, "\nMemory dump from %02X:%04X - %02X:%04X\n",
          address >> 16, address & 0xFFFF,
          (address + 0xff) >> 16, (address + 0xff) & 0xFFFF);
  Gui::append(s);

  if (Gui::getMode() == Gui::MODE_265)
  {
    sendString("D");
    sprintf(s, "%02X%04X", address >> 16, address & 0xFFFF);
    sendString(s);
    sprintf(s, "%02X%04X\n", (address + 0xff) >> 16, (address + 0xff) & 0xFFFF);
    sendString(s);
  }
  else if (Gui::getMode() == Gui::MODE_134)
  {
    sendString("D");
    sprintf(s, "%04X%04X", address & 0xFFFF, (address + 0xff) & 0xffff);
    sendString(s);
  }
}

void Terminal::upload()
{
  if (connected == false)
  {
    Dialog::message("Error", "Not Connected.");
    return;
  }

  Fl_Native_File_Chooser fc;
  fc.title("Upload Program");
  fc.filter("HEX File\t*.hex\nSREC File\t*.srec\n");
  fc.options(Fl_Native_File_Chooser::PREVIEW);
  fc.type(Fl_Native_File_Chooser::BROWSE_FILE);
  fc.directory(load_dir);

  switch (fc.show())
  {
    case -1:
    case 1:
      return;
    default:
      getDirectory(load_dir, fc.filename());
      break;
  }

  const char *ext = fl_filename_ext(fc.filename());

  if (strcasecmp(ext, ".hex") == 0)
  {
    Terminal::uploadHex(fc.filename());
  }
  else if (strcasecmp(ext, ".srec") == 0)
  {
    Terminal::uploadSrec(fc.filename());
  }
    else
  {
    Dialog::message("Upload Error",
                    "Only .hex and .srec file extentions are supported.");
  }
}

void Terminal::uploadHex(const char *filename)
{
  int segment = 0;
  int address = 0;
  int start_address = -1;
  int code = 0;
  int value = 0;
  int count = 0;
  int temp;
  int ret;
  char s[256];

  FILE *fp = fopen(filename, "r");

  if (fp == NULL)
  {
    Dialog::message("Error", "Could not open file.\n");
    return;
  }

  Gui::append("\n>> Uploading Program, ESC to cancel.\n");

  while (1)
  {
    temp = fgetc(fp);

    if (temp == EOF)
      break; 

    // start of line
    if (temp == ':')
    {
      segment = 0;
      ret = fscanf(fp, "%02X", &count);

      if (ret == -1 || ret == EOF)
      {
        fileError();
        break;
      }

      // last line
      if (count == 0)
      {
        break;
      }
        else
      {
        ret = fscanf(fp, "%04X", &address);

        if (ret == -1 || ret == EOF)
        {
          fileError();
          break;
        }

        if (start_address == -1)
          start_address = address;

        ret = fscanf(fp, "%02X", &code);

        if (ret == -1 || ret == EOF)
        {
          fileError();
          break;
        }

        if (code == 0x04)
        {
          segment = address;
        }
        else if (code == 0x00)
        {
          int checksum = 0;

          // address
          sprintf(s, "S2%02X%02X%02X%02X", count + 4,
                  segment, (address >> 8) & 0xFF, address & 0xFF);
          checksum += count + 4;
          checksum += address >> 8;
          checksum += address & 0xFF;

          // data
          int index = 10;
          bool cancel = false;

          for (int i = 0; i < count; i++)
          {
            ret = fscanf(fp, "%02X", &value);

            if (ret == -1 || ret == EOF)
            {
              cancel = true;
              break;
            }

            sprintf(s + index, "%02X", value);
            index += 2;
            checksum += value;
          }

          if (cancel == true)
          {
            fileError();
            break;
          }

          // checksum
          sprintf(s + index, "%02X\n", 0xFF - (checksum & 0xFF));
          sendString(s);

          // update terminal
          getData();
          Gui::append(buf);
        }
      }

      // skip to next line
      for (int i = 0; i < 256; i++)
      {
        temp = fgetc(fp);

        if (temp == '\n')
          break;
      }

      // cancel operation with escape key
      Fl::check();

      if (Gui::getCancelled() == true)
      {
        Gui::setCancelled(false);
        break;
      }
    }
  }

  sprintf(s, "S804000000FB\n");
  sendString(s);

  fclose(fp);
  Gui::setAddress(start_address);
}

void Terminal::uploadSrec(const char *filename)
{
  int address = 0;
  int start_address = -1;
  int code = 0;
  int value = 0;
  int count = 0;
  int temp;
  int ret;
  char prefix[8];
  char s[256];

  FILE *fp = fopen(filename, "r");

  if (fp == NULL)
  {
    Dialog::message("Error", "Could not open file.\n");
    return;
  }

  Gui::append("\n>> Uploading Program, ESC to cancel.\n");

  while (1)
  {
    // get code from prefix
    prefix[0] = fgetc(fp);

    if (prefix[0] == EOF)
      break;

    prefix[1] = fgetc(fp);

    if (prefix[1] == EOF)
      break;

    code = prefix[1] - '0';

    if (code < 0 || code > 2)
      break;

    ret = fscanf(fp, "%02X", &count);

    if (ret == -1 || ret == EOF)
    {
      fileError();
      break;
    }

    if (code == 1)
      count -= 3;
    else if (code == 2)
      count -= 4;

    // last line
    if (count == 0)
    {
      break;
    }
    else if (code > 0)
    {
      if (code == 1)
      {
        ret = fscanf(fp, "%04X", &address);

        if (ret == -1 || ret == EOF)
        {
          fileError();
          break;
        }

        if (start_address == -1)
          start_address = address;
      }
      else if (code == 2)
      {
        ret = fscanf(fp, "%06X", &address);

        if (ret == -1 || ret == EOF)
        {
          fileError();
          break;
        }

        if (start_address == -1)
          start_address = address;
      }
        else
      {
        break;
      }

      int checksum = 0;

      // address
      sprintf(s, "S2%02X%02X%02X%02X", count + 4,
              (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF);
      checksum += count + 4;
      checksum += address >> 8;
      checksum += address & 0xFF;

      // data
      int index = 10;

      for (int i = 0; i < count; i++)
      {
        ret = fscanf(fp, "%02X", &value);

        if (ret == -1 || ret == EOF)
        {
          fileError();
          break;
        }

        sprintf(s + index, "%02X", value);
        index += 2;
        checksum += value;
      }

      // checksum
      sprintf(s + index, "%02X\n", 0xFF - (checksum & 0xFF));
      sendString(s);

      // update terminal
      getData();
      Gui::append(buf);
    }

    // skip to next line
    for (int i = 0; i < 256; i++)
    {
      temp = fgetc(fp);

      if (temp == '\n')
        break;
    }

    // cancel operation with escape key
    Fl::check();

    if (Gui::getCancelled() == true)
    {
      Gui::setCancelled(false);
      break;
    }
  }

  sprintf(s, "S804000000FB\n");
  sendString(s);

  fclose(fp);
  Gui::setAddress(start_address);
}


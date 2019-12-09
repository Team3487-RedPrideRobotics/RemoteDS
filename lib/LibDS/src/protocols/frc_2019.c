/*
 * The Driver Station Library (LibDS)
 * Copyright (c) 2015-2017 Alex Spataru <alex_spataru@outlook>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

 /* LibDS updated for 2019 FRC protocol by Riviera Robotics <team@rivierarobotics.org> */

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "DS_Utils.h"
#include "DS_Config.h"
#include "DS_Protocol.h"
#include "DS_Joysticks.h"
#include "DS_DefaultProtocols.h"

#if defined _WIN32
    #include <windows.h>
#endif

/*
 * Protocol bytes
 */
 //TODO make sure these correct (wireshark observations)
static const uint8_t cTest               = 0x01; //pos 4 udp1110 - 1
static const uint8_t cEnabled            = 0x04; //pos 4 replaced udp1110 - 4
static const uint8_t cAutonomous         = 0x02; //pos 4 udp1110 - 2
static const uint8_t cTeleoperated       = 0x00; //pos 4 udp1110 - 0
static const uint8_t cEmergencyStop      = 0x80; //pos 4 replaced udp1110 - 128
static const uint8_t cRequestReboot      = 0x08; //
static const uint8_t cRequestNormal      = 0x30; //pos 5 udp1110 - 48
static const uint8_t cRequestUnconnected = 0x00; //
static const uint8_t cRequestRestartCode = 0x10; //pos 5 udp1110 - 16
static const uint8_t cTagDate            = 0x0f; //
static const uint8_t cTagGeneral         = 0x01; //pos 3 udp1110 - 1
static const uint8_t cTagJoystick        = 0x0c; //
static const uint8_t cTagTimezone        = 0x10; //
static const uint8_t cRed1               = 0x00; //pos 6 udp1110 - 0
static const uint8_t cRed2               = 0x01; //pos 6 udp1110 - 1
static const uint8_t cRed3               = 0x02; //pos 6 udp1110 - 2
static const uint8_t cBlue1              = 0x03; //pos 6 udp1110 - 3
static const uint8_t cBlue2              = 0x04; //pos 6 udp1110 - 4
static const uint8_t cBlue3              = 0x05; //pos 6 udp1110 - 5
static const uint8_t cRequestTime        = 0x01; //
static const uint8_t cRobotHasCode       = 0x20; //

/*
 * Sent robot packet counters
 */
static unsigned int send_time_data = 0;
static unsigned int sent_robot_packets = 0;

/*
 * Control code flags
 */
static int reboot = 0;
static int restart_code = 0;

//----------------------------------------------------------------------------//
// Recomendations for the devs who WILL implement this:                       //
//   - To reverse engineer the protocol, launch Wireshark and see what the    //
//     FRC DS sends and receives from the robot under different conditions,   //
//     take note of the port info and timings of each event to know how the   //
//     RX/TX bytes changed.                                                   //
//   - Update the target addresses/IPs, I have used the ones defined by the   //
//     FRC 2016 protocol, but things may have changed over these two years    //
//   - Check the functions of DS_String.h, LibDS passes byte arrays (strings) //
//     using the structures and functions defined in that header/module.      //
//----------------------------------------------------------------------------//

//----------------------------------------------------------------------------//
// Network address section, someone please verify these functions             //
//----------------------------------------------------------------------------//

/**
 * The FMS address is not defined, it will be assigned automatically when the
 * DS receives a FMS packet
 */
static DS_String fms_address (void)
{
    return DS_StrNew (DS_FallBackAddress);
}

/**
 * The 2015+ control system assigns the radio IP in 10.te.am.1
 */
static DS_String radio_address (void)
{
    return DS_GetStaticIP (10, CFG_GetTeamNumber(), 1);
}

/**
 * The 2016+ control system assigns the robot address at roboRIO-TEAM-frc.local via mDNS
 * Using 10.TE.AM.2 for now as it doesn't rely on the mDNS working
 */
static DS_String robot_address (void)
{
    return DS_GetStaticIP (10, CFG, GetTeamNumber(), 2);
    //return DS_StrFormat ("roboRIO-%d-frc.local", CFG_GetTeamNumber());
}

/**
 * Returns the control code sent to the robot, it contains:
 *    - The control mode of the robot (teleop, autonomous, test)
 *    - The enabled state of the robot
 *    - The FMS attached keyword
 *    - The operation state (e-stop, normal)
 */
static uint8_t get_control_code (void)
{
    uint8_t code = 0;

    /* Get current control mode (Test, Auto or Teleop) */
    switch (CFG_GetControlMode()) {
      case DS_CONTROL_TEST: code |= cTest; break;
      case DS_CONTROL_AUTONOMOUS: code |= cAutonomous; break;
      case DS_CONTROL_TELEOPERATED: code |= cTeleoperated; break;
      default: break;
    }

    /* Let the robot know if it should e-stop right now */
    if (CFG_GetEmergencyStopped())
        code |= cEmergencyStop;

    /* Append the robot enabled state */
    if (CFG_GetRobotEnabled())
        code |= cEnabled;

    return code;
}

/**
 * Generates the request code sent to the robot, which may instruct it to:
 *    - Operate normally
 *    - Reboot the roboRIO
 *    - Restart the robot code process
 */
static uint8_t get_request_code (void)
{
    uint8_t code = cRequestNormal;

    /* Robot has comms, check if we need to send additional flags */
    if (CFG_GetRobotCommunications()) {
      if (reboot) {
        code = cRequestReboot;
      } else if (restart_code) {
        code = cRequestRestartCode;
      }
    } else {
      code = cRequestUnconnected;
    }

    return code;
}

/**
 * Returns the team station code sent to the robot.
 * This value may be used by the robot program to use specialized autonomous
 * modes or adjust sensor input.
 */
static uint8_t get_station_code (void)
{
    if(CFG_GetAlliance() == DS_ALLIANCE_RED) {
      switch(CFG_GetPosition()) {
        case DS_POSITION_1: return cRed1; break;
        case DS_POSITION_2: return cRed2; break;
        case DS_POSITION_3: return cRed3; break;
        default: break;
      }
    } else {
      switch(CFG_GetPosition()) {
        case DS_POSITION_1: return cBlue1; break;
        case DS_POSITION_2: return cBlue2; break;
        case DS_POSITION_3: return cBlue3; break;
        default: break;
      }
    }

    return cRed1;
}

/**
 * Returns information regarding the current date and time and the timezone
 * of the client computer.
 *
 * The robot may ask for this information in some cases (e.g. when initializing
 * the robot code).
 */
static DS_String get_timezone_data (void)
{
    DS_String data = DS_StrNewLen (14);

    /* Get current time */
    time_t rt = 0;
    uint32_t ms = 0;
    struct tm timeinfo;

#if defined _WIN32
    localtime_s (&timeinfo, &rt);
#else
    localtime_r (&rt, &timeinfo);
#endif

#if defined _WIN32
    /* Get timezone information */
    TIME_ZONE_INFORMATION info;
    GetTimeZoneInformation (&info);

    /* Convert the wchar to a standard string */
    size_t len = wcslen (info.StandardName) + 1;
    char* str = calloc (len, sizeof (char));
    wcstombs_s (NULL, str, len, info.StandardName, wcslen (info.StandardName));

    /* Convert the obtained cstring to a bstring */
    DS_String tz = DS_StrNew (str);
    free (str);

    /* Get milliseconds */
    GetSystemTime (&info.StandardDate);
    ms = (uint32_t) info.StandardDate.wMilliseconds;
#else
    /* Timezone is stored directly in time_t structure */
    DS_String tz = DS_StrNew (timeinfo.tm_zone);
#endif

    /* Encode date/time in datagram */
    DS_StrSetChar (&data, 0,  (uint8_t) 0x0b);
    DS_StrSetChar (&data, 1,  (uint8_t) cTagDate);
    DS_StrSetChar (&data, 2,  (uint8_t) (ms >> 24));
    DS_StrSetChar (&data, 3,  (uint8_t) (ms >> 16));
    DS_StrSetChar (&data, 4,  (uint8_t) (ms >> 8));
    DS_StrSetChar (&data, 5,  (uint8_t) (ms));
    DS_StrSetChar (&data, 6,  (uint8_t) timeinfo.tm_sec);
    DS_StrSetChar (&data, 7,  (uint8_t) timeinfo.tm_min);
    DS_StrSetChar (&data, 8,  (uint8_t) timeinfo.tm_hour);
    DS_StrSetChar (&data, 9,  (uint8_t) timeinfo.tm_yday);
    DS_StrSetChar (&data, 10, (uint8_t) timeinfo.tm_mon);
    DS_StrSetChar (&data, 11, (uint8_t) timeinfo.tm_year);

    /* Add timezone length and tag */
    DS_StrSetChar (&data, 12, DS_StrLen (&tz));
    DS_StrSetChar (&data, 13, cTagTimezone);

    /* Add timezone string */
    DS_StrJoin (&data, &tz);

    /* Return the obtained data */
    return data;
}

//----------------------------------------------------------------------------//
// DS to Robot/Radio/FMS packet generation                                    //
//----------------------------------------------------------------------------//

/* Ignore all FMS for now */
static DS_String create_fms_packet (void)
{
    /* Return empty (0-length) string */
    return DS_StrNewLen (0);
}

/* Radio does not interact with DS directly - create blank radio packets */
static DS_String create_radio_packet (void)
{
    return DS_StrNewLen (0);
}

//TODO rework this method based on wireshark observations (this is 2015, look at 2014 for reference)
static DS_String create_robot_packet (void)
{
    DS_String data = DS_StrNewLen (6);

    /* Add packet index */
    DS_StrSetChar (&data, 0, (sent_robot_packets >> 8));
    DS_StrSetChar (&data, 1, (sent_robot_packets));

    /* Add packet header */
    DS_StrSetChar (&data, 2, cTagGeneral);

    /* Add control code, request flags and team station */
    DS_StrSetChar (&data, 3, get_control_code());
    DS_StrSetChar (&data, 4, get_request_code());
    DS_StrSetChar (&data, 5, get_station_code());

    /* Add timezone data (if robot wants it) */
    if (send_time_data) {
        DS_String tz = get_timezone_data();
        DS_StrJoin (&data, &tz);
    }

    /* Add joystick data */
    else if (sent_robot_packets > 5) {
        DS_String js = get_joystick_data();
        DS_StrJoin (&data, &js);
    }

    /* Increase robot packet counter */
    ++sent_robot_packets;

    return data;
}

//----------------------------------------------------------------------------//
// Robot/Radio/FMS to DS packet reading & interpretation                      //
//----------------------------------------------------------------------------//

/* Ignore FMS for now */
static int read_fms_packet (const DS_String* data)
{
    return 0;
}

/* Radio does not interact with DS directly - ignore radio packets */
static int read_radio_packet (const DS_String* data)
{
    return 0;
}

static int read_robot_packet (const DS_String* data)
{
    /* Check if data pointer is valid */
    if (!data) {
      return 0;
    }

    /* Check that packet is not undersized */
    if (DS_StrLen(data) < 7) {
      return 0;
    }

    /* Read robot packet */
    uint8_t control = (uint8_t) DS_StrCharAt (data, 3);
    uint8_t rstatus = (uint8_t) DS_StrCharAt (data, 4);
    uint8_t request = (uint8_t) DS_StrCharAt (data, 7);

    /* Update client information */
    CFG_SetRobotCode (rstatus & cRobotHasCode);
    CFG_SetEmergencyStopped (control & cEmergencyStop);

    /* Update date/time request flag */
    send_time_data = (request == cRequestTime);

    /* Calculate the voltage */
    uint8_t upper = (uint8_t) DS_StrCharAt (data, 5);
    uint8_t lower = (uint8_t) DS_StrCharAt (data, 6);
    CFG_SetRobotVoltage (decode_voltage (upper, lower));

    /* Packet read, feed watchdog */
    return 1;
}

//----------------------------------------------------------------------------//
// The implementation of these functions is up to you, and how the protocol   //
// works...                                                                   //
// In previous protocol implementations, these functions just changed the     //
// value of global variables that were used by the packet generation          //
// functions.                                                                 //
//----------------------------------------------------------------------------//

/** FMS watchdog expires - do nothing **/
static void reset_fms (void) {}

/** Radio watchdog expires - do nothing **/
static void reset_radio (void) {}

static void reset_robot (void) {
	reboot = 0;
  restart_code = 0;
  send_time_data = 0;
}

static void reboot_robot (void) {
	reboot = 1;
}

static void restart_robot_code (void) {
	restart_code = 1;
}

//----------------------------------------------------------------------------//
// Finally, to use the protocol with the rest of LibDS, create a DS_Protocol  //
// structure, which contains pointers to the functions implemented above and  //
// other properties, such as:                                                 //
//   - Maximum joystick count, and individual joystick button and axis limits //
//   - DS to Robot/Radio/FMS packet sending intervals (in milliseconds)       //
//   - Network socket properties for robot, radio and FMS                     //
//   - Input/output network ports for robot, radio and FMS                    //
//----------------------------------------------------------------------------//

/**
 * Initializes and configures the FRC 2019 communication protocol
 */
DS_Protocol DS_GetProtocolFRC_2019 (void)
{
    /* Initialize pointers */
    DS_Protocol protocol;

    /* Set protocol name */
    protocol.name = DS_StrNew ("FRC 2019");

    /* Set address functions */
    protocol.fms_address = &fms_address;
    protocol.radio_address = &radio_address;
    protocol.robot_address = &robot_address;

    /* Set packet generator functions */
    protocol.create_fms_packet = &create_fms_packet;
    protocol.create_radio_packet = &create_radio_packet;
    protocol.create_robot_packet = &create_robot_packet;

    /* Set packet interpretation functions */
    protocol.read_fms_packet = &read_fms_packet;
    protocol.read_radio_packet = &read_radio_packet;
    protocol.read_robot_packet = &read_robot_packet;

    /* Set reset functions */
    protocol.reset_fms = &reset_fms;
    protocol.reset_radio = &reset_radio;
    protocol.reset_robot = &reset_robot;

    /* Set misc. functions */
    protocol.max_battery_voltage = 13;
    protocol.reboot_robot = &reboot_robot;
    protocol.restart_robot_code = &restart_robot_code;

    /* Set packet intervals */
    protocol.fms_interval = 500;
    protocol.radio_interval = 0;
    protocol.robot_interval = 20;

    /* Set joystick properties */
    protocol.max_hat_count = 0;
    protocol.max_axis_count = 0;
    protocol.max_joysticks = 0;
    protocol.max_button_count = 0;

    /* Define FMS socket properties */
    protocol.fms_socket = *DS_SocketEmpty();
    protocol.fms_socket.disabled = 1;

    /* Define radio socket properties */
    protocol.radio_socket = *DS_SocketEmpty();
    protocol.radio_socket.disabled = 1;

    /* Define robot socket properties */
    protocol.robot_socket = *DS_SocketEmpty();
    protocol.robot_socket.disabled = 0;
    protocol.robot_socket.in_port = 1150;
    protocol.robot_socket.out_port = 1110;
    protocol.robot_socket.type = DS_SOCKET_UDP;

    /* Define netconsole socket properties */
    protocol.netconsole_socket = *DS_SocketEmpty();
    protocol.netconsole_socket.disabled = 1;

    /* Return the pointer */
    return protocol;
}

/*
  ASCOM Canon EF Lens Controller by ASTROMECHANICS

Protocol description
  User level software communicates with ASCOM Lens Controller via a simple commands transfer protocol.
  Commands transmit as a specially formed text units.
  You can use this protocol autonomously or embed it in your software products.
  You can use a development environment that allows you to interact with COM ports.

  Now you can try to type some commands. All commands terminate by # symbol.
  List of available commands and their syntax is shown in the table.

  1.P#
    Get current focus position (P# 4800#)

  2.Mxxxx#
    Move the focus position to xxxx absolute value (M5200#).
    This command doesn't send a reply.

  3.Axx#
    Set the aperture value. NOTE: xx value is a difference between the aperture value you want and maximum aperture value (xx=0) available for your lens.
    For example: f1.8 -> f2.8 with Canon EF 50 mm f1.8 - A04#. A00# return aperture value to f/1.8.
    This is easy to understand if you look at AV series for your lens.
    Canon EF 50 mm f/1.8 | 1.8 2.0 2.2 2.5 2.8 3.2 3.5 4.0 4.5 5.0 5.6 6.3 7.1 8 9 10 11 13 14 16 18 20 22
    This command doesn't send a reply.


	M5Stack version

	This program was created by Bergamot for the M5Stack.
	and It requires an HSB host module in addition to the M5Stack CPU module.

	Copyright (C) 2022 by bergamot-jellybeans.

	Date-written.	Jan 06,2022.
	Last-modify.	Feb 17,2022.
	mailto:			bergamot.jellybeans@icloud.com

  Revision history
  Feb 16,2022
    Add battery indicator of one second interval.
  Feb 17,2022
    Button "UP" and Button "DOWN" are swapped.
    
*/

// ----------------------------------------------------------------------------------------------------------
// FIRMWARE CODE START
#include <M5Stack.h>
#include "stringQueue.h"	//  By Steven de Salas
#include <cdcftdi.h>
#include <usbhub.h>
#include "IniFiles.h"
#include "ButtonEx.h"

int baud = 38400;   // for ASCOM Canon EF Lens Controller

// Satisfy the IDE, which needs to see the include statment in the ino too.
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>

// FTDI Async class
class FTDIAsync : public FTDIAsyncOper {
  public:
    bool flagOnInit;
    uint8_t OnInit( FTDI *pftdi );
};

uint8_t FTDIAsync::OnInit( FTDI *pftdi ) {
  uint8_t rcode = 0;

  rcode = pftdi->SetBaudRate( baud );  // for ASCOM Canon EF Lens Controller

  if ( rcode ) {
    Serial.printf( "rode=%d\n", rcode );
    ErrorMessage<uint8_t>( PSTR( "SetBaudRate" ), rcode );
    return rcode;
  }
  rcode = pftdi->SetFlowControl( FTDI_SIO_DISABLE_FLOW_CTRL );

  if (rcode){
    Serial.printf( "rode=%d\n", rcode );
    ErrorMessage<uint8_t>( PSTR( "SetFlowControl" ), rcode );
  }

  Serial.println( "OnInit" );
  flagOnInit = true;
  return rcode;
}

// USB settings
USB              Usb;
FTDIAsync        FtdiAsync;
FTDI             Ftdi( &Usb, &FtdiAsync );

#define MYINIFILENAME       "/canonLens.ini"
#define LENSINFOFILENAME    "/Lens.txt"
#define MAX_LENS        15			// number of lens list
#define MAX_APERTURE    32			// number of aperture list
#define QUEUELENGTH     10      // number of commands that can be saved in the serial queue
#define RECVLINES       32

// State machine phase
#define PHASE_WAIT_CONNECT  0   // Waiting for the lens controller to be connected.
#define PHASE_LENS          1   // Lens selection in progress.
#define PHASE_APERTURE      2   // Aperture selection in progress.
#define PHASE_FOCUS         3   // Adjusting the focus position of the lens.

#define BATTERYUPDATETIMEMS 1000
#define RGB(r,g,b) (int16_t)( b + (g << 5 ) + ( r << 11 ) )

typedef struct {
  String lines;
  String lensName;
  int numberOfAperture;
  String aperture[MAX_APERTURE];
} lensInfo_t;

typedef struct {
  int lensIndex;
  int apertureIndex;
  int focusPosition;
} systemParameter_t;

unsigned long batteryUpdateTime;
int lastBatteryLevel;
int numberOfLens;
int phase;
systemParameter_t systemParam;
lensInfo_t *selectlensInfo;
StringQueue queue( QUEUELENGTH );       // receive serial queue of commands
int recvLineIndex;
char recvLine[RECVLINES];

lensInfo_t lensInfo[MAX_LENS];
ButtonEx* buttonScan;
LabelEx* labelStatus;
LabelEx* labelLensNameTitle;
LabelEx* labelApertureTitle;
LabelEx* labelFocusTitle;
LabelEx* labelLensName;
LabelEx* labelAperture;
LabelEx* labelFocus;
LabelEx* labelBtnA;
LabelEx* labelBtnB;
LabelEx* labelBtnC;

// ---------------------------------------------------------------------------------------------------------
// DO NOT CHANGE
//#define DEBUG     1
//#define DEBUGHPSW 1

// ---------------------------------------------------------------------------------------------------------
// CODE START

void software_Reboot()
{
//  asm volatile ( "jmp 0");        // jump to the start of the program
}

// Load the lens information list from the micro SD card.
bool readLensInfoFile( void )
{
  IniFiles ini( MAX_LENS );
  bool validFile = ini.open( SD, LENSINFOFILENAME );

  Serial.printf( "Open : %s %d\n", LENSINFOFILENAME, validFile );

  numberOfLens = 0;
  for ( int i = 0; i < MAX_LENS; i++ ) {
    int s1;
    String key = "lens" + String( i+1 );
    String lines = ini.readString( key, "" );
    lensInfo_t *lensp = &lensInfo[i];
    s1 = lines.indexOf( '|' );  // Find the separator between the lens name and the aperture string.
//    Serial.printf( "Line(%s) : %s\n", key.c_str(), lines.c_str() );

    // Parse the lens name and aperture string.
    if ( s1 > 0 ) {
      String aLines = lines.substring( 0, s1 ); // cut a lens name of lines.
      lines.remove( 0, s1 + 1 ); // remove a lens name of lines.
      aLines.trim();
      lensp->lensName = aLines;
      lines.trim();
      numberOfLens++;
      Serial.printf( "LensName%d = %s\n", i, lensp->lensName.c_str() );
      Serial.printf( "Aperture%d = %s\n", i, lines.c_str() );

      // Aperture
      int n = 0;
      lensp->numberOfAperture = 0;
      // Parse an aperture string.
      while ( lines.length() > 0 ) {
        lensp->numberOfAperture++;
        s1 = lines.indexOf( ' ' );  // Find the separator in the aperture string.
        if ( s1 > 0 ) {
          lensp->aperture[n++] = lines.substring( 0, s1 );
          lines.remove( 0, s1 + 1 ); // remove a aperture of lines.
        } else {
          lensp->aperture[n] = lines;
          break;          
        }
      }
      Serial.printf( "numberOfAperture%d = %d\n", i, lensp->numberOfAperture );
    }
  }

  ini.close( SD );
  return true;
}

// Display lens name on the labelLensName.
void lensSelect( void )
{
  selectlensInfo = &lensInfo[systemParam.lensIndex];
  int clFill = ( phase == PHASE_LENS ) ? TFT_BLUE : TFT_BLACK;
  labelLensName->frameRect( TFT_WHITE, clFill, 4 );
  labelLensName->caption( TFT_WHITE, selectlensInfo->lensName );
}

// Set the lens to the index of the argument <sel>
void lensSelect( int sel )
{
  systemParam.lensIndex = sel;
  lensSelect();
}

// Move the lens list to the next index.
void lensSelectNext( void )
{
  int nsel = systemParam.lensIndex + 1;
  if ( nsel >= numberOfLens ) {
    nsel = 0;
  }
  lensSelect( nsel );
}

// Move the lens list to the previous index.
void lensSelectPrev( void )
{
  int nsel = systemParam.lensIndex - 1;
  if ( nsel < 0 ) {
    nsel = numberOfLens - 1;
  }
  lensSelect( nsel );
}

// Display aperture name on the labelAperture.
void apertureSelect( void )
{
  int clFill = ( phase == PHASE_APERTURE ) ? TFT_RED : TFT_BLACK;
  labelAperture->frameRect( TFT_WHITE, clFill, 4 );
  labelAperture->caption( TFT_WHITE, selectlensInfo->aperture[systemParam.apertureIndex] );
}

// Set the aperture to the index of the argument <sel>
void apertureSelect( int sel )
{
  systemParam.apertureIndex = sel;
  apertureSelect();
  setApertureValue( sel );
}

// Move the aperture to the next index.
void apertureSelectNext( void )
{
  int nsel = systemParam.apertureIndex + 1;
  if ( nsel >= selectlensInfo->numberOfAperture ) {
    nsel = 0;
  }
  apertureSelect( nsel );
}

// Move the aperture to the previous index.
void apertureSelectPrev( void )
{
  int nsel = systemParam.apertureIndex - 1;
  if ( nsel < 0 ) {
    nsel = selectlensInfo->numberOfAperture - 1;
  }
  apertureSelect( nsel );
}

// Display focus position on the labelFocus.
void focusPosition( void )
{
  int clFill = ( phase == PHASE_FOCUS ) ? TFT_RED : TFT_BLACK;
  labelFocus->frameRect( TFT_WHITE, clFill, 4 );
  labelFocus->caption( TFT_WHITE, "%d", systemParam.focusPosition );
}

// Set the focus to the position of the argument <sel>
void focusPosition( int sel )
{
  systemParam.focusPosition = sel;
  focusPosition();
  setFocusPosition(sel );
}

// Move the focus position of the lens by the incremental argument <incstep>.
void focusPositionIncrease( int incstep )
{
  int nsel = systemParam.focusPosition + incstep;
  focusPosition( nsel );
}

// Send aperture setting commands to the lens controller.
uint8_t setApertureValue( int index )
{
  uint8_t  rcode;
  char buff[16];

  sprintf( buff, "A%02d#", index );  
  Serial.printf( ">%s\n", buff );
  rcode = Ftdi.SndData( strlen( buff ), (uint8_t*)buff );
  return rcode;
}

// Send focus position setting commands to the lens controller.
uint8_t setFocusPosition( int position )
{
  uint8_t  rcode;
  char buff[16];

  sprintf( buff, "M%d#", position );  
  Serial.printf( ">%s\n", buff );
  rcode = Ftdi.SndData( strlen( buff ), (uint8_t*)buff );
  return rcode;
}

// Save the system settings to the micro SD card.
bool writeSystemFile( void )
{
  IniFiles ini( 16 );
  bool validFile = ini.open( SD, MYINIFILENAME );
  ini.writeInteger( "LensIndex", systemParam.lensIndex );
//  ini.writeInteger( "ApertureIndex", systemParam.apertureIndex );
  ini.close( SD );
  return validFile;
}

// Load the system settings from the micro SD card.
bool readSystemFile( void )
{
  IniFiles ini( 16 );
  bool validFile = ini.open( SD, MYINIFILENAME );
  systemParam.lensIndex = ini.readInteger( "LensIndex", 0 );
  systemParam.apertureIndex = ini.readInteger( "ApertureIndex", 0 );
  ini.close( SD );

  return validFile;
}

// Get the battery information of the M5Stack.
int8_t getBatteryLevel( void )
{
  Wire.beginTransmission( 0x75 ); // start I2C transmission.
  Wire.write( 0x78 ); // slave address.
  if ( Wire.endTransmission( false ) == 0 && Wire.requestFrom( 0x75, 1 ) ) {
    switch ( Wire.read() & 0xF0 ) {
    case 0xE0: return 25;   // battery level is about 25%.
    case 0xC0: return 50;   // battery level is about 50%.
    case 0x80: return 75;   // battery level is about 75%.
    case 0x00: return 100;  // battery level is full.
    default: return 0;      // battery level is empty.
    }
  }
  return -1;  // Battery information could not be obtained.
}

// Setup
/*************************************************************************
 * NAME  setup - 
 * SYNOPSIS
 *
 *    void setup( void )
 *
 * DESCRIPTION
 *  
 *************************************************************************/
void setup()
{
  // initialize the M5Stack object
  M5.begin( true, true, true, true );
  M5.Power.begin();

  batteryUpdateTime = 0;
  lastBatteryLevel = 0;

  FtdiAsync.flagOnInit = false;
  Serial.printf( "Start\n" );
  Serial.printf( "BatteryLevel = %d\n", getBatteryLevel() );

  // Display base images
  M5.Lcd.fillScreen( TFT_BLACK );
  M5.Lcd.setTextSize( 1 );
  M5.Lcd.setTextColor( TFT_WHITE, TFT_BLACK );
  M5.Lcd.drawString( "ASCOM Canon EF Lens Controller", 0, 0, 2 );

  labelLensNameTitle = new LabelEx( 0, 32, 32, 16 );
  labelApertureTitle = new LabelEx( 0, 80, 64, 16 );
  labelFocusTitle = new LabelEx( 0, 132, 64, 16 );

  labelLensName = new LabelEx( 40, 24, 260, 32 );
  labelAperture = new LabelEx( 64, 64, 100, 48 );
  labelFocus = new LabelEx( 64, 120, 150, 48 );

  labelLensNameTitle->frameRect( TFT_BLACK, TFT_BLACK );
  labelLensNameTitle->alignment = taLeftJustify;
  labelLensNameTitle->textSize = 2;
  labelLensNameTitle->caption( TFT_WHITE, "Lens" );

  labelLensName->frameRect( TFT_WHITE, TFT_BLUE, 4 );
  labelLensName->alignment = taCenter;
  labelLensName->textSize = 2;

  labelStatus = new LabelEx( 0, 176, 319, 40 );
  labelStatus->frameRect( TFT_BLACK, TFT_BLACK, 4 );
  labelStatus->alignment = taLeftJustify;

  labelBtnA = new LabelEx( 26, 220, 76, 16 );
  labelBtnA->frameRect( TFT_WHITE, TFT_BLACK, 4 );
  labelBtnA->alignment = taCenter;
  labelBtnA->textSize = 2;
  labelBtnA->caption( TFT_WHITE, "SEL" );

  labelBtnB = new LabelEx( 122, 220, 76, 16 );
  labelBtnB->frameRect( TFT_WHITE, TFT_BLACK, 4 );
  labelBtnB->alignment = taCenter;
  labelBtnB->textSize = 2;
  labelBtnB->caption( TFT_WHITE, "DOWN" );

  labelBtnC = new LabelEx( 218, 220, 76, 16 );
  labelBtnC->frameRect( TFT_WHITE, TFT_BLACK, 4 );
  labelBtnC->alignment = taCenter;
  labelBtnC->textSize = 2;
  labelBtnC->caption( TFT_WHITE, "UP" );

  delay( 300 );

  phase = PHASE_WAIT_CONNECT;
  readLensInfoFile();
  readSystemFile();

  delay( 300 );

  labelLensNameTitle->caption( TFT_GREEN, "Lens" );
  lensSelect();
  
  String USB_STATUS;
  if ( Usb.Init() == -1 ) {
    Serial.println( "OSC did not start." );
    USB_STATUS = "OSC did not start.";
  }else{
    Serial.println( "OSC started." );
    USB_STATUS = "Waiting for the lens controller to be connected.";
  }
  labelStatus->caption( TFT_YELLOW, USB_STATUS );
  delay( 300 );

}

// Main Loop
/*************************************************************************
 * NAME  loop - 
 *
 * SYNOPSIS
 *
 *    void loop( void )
 *
 * DESCRIPTION
 *  
 *************************************************************************/
void loop( void )
{
  Usb.Task();
  M5.update();

  switch ( phase ) {
  case PHASE_WAIT_CONNECT:  // // Waiting for the lens controller to be connected.
    if ( FtdiAsync.flagOnInit ) {
      String USB_STATUS;
      USB_STATUS = "USB FTDI CDC Baud Rate:" + String( baud ) + "bps";
      labelStatus->caption( TFT_YELLOW, USB_STATUS );
      Ftdi.SndData( 2, (uint8_t*)"P#" );
      phase = PHASE_LENS;
    }
    break;
  }

  switch ( phase ) {
  case PHASE_LENS:    // Lens selection in progress.
    if ( M5.BtnA.wasPressed() ) {
      phase = PHASE_APERTURE;
      labelApertureTitle->frameRect( TFT_BLACK, TFT_BLACK );
      labelApertureTitle->alignment = taLeftJustify;
      labelApertureTitle->textSize = 2;
      labelApertureTitle->caption( TFT_WHITE, "Aperture" );
      labelFocusTitle->frameRect( TFT_BLACK, TFT_BLACK );
      labelFocusTitle->alignment = taLeftJustify;
      labelFocusTitle->textSize = 2;
      labelFocusTitle->caption( TFT_WHITE, "Focus" );
      labelLensNameTitle->caption( TFT_WHITE, "Lens" );
      labelApertureTitle->caption( TFT_GREEN, "Aperture" );
      labelAperture->frameRect( TFT_WHITE, TFT_RED, 4 );
      labelAperture->alignment = taCenter;
      labelAperture->textSize = 6;
      labelAperture->textBaseOffset = -4;
      labelFocus->frameRect( TFT_WHITE, TFT_RED, 4 );
      labelFocus->alignment = taRightJustify;
      labelFocus->textSize = 6;
      labelFocus->textBaseOffset = -4;
      lensSelect();
      apertureSelect( 0 );
      focusPosition();
      writeSystemFile();
    }
    if ( M5.BtnC.wasPressed() ) {
      lensSelectNext();
    }
    if ( M5.BtnB.wasPressed() ) {
      lensSelectPrev();
    }
    break;
  case PHASE_APERTURE:  // Aperture selection in progress.
    if ( M5.BtnA.wasPressed() ) {
      phase = PHASE_FOCUS;
      labelApertureTitle->caption( TFT_WHITE, "Aperture" );
      labelFocusTitle->caption( TFT_GREEN, "Focus" );
      apertureSelect();
      focusPosition();
    }
    if ( M5.BtnC.wasPressed() ) {
      apertureSelectNext();
    }
    if ( M5.BtnB.wasPressed() ) {
      apertureSelectPrev();
    }
    break;
  case PHASE_FOCUS:   // Adjusting the focus position of the lens.
    if ( M5.BtnA.wasPressed() ) {
      if ( M5.BtnC.isPressed() ) { 
        focusPositionIncrease( +10 );
      } else if ( M5.BtnB.isPressed() ) {
        focusPositionIncrease( -10 );
      } else {
        phase = PHASE_APERTURE;
        labelFocusTitle->caption( TFT_WHITE, "focus" );
        labelApertureTitle->caption( TFT_GREEN, "Aperture" );
        focusPosition();
        apertureSelect();
      }
    }
    if ( M5.BtnC.wasPressed() ) {
      focusPositionIncrease( +1 );
    }
    if ( M5.BtnB.wasPressed() ) {
      focusPositionIncrease( -1 );
    }
    break;
  }

  // Battery indicator of one second interval.
  // refererd by ProgramResource.net. Thanks ねふぁさん
  if ( batteryUpdateTime < millis() ) {
    batteryUpdateTime = millis() + BATTERYUPDATETIMEMS;
    int batteryLevel = getBatteryLevel();
    if ( lastBatteryLevel != batteryLevel ) {
      // Rewrite only when the battery state changes.
      const int xBase = 256;
      const int yBase = 0;
      M5.Lcd.fillRect( xBase, yBase, 56, 21, RGB( 31, 63, 31 ) );
      M5.Lcd.fillRect( xBase + 56, yBase + 4, 4, 13, RGB( 31, 63, 31 ) );
      M5.Lcd.fillRect( xBase + 2, yBase + 2, 52, 17, RGB( 0, 0, 0 ) );
      if ( batteryLevel <= 25 ) {
        M5.Lcd.fillRect( xBase + 3, yBase + 3, batteryLevel / 2, 15, RGB( 31, 20, 10 ) ); // for low level alert.
      } else {
        M5.Lcd.fillRect( xBase + 3, yBase + 3, batteryLevel / 2, 15, RGB( 20, 40, 31 ) ); // for normal state.
      }
      lastBatteryLevel = batteryLevel;
    }
  }

  // USB data processing
  if ( queue.count() > 0 ) {  // Check for serial command
    String replystr;
    replystr = queue.pop();   // Take out receive data
    Serial.println( replystr );
    systemParam.focusPosition = replystr.toInt(); // Set current focus position
    setApertureValue( 0 );  // Set the aperture wide open.
  }

  // USB data receive
  if ( Usb.getUsbTaskState() == USB_STATE_RUNNING ) {
    uint8_t rcode;
    uint8_t buff[64];

    memset( buff, 0, 64 );
    uint16_t rcvd = 64;
    rcode = Ftdi.RcvData( &rcvd, buff );

    if ( rcode && rcode != hrNAK ) {
      ErrorMessage<uint8_t>( PSTR("Ret"), rcode );
    }
    // The device reserves the first two bytes of data
    //   to contain the current values of the modem and line status registers.
    if ( rcvd > 2 ) {
      for ( int i = 2; i < rcvd; i++ ) {
        char inChar = buff[i];
        if ( inChar == '#' ) {
          recvLineIndex = 0;
          queue.push( String( recvLine ) );
          memset( recvLine, 0, RECVLINES );
        } else {
          recvLine[recvLineIndex++] = inChar;
          if ( recvLineIndex >= RECVLINES ) {
            recvLineIndex = RECVLINES - 1;
          }
        }
      }
    }
  }
}

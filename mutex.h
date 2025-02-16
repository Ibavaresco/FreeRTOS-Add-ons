//==============================================================================
// Copyright (c) 2007-2009, Isaac Marino Bavaresco
// All rights reserved
// isaacbavaresco@yahoo.com.br
//==============================================================================
#ifndef		__MUTEX_H__
#define		__MUTEX_H__
//==============================================================================
#include "FreeRTOS.h"
//==============================================================================
typedef void			*xMutexHandle;

xMutexHandle			xMutexCreate( void );
signed portBASE_TYPE	xMutexTake( xMutexHandle pxMutex, portTickType xTicksToWait );
signed portBASE_TYPE	xMutexGive( xMutexHandle pxMutex, portBASE_TYPE Release );
signed portBASE_TYPE	xDoIOwnTheMutex( xMutexHandle pxMutex );
//==============================================================================
#endif	//	__MUTEX_H__
//==============================================================================
